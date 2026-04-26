#include "protogpu/broker_transport.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace protogpu {
namespace {

constexpr std::uint32_t kWireMagic = 0x50544750u;  // PTGP
constexpr std::uint32_t kWireVersion = 1u;

enum class WireType : std::uint32_t {
  Request = 1,
  Response = 2,
};

struct WireHeader final {
  std::uint32_t magic = kWireMagic;
  std::uint32_t version = kWireVersion;
  std::uint32_t type = 0;
  std::uint32_t reserved0 = 0;
  std::uint64_t payload_bytes = 0;
};

struct WireRequestMeta final {
  protogpu_submit_job job{};
  std::uint32_t buffer_count = 0;
  std::uint32_t reserved0 = 0;
  std::uint64_t arena_bytes = 0;
};

struct WireBufferDesc final {
  std::uint64_t handle = 0;
  std::uint32_t flags = 0;
  std::uint32_t reserved0 = 0;
  std::uint64_t bytes = 0;
};

struct WireResponseMeta final {
  std::uint32_t status = PROTOGPU_STATUS_RUNTIME_ERROR;
  std::uint32_t reserved0 = 0;
  std::uint64_t steps = 0;
  std::uint64_t diag_bytes = 0;
  std::uint64_t trace_bytes = 0;
  std::uint64_t stats_bytes = 0;
  std::uint32_t buffer_count = 0;
  std::uint32_t reserved1 = 0;
};

bool read_full(int fd, void* data, std::size_t bytes) {
  auto* ptr = static_cast<std::uint8_t*>(data);
  std::size_t done = 0;
  while (done < bytes) {
    const ssize_t rc = ::read(fd, ptr + done, bytes - done);
    if (rc == 0) return false;
    if (rc < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    done += static_cast<std::size_t>(rc);
  }
  return true;
}

bool write_full(int fd, const void* data, std::size_t bytes) {
  const auto* ptr = static_cast<const std::uint8_t*>(data);
  std::size_t done = 0;
  while (done < bytes) {
    const ssize_t rc = ::write(fd, ptr + done, bytes - done);
    if (rc < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    done += static_cast<std::size_t>(rc);
  }
  return true;
}

bool append_bytes(std::vector<std::uint8_t>& out, const void* p, std::size_t n) {
  const auto* b = static_cast<const std::uint8_t*>(p);
  out.insert(out.end(), b, b + n);
  return true;
}

bool append_string(std::vector<std::uint8_t>& out, const std::string& s) {
  return append_bytes(out, s.data(), s.size());
}

bool set_sockaddr(const std::string& socket_path, sockaddr_un& addr) {
  if (socket_path.size() >= sizeof(addr.sun_path)) return false;
  std::memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  std::memcpy(addr.sun_path, socket_path.c_str(), socket_path.size() + 1);
  return true;
}

BrokerExecutionResult bad_wire(const std::string& why) {
  BrokerExecutionResult out;
  out.status = PROTOGPU_STATUS_RUNTIME_ERROR;
  out.diagnostic = "broker transport error: " + why;
  return out;
}

bool process_one_client(int fd) {
  WireHeader req_hdr{};
  if (!read_full(fd, &req_hdr, sizeof(req_hdr))) return false;

  if (req_hdr.magic != kWireMagic || req_hdr.version != kWireVersion || req_hdr.type != static_cast<std::uint32_t>(WireType::Request)) {
    return false;
  }

  std::vector<std::uint8_t> payload(static_cast<std::size_t>(req_hdr.payload_bytes));
  if (!payload.empty() && !read_full(fd, payload.data(), payload.size())) return false;

  const auto need = sizeof(WireRequestMeta);
  if (payload.size() < need) return false;

  WireRequestMeta req_meta{};
  std::memcpy(&req_meta, payload.data(), sizeof(req_meta));

  std::size_t cursor = sizeof(WireRequestMeta);
  const std::size_t desc_bytes = static_cast<std::size_t>(req_meta.buffer_count) * sizeof(WireBufferDesc);
  if (cursor + desc_bytes > payload.size()) return false;

  std::vector<WireBufferDesc> descs(req_meta.buffer_count);
  if (!descs.empty()) {
    std::memcpy(descs.data(), payload.data() + cursor, desc_bytes);
  }
  cursor += desc_bytes;

  if (cursor + req_meta.arena_bytes > payload.size()) return false;
  std::vector<std::uint8_t> arena(req_meta.arena_bytes);
  if (!arena.empty()) {
    std::memcpy(arena.data(), payload.data() + cursor, arena.size());
  }
  cursor += arena.size();

  std::vector<MappedBuffer> mapped;
  mapped.reserve(descs.size());
  for (const auto& d : descs) {
    if (cursor + d.bytes > payload.size()) return false;
    MappedBuffer m;
    m.handle = d.handle;
    m.flags = d.flags;
    m.bytes.resize(static_cast<std::size_t>(d.bytes));
    if (!m.bytes.empty()) {
      std::memcpy(m.bytes.data(), payload.data() + cursor, m.bytes.size());
    }
    cursor += m.bytes.size();
    mapped.push_back(std::move(m));
  }

  auto exec = execute_blocking_job(req_meta.job, arena, mapped);

  WireResponseMeta resp_meta{};
  resp_meta.status = static_cast<std::uint32_t>(exec.status);
  resp_meta.steps = exec.steps;
  resp_meta.diag_bytes = exec.diagnostic.size();
  resp_meta.trace_bytes = exec.trace_jsonl.size();
  resp_meta.stats_bytes = exec.stats_json.size();
  resp_meta.buffer_count = static_cast<std::uint32_t>(mapped.size());

  std::vector<WireBufferDesc> out_desc;
  out_desc.reserve(mapped.size());
  for (const auto& m : mapped) {
    out_desc.push_back(WireBufferDesc{ m.handle, m.flags, 0, static_cast<std::uint64_t>(m.bytes.size()) });
  }

  std::vector<std::uint8_t> out_payload;
  append_bytes(out_payload, &resp_meta, sizeof(resp_meta));
  if (!out_desc.empty()) append_bytes(out_payload, out_desc.data(), out_desc.size() * sizeof(WireBufferDesc));
  append_string(out_payload, exec.diagnostic);
  append_string(out_payload, exec.trace_jsonl);
  append_string(out_payload, exec.stats_json);
  for (const auto& m : mapped) {
    if (!m.bytes.empty()) append_bytes(out_payload, m.bytes.data(), m.bytes.size());
  }

  WireHeader resp_hdr{};
  resp_hdr.type = static_cast<std::uint32_t>(WireType::Response);
  resp_hdr.payload_bytes = out_payload.size();

  if (!write_full(fd, &resp_hdr, sizeof(resp_hdr))) return false;
  if (!out_payload.empty() && !write_full(fd, out_payload.data(), out_payload.size())) return false;
  return true;
}

}  // namespace

BrokerExecutionResult execute_blocking_job_remote(const std::string& socket_path,
                                                  const protogpu_submit_job& job,
                                                  const std::vector<std::uint8_t>& arena,
                                                  std::vector<MappedBuffer>& mapped_buffers) {
  BrokerExecutionResult fail = bad_wire("connect failed");

  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return fail;

  sockaddr_un addr{};
  if (!set_sockaddr(socket_path, addr)) {
    ::close(fd);
    return bad_wire("socket path too long");
  }

  if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
    ::close(fd);
    return fail;
  }

  WireRequestMeta req_meta{};
  req_meta.job = job;
  req_meta.buffer_count = static_cast<std::uint32_t>(mapped_buffers.size());
  req_meta.arena_bytes = arena.size();

  std::vector<WireBufferDesc> descs;
  descs.reserve(mapped_buffers.size());
  for (const auto& m : mapped_buffers) {
    descs.push_back(WireBufferDesc{ m.handle, m.flags, 0, static_cast<std::uint64_t>(m.bytes.size()) });
  }

  std::vector<std::uint8_t> payload;
  append_bytes(payload, &req_meta, sizeof(req_meta));
  if (!descs.empty()) append_bytes(payload, descs.data(), descs.size() * sizeof(WireBufferDesc));
  if (!arena.empty()) append_bytes(payload, arena.data(), arena.size());
  for (const auto& m : mapped_buffers) {
    if (!m.bytes.empty()) append_bytes(payload, m.bytes.data(), m.bytes.size());
  }

  WireHeader req_hdr{};
  req_hdr.type = static_cast<std::uint32_t>(WireType::Request);
  req_hdr.payload_bytes = payload.size();

  if (!write_full(fd, &req_hdr, sizeof(req_hdr)) || (!payload.empty() && !write_full(fd, payload.data(), payload.size()))) {
    ::close(fd);
    return bad_wire("send failed");
  }

  WireHeader resp_hdr{};
  if (!read_full(fd, &resp_hdr, sizeof(resp_hdr))) {
    ::close(fd);
    return bad_wire("recv header failed");
  }
  if (resp_hdr.magic != kWireMagic || resp_hdr.version != kWireVersion || resp_hdr.type != static_cast<std::uint32_t>(WireType::Response)) {
    ::close(fd);
    return bad_wire("invalid response header");
  }

  std::vector<std::uint8_t> resp_payload(static_cast<std::size_t>(resp_hdr.payload_bytes));
  if (!resp_payload.empty() && !read_full(fd, resp_payload.data(), resp_payload.size())) {
    ::close(fd);
    return bad_wire("recv payload failed");
  }
  ::close(fd);

  if (resp_payload.size() < sizeof(WireResponseMeta)) {
    return bad_wire("response too small");
  }
  WireResponseMeta resp_meta{};
  std::memcpy(&resp_meta, resp_payload.data(), sizeof(resp_meta));

  std::size_t cursor = sizeof(resp_meta);
  std::size_t desc_bytes = static_cast<std::size_t>(resp_meta.buffer_count) * sizeof(WireBufferDesc);
  if (cursor + desc_bytes > resp_payload.size()) return bad_wire("response desc out of bounds");

  std::vector<WireBufferDesc> out_desc(resp_meta.buffer_count);
  if (!out_desc.empty()) std::memcpy(out_desc.data(), resp_payload.data() + cursor, desc_bytes);
  cursor += desc_bytes;

  auto take_string = [&](std::size_t n, std::string& out) -> bool {
    if (cursor + n > resp_payload.size()) return false;
    out.assign(reinterpret_cast<const char*>(resp_payload.data() + cursor), n);
    cursor += n;
    return true;
  };

  BrokerExecutionResult out;
  out.status = static_cast<protogpu_status_code>(resp_meta.status);
  out.steps = resp_meta.steps;
  if (!take_string(resp_meta.diag_bytes, out.diagnostic)) return bad_wire("diag out of bounds");
  if (!take_string(resp_meta.trace_bytes, out.trace_jsonl)) return bad_wire("trace out of bounds");
  if (!take_string(resp_meta.stats_bytes, out.stats_json)) return bad_wire("stats out of bounds");

  std::unordered_map<std::uint64_t, MappedBuffer*> by_handle;
  for (auto& m : mapped_buffers) by_handle[m.handle] = &m;

  for (const auto& d : out_desc) {
    auto it = by_handle.find(d.handle);
    if (it == by_handle.end()) return bad_wire("unknown buffer handle in response");
    if (cursor + d.bytes > resp_payload.size()) return bad_wire("buffer bytes out of bounds");
    it->second->flags = d.flags;
    it->second->bytes.resize(static_cast<std::size_t>(d.bytes));
    if (d.bytes != 0) {
      std::memcpy(it->second->bytes.data(), resp_payload.data() + cursor, static_cast<std::size_t>(d.bytes));
    }
    cursor += static_cast<std::size_t>(d.bytes);
  }

  if (cursor != resp_payload.size()) return bad_wire("trailing response bytes");
  return out;
}

int run_broker_daemon(const std::string& socket_path) {
  const int listen_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (listen_fd < 0) return 2;

  ::unlink(socket_path.c_str());

  sockaddr_un addr{};
  if (!set_sockaddr(socket_path, addr)) {
    ::close(listen_fd);
    return 2;
  }
  if (::bind(listen_fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
    ::close(listen_fd);
    return 2;
  }
  if (::listen(listen_fd, 8) != 0) {
    ::close(listen_fd);
    return 2;
  }

  while (true) {
    int client_fd = ::accept(listen_fd, nullptr, nullptr);
    if (client_fd < 0) {
      if (errno == EINTR) continue;
      break;
    }
    (void)process_one_client(client_fd);
    ::close(client_fd);
  }

  ::close(listen_fd);
  ::unlink(socket_path.c_str());
  return 0;
}

}  // namespace protogpu
