#include "protogpu_ioctl.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

protogpu_blob_ref append_string(std::vector<std::uint8_t>& arena, const std::string& text) {
  const auto offset = static_cast<std::uint64_t>(arena.size());
  arena.insert(arena.end(), text.begin(), text.end());
  return protogpu_blob_ref{ offset, static_cast<std::uint64_t>(text.size()) };
}

template <typename T>
protogpu_record_ref append_records(std::vector<std::uint8_t>& arena, const std::vector<T>& items) {
  const auto offset = static_cast<std::uint64_t>(arena.size());
  if (!items.empty()) {
    const auto* begin = reinterpret_cast<const std::uint8_t*>(items.data());
    arena.insert(arena.end(), begin, begin + items.size() * sizeof(T));
  }
  return protogpu_record_ref{ offset, static_cast<std::uint32_t>(items.size()), 0 };
}

std::uint32_t unpack_u32_le(const std::uint8_t* bytes) {
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8) |
         (static_cast<std::uint32_t>(bytes[2]) << 16) |
         (static_cast<std::uint32_t>(bytes[3]) << 24);
}

}  // namespace

int main() {
  const char* node = "/dev/protogpu0";
  int fd = ::open(node, O_RDWR | O_CLOEXEC);
  if (fd < 0) {
    std::fprintf(stderr, "open %s failed: %s\n", node, std::strerror(errno));
    std::fprintf(stderr, "note: this sample expects the future kernel driver to create %s\n", node);
    return 1;
  }

  protogpu_ioctl_create_ctx create_ctx{};
  create_ctx.uapi_version = PROTOGPU_IOCTL_UAPI_VERSION_V1;

  if (::ioctl(fd, PROTOGPU_IOCTL_CREATE_CTX, &create_ctx) != 0) {
    std::fprintf(stderr, "CREATE_CTX ioctl failed: %s\n", std::strerror(errno));
    ::close(fd);
    return 1;
  }

  std::printf("created ctx_id=%llu\n", static_cast<unsigned long long>(create_ctx.ctx_id));

  protogpu_ioctl_set_broker set_broker{};
  std::snprintf(set_broker.unix_socket_path,
                sizeof(set_broker.unix_socket_path),
                "%s",
                "/tmp/protogpu-broker.sock");
  if (::ioctl(fd, PROTOGPU_IOCTL_SET_BROKER, &set_broker) != 0) {
    std::fprintf(stderr, "SET_BROKER ioctl failed: %s\n", std::strerror(errno));
    ::close(fd);
    return 1;
  }

  protogpu_ioctl_alloc_bo alloc{};
  alloc.ctx_id = create_ctx.ctx_id;
  alloc.bytes = 4;
  alloc.align = 4;
  alloc.flags = PROTOGPU_BUFFER_INPUT | PROTOGPU_BUFFER_OUTPUT;
  if (::ioctl(fd, PROTOGPU_IOCTL_ALLOC_BO, &alloc) != 0) {
    std::fprintf(stderr, "ALLOC_BO ioctl failed: %s\n", std::strerror(errno));
    ::close(fd);
    return 1;
  }
  std::printf("allocated bo handle=%llu bytes=%llu\n",
              static_cast<unsigned long long>(alloc.handle),
              static_cast<unsigned long long>(alloc.bytes));

  void* mapped = ::mmap(nullptr,
                        static_cast<size_t>(alloc.bytes),
                        PROT_READ | PROT_WRITE,
                        MAP_SHARED,
                        fd,
                        static_cast<off_t>(alloc.mmap_offset));
  if (mapped == MAP_FAILED) {
    std::fprintf(stderr, "mmap failed: %s\n", std::strerror(errno));
    ::close(fd);
    return 1;
  }
  std::memset(mapped, 0, static_cast<size_t>(alloc.bytes));

  std::vector<std::uint8_t> arena;
  const auto ptx_ref = append_string(arena, R"PTX(
.version 6.4
.target sm_70
.address_size 64

.visible .entry write_value(
    .param .u64 out_ptr,
    .param .u32 value
)
{
  .reg .u64 %rd<2>;
  .reg .u32 %r<2>;

  ld.param.u64 %rd1, [out_ptr];
  ld.param.u32 %r1, [value];
  st.global.u32 [%rd1], %r1;
  ret;
}
)PTX");
  const auto isa_ref = append_string(arena, R"JSON(
{
  "insts": [
    {"ptx_opcode":"ld","type_mod":"u64","operand_kinds":["reg","symbol"],"ir_op":"ld"},
    {"ptx_opcode":"ld","type_mod":"u32","operand_kinds":["reg","symbol"],"ir_op":"ld"},
    {"ptx_opcode":"st","type_mod":"u32","operand_kinds":["addr","reg"],"ir_op":"st"},
    {"ptx_opcode":"ret","type_mod":"","operand_kinds":[],"ir_op":"ret"}
  ]
}
)JSON");
  const auto desc_ref = append_string(arena, R"JSON(
{
  "insts": [
    {"opcode":"ld","type_mod":"u64","operand_kinds":["reg","symbol"],"uops":[{"kind":"MEM","op":"LD","in":[1],"out":[0]}]},
    {"opcode":"ld","type_mod":"u32","operand_kinds":["reg","symbol"],"uops":[{"kind":"MEM","op":"LD","in":[1],"out":[0]}]},
    {"opcode":"st","type_mod":"u32","operand_kinds":["addr","reg"],"uops":[{"kind":"MEM","op":"ST","in":[0,1],"out":[]}]},
    {"opcode":"ret","type_mod":"","operand_kinds":[],"uops":[{"kind":"CTRL","op":"RET","in":[],"out":[]}]}
  ]
}
)JSON");
  const auto cfg_ref = append_string(arena, "{\"sim\":{\"warp_size\":1,\"max_steps\":64},\"observability\":{\"enabled\":true,\"trace_capacity\":64}}");
  const auto entry_ref = append_string(arena, "write_value");

  std::vector<protogpu_arg_desc> args = {
    protogpu_arg_desc{ PROTOGPU_ARG_BUFFER_HANDLE, 0, alloc.handle, protogpu_blob_ref{ 0, 0 } },
    protogpu_arg_desc{ PROTOGPU_ARG_U32, 0, 321, protogpu_blob_ref{ 0, 0 } },
  };
  const auto args_ref = append_records(arena, args);
  std::vector<protogpu_buffer_binding> bindings = {
    protogpu_buffer_binding{ alloc.handle, PROTOGPU_BUFFER_OUTPUT, 0 },
  };
  const auto bindings_ref = append_records(arena, bindings);

  protogpu_ioctl_submit_job submit{};
  submit.ctx_id = create_ctx.ctx_id;
  submit.job_id = 0;
  submit.job.uapi_version = PROTOGPU_UAPI_VERSION_V1;
  submit.job.module = protogpu_module_desc{ ptx_ref, isa_ref, desc_ref, cfg_ref, entry_ref };
  submit.job.launch.grid_dim = protogpu_dim3{ 1, 1, 1 };
  submit.job.launch.block_dim = protogpu_dim3{ 1, 1, 1 };
  submit.job.launch.args = args_ref;
  submit.job.buffer_bindings = bindings_ref;
  submit.arena_user_ptr = reinterpret_cast<std::uint64_t>(arena.data());
  submit.arena_bytes = arena.size();

  if (::ioctl(fd, PROTOGPU_IOCTL_SUBMIT_JOB, &submit) != 0) {
    std::fprintf(stderr, "SUBMIT_JOB ioctl failed: %s\n", std::strerror(errno));
    ::close(fd);
    return 1;
  }
  std::printf("submitted job_id=%llu\n", static_cast<unsigned long long>(submit.job_id));

  protogpu_ioctl_wait_job wait{};
  wait.ctx_id = create_ctx.ctx_id;
  wait.job_id = submit.job_id;
  wait.timeout_ns = 0;
  if (::ioctl(fd, PROTOGPU_IOCTL_WAIT_JOB, &wait) != 0) {
    std::fprintf(stderr, "WAIT_JOB ioctl failed: %s\n", std::strerror(errno));
    ::close(fd);
    return 1;
  }
  std::printf("wait status=%u steps=%llu\n",
              wait.status,
              static_cast<unsigned long long>(wait.steps));

  char diag_buf[256] = {0};
  protogpu_ioctl_read_diag read_diag{};
  read_diag.ctx_id = create_ctx.ctx_id;
  read_diag.job_id = submit.job_id;
  read_diag.user_ptr = reinterpret_cast<std::uint64_t>(diag_buf);
  read_diag.capacity = sizeof(diag_buf) - 1;
  if (::ioctl(fd, PROTOGPU_IOCTL_READ_DIAG, &read_diag) != 0) {
    std::fprintf(stderr, "READ_DIAG ioctl failed: %s\n", std::strerror(errno));
    ::close(fd);
    return 1;
  }
  diag_buf[read_diag.bytes_written < sizeof(diag_buf) ? read_diag.bytes_written : sizeof(diag_buf) - 1] = '\0';
  std::printf("diag(%llu bytes): %s\n",
              static_cast<unsigned long long>(read_diag.bytes_written),
              diag_buf);

  const auto observed = unpack_u32_le(static_cast<const std::uint8_t*>(mapped));
  std::printf("mapped output value=%u\n", observed);
  if (wait.status != PROTOGPU_STATUS_OK || observed != 321u) {
    std::fprintf(stderr, "unexpected execution result (status=%u, value=%u)\n", wait.status, observed);
    ::munmap(mapped, static_cast<size_t>(alloc.bytes));
    ::close(fd);
    return 1;
  }

  protogpu_ioctl_free_bo free_bo{};
  free_bo.ctx_id = create_ctx.ctx_id;
  free_bo.handle = alloc.handle;
  if (::ioctl(fd, PROTOGPU_IOCTL_FREE_BO, &free_bo) != 0) {
    std::fprintf(stderr, "FREE_BO ioctl failed: %s\n", std::strerror(errno));
    ::close(fd);
    return 1;
  }

  ::munmap(mapped, static_cast<size_t>(alloc.bytes));

  protogpu_ioctl_destroy_ctx destroy_ctx{};
  destroy_ctx.ctx_id = create_ctx.ctx_id;

  if (::ioctl(fd, PROTOGPU_IOCTL_DESTROY_CTX, &destroy_ctx) != 0) {
    std::fprintf(stderr, "DESTROY_CTX ioctl failed: %s\n", std::strerror(errno));
    ::close(fd);
    return 1;
  }

  std::printf("destroyed ctx_id=%llu\n", static_cast<unsigned long long>(destroy_ctx.ctx_id));
  ::close(fd);
  return 0;
}
