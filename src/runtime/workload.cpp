#include "gpusim/runtime.h"

#include "gpusim/json.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gpusim {

namespace {

struct SchemaError final {
  Diagnostic diag;
};

[[noreturn]] void schema_fail(const std::string& message) {
  throw SchemaError{ Diagnostic{ "runtime", "E_WORKLOAD_SCHEMA", message, std::nullopt, std::nullopt, std::nullopt } };
}

[[noreturn]] void ref_fail(const std::string& message) {
  throw SchemaError{ Diagnostic{ "runtime", "E_WORKLOAD_REF", message, std::nullopt, std::nullopt, std::nullopt } };
}

std::string slurp(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("cannot open " + path);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

const json::Value& require_field(const json::Object& o, const char* key) {
  auto it = o.find(key);
  if (it == o.end()) schema_fail(std::string("missing field: ") + key);
  return it->second;
}

std::string require_string(const json::Value& v, const char* ctx) {
  if (!v.is_string()) schema_fail(std::string(ctx) + ": expected string");
  return v.as_string();
}

std::uint64_t require_u64(const json::Value& v, const char* ctx) {
  if (!v.is_number()) schema_fail(std::string(ctx) + ": expected number");
  return v.as_u64();
}

Dim3 parse_dim3(const json::Value& v, const char* ctx) {
  if (v.is_object()) {
    const auto& o = v.as_object();
    auto x = require_u64(require_field(o, "x"), ctx);
    auto y = require_u64(require_field(o, "y"), ctx);
    auto z = require_u64(require_field(o, "z"), ctx);
    return Dim3{ static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), static_cast<std::uint32_t>(z) };
  }
  if (v.is_array()) {
    const auto& a = v.as_array();
    if (a.size() != 3) schema_fail(std::string(ctx) + ": expected [x,y,z]");
    return Dim3{ static_cast<std::uint32_t>(a[0].as_u64()),
                 static_cast<std::uint32_t>(a[1].as_u64()),
                 static_cast<std::uint32_t>(a[2].as_u64()) };
  }
  schema_fail(std::string(ctx) + ": expected object {x,y,z} or [x,y,z]");
}

struct BufRef final {
  std::string name;
  std::uint64_t offset = 0;
};

BufRef parse_buf_ref(const json::Value& v, const char* ctx) {
  if (v.is_string()) {
    return BufRef{ v.as_string(), 0 };
  }
  if (v.is_object()) {
    const auto& o = v.as_object();
    BufRef r;
    r.name = require_string(require_field(o, "buffer"), ctx);
    if (auto* off = v.get("offset")) {
      r.offset = require_u64(*off, ctx);
    }
    return r;
  }
  schema_fail(std::string(ctx) + ": expected string buffer name or {buffer, offset?}");
}

int hex_val(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
  if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
  return -1;
}

std::vector<std::uint8_t> parse_hex_bytes(std::string s) {
  if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    s = s.substr(2);
  }
  s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c) != 0; }), s.end());
  if (s.size() % 2 != 0) schema_fail("hex string length must be even");

  std::vector<std::uint8_t> out;
  out.reserve(s.size() / 2);
  for (std::size_t i = 0; i < s.size(); i += 2) {
    int hi = hex_val(s[i]);
    int lo = hex_val(s[i + 1]);
    if (hi < 0 || lo < 0) schema_fail("invalid hex digit");
    out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
  }
  return out;
}

struct HostBufDecl final {
  std::uint64_t bytes = 0;
  enum class InitKind : std::uint8_t { Zeros, Hex, File } init_kind = InitKind::Zeros;
  std::string init_payload;
};

struct DeviceBufDecl final {
  std::uint64_t bytes = 0;
  std::uint64_t align = 16;
};

struct ModuleDecl final {
  std::string ptx;
  std::string ptx_isa;
  std::string inst_desc;
};

enum class CommandKind : std::uint8_t { Copy, Kernel, EventRecord, EventWait, Sync };

enum class CopyKind : std::uint8_t { H2D, D2H, D2D, Memset };

struct CopyPayload final {
  CopyKind kind = CopyKind::H2D;
  HostBufId host = 0;
  std::uint64_t host_off = 0;
  DevicePtr dev = 0;
  std::uint64_t dev_off = 0;
  std::uint64_t bytes = 0;
};

struct ArgValue final {
  enum class Kind : std::uint8_t { U32, U64, PtrDevice, BytesHex } kind = Kind::U32;
  std::uint64_t u64 = 0;
  std::string str;
};

struct KernelPayload final {
  ModuleDecl mod;
  std::string module_name;
  std::string entry;
  LaunchConfig launch;
  std::unordered_map<std::string, ArgValue> args;
};

struct EventPayload final {
  EventId event_id = 0;
  std::string event_name;
};

struct Command final {
  CmdId cmd_id = 0;
  StreamId stream_id = 0;
  CommandKind kind = CommandKind::Sync;
  std::optional<CopyPayload> copy;
  std::optional<KernelPayload> kernel;
  std::optional<EventPayload> event;
};

static ArgValue parse_arg_value(const json::Value& v, const char* ctx) {
  if (!v.is_object()) schema_fail(std::string(ctx) + ": arg value must be object oneof {u32,u64,ptr_device,bytes_hex}");
  const auto& o = v.as_object();
  if (o.size() != 1) schema_fail(std::string(ctx) + ": arg value object must have exactly one key");
  const auto& [k, vv] = *o.begin();

  ArgValue av;
  if (k == "u32") {
    av.kind = ArgValue::Kind::U32;
    av.u64 = require_u64(vv, ctx);
    return av;
  }
  if (k == "u64") {
    av.kind = ArgValue::Kind::U64;
    av.u64 = require_u64(vv, ctx);
    return av;
  }
  if (k == "ptr_device") {
    av.kind = ArgValue::Kind::PtrDevice;
    av.str = require_string(vv, ctx);
    return av;
  }
  if (k == "bytes_hex") {
    av.kind = ArgValue::Kind::BytesHex;
    av.str = require_string(vv, ctx);
    return av;
  }
  schema_fail(std::string(ctx) + ": unknown arg kind: " + k);
}

static void write_le(std::vector<std::uint8_t>& blob, std::uint32_t off, std::uint64_t v, std::uint32_t size) {
  if (static_cast<std::uint64_t>(off) + size > blob.size()) schema_fail("param blob write out of range");
  for (std::uint32_t b = 0; b < size; b++) {
    blob[static_cast<std::size_t>(off + b)] = static_cast<std::uint8_t>((v >> (8u * b)) & 0xFFu);
  }
}

static KernelArgs pack_kernel_args(Runtime& rt,
                                  const ModuleDecl& mod,
                                  const std::string& entry,
                                  const std::unordered_map<std::string, ArgValue>& args,
                                  const std::unordered_map<std::string, std::pair<DevicePtr, std::uint64_t>>& dev_bufs) {
  Parser parser;
  auto mod_tokens = parser.parse_ptx_file_tokens(mod.ptx);
  Binder binder;
  KernelTokens kernel_tokens;
  try {
    kernel_tokens = binder.bind_kernel_by_name(mod_tokens, entry);
  } catch (const std::exception&) {
    throw SchemaError{ Diagnostic{ "runtime", "E_ENTRY_NOT_FOUND", "kernel entry not found: " + entry, std::nullopt, entry, std::nullopt } };
  }

  KernelArgs ka;
  ka.layout = kernel_tokens.params;

  std::uint32_t blob_bytes = 0;
  for (const auto& p : ka.layout) {
    blob_bytes = std::max(blob_bytes, static_cast<std::uint32_t>(p.offset + p.size));
  }
  ka.blob.assign(static_cast<std::size_t>(blob_bytes), 0);

  std::unordered_map<std::string, ParamDesc> layout_by_name;
  for (const auto& p : ka.layout) layout_by_name.emplace(p.name, p);

  for (const auto& [name, av] : args) {
    auto it = layout_by_name.find(name);
    if (it == layout_by_name.end()) {
      throw SchemaError{ Diagnostic{ "runtime", "E_BAD_ARGS", "unknown kernel arg: " + name, std::nullopt, entry, std::nullopt } };
    }
    const auto& p = it->second;

    if (av.kind == ArgValue::Kind::U32) {
      if (p.type != ParamType::U32 || p.size != 4) {
        throw SchemaError{ Diagnostic{ "runtime", "E_BAD_ARGS", "arg type mismatch for " + name, std::nullopt, entry, std::nullopt } };
      }
      write_le(ka.blob, p.offset, av.u64, 4);
    } else if (av.kind == ArgValue::Kind::U64) {
      if (p.type != ParamType::U64 || p.size != 8) {
        throw SchemaError{ Diagnostic{ "runtime", "E_BAD_ARGS", "arg type mismatch for " + name, std::nullopt, entry, std::nullopt } };
      }
      write_le(ka.blob, p.offset, av.u64, 8);
    } else if (av.kind == ArgValue::Kind::PtrDevice) {
      if (p.type != ParamType::U64 || p.size != 8) {
        throw SchemaError{ Diagnostic{ "runtime", "E_BAD_ARGS", "ptr_device expects u64 param for " + name, std::nullopt, entry, std::nullopt } };
      }
      auto dit = dev_bufs.find(av.str);
      if (dit == dev_bufs.end()) {
        throw SchemaError{ Diagnostic{ "runtime", "E_WORKLOAD_REF", "device buffer not found: " + av.str, std::nullopt, entry, std::nullopt } };
      }
      write_le(ka.blob, p.offset, dit->second.first, 8);
    } else if (av.kind == ArgValue::Kind::BytesHex) {
      auto bytes = parse_hex_bytes(av.str);
      if (bytes.size() != p.size) {
        throw SchemaError{ Diagnostic{ "runtime", "E_BAD_ARGS", "bytes_hex size mismatch for " + name, std::nullopt, entry, std::nullopt } };
      }
      if (static_cast<std::uint64_t>(p.offset) + bytes.size() > ka.blob.size()) schema_fail("param blob write out of range");
      for (std::size_t i = 0; i < bytes.size(); i++) {
        ka.blob[static_cast<std::size_t>(p.offset) + i] = bytes[i];
      }
    }
  }

  for (const auto& p : ka.layout) {
    if (args.find(p.name) == args.end()) {
      throw SchemaError{ Diagnostic{ "runtime", "E_BAD_ARGS", "missing kernel arg: " + p.name, std::nullopt, entry, std::nullopt } };
    }
  }

  (void)rt;
  return ka;
}

static Event make_stream_event(std::uint64_t ts, const std::string& action, StreamId stream_id, CmdId cmd_id,
                              std::optional<EventId> event_id = std::nullopt) {
  Event e;
  e.ts = ts;
  e.category = EventCategory::Stream;
  e.action = action;
  e.stream_id = stream_id;
  e.cmd_id = cmd_id;
  e.event_id = event_id;
  return e;
}

} // namespace

RunOutputs Runtime::run_workload(const std::string& workload_json_path) {
  RunOutputs out;
  out.sim.completed = false;
  out.sim.steps = 0;

  try {
    auto root = gpusim::json::parse(slurp(workload_json_path));
    if (!root.is_object()) schema_fail("root must be object");
    const auto& o = root.as_object();

    // --- Parse buffers/modules/streams ---
    std::map<std::string, HostBufDecl> host_buf_decls;
    std::map<std::string, DeviceBufDecl> dev_buf_decls;
    std::map<std::string, ModuleDecl> modules;

    const auto& buffers = require_field(o, "buffers").as_object();
    const auto& host = require_field(buffers, "host").as_object();
    const auto& device = require_field(buffers, "device").as_object();

    for (const auto& [name, vv] : host) {
      if (!vv.is_object()) schema_fail("buffers.host entry must be object: " + name);
      const auto& ho = vv.as_object();
      HostBufDecl d;
      d.bytes = require_u64(require_field(ho, "bytes"), "buffers.host.bytes");
      if (const auto* initv = vv.get("init")) {
        if (initv->is_string()) {
          auto s = initv->as_string();
          if (s == "zeros") {
            d.init_kind = HostBufDecl::InitKind::Zeros;
          } else {
            schema_fail("unknown host init string: " + s);
          }
        } else if (initv->is_object()) {
          const auto& io = initv->as_object();
          if (io.size() != 1) schema_fail("init must be oneof {zeros,hex,file}");
          const auto& [k, pv] = *io.begin();
          if (k == "zeros") {
            d.init_kind = HostBufDecl::InitKind::Zeros;
          } else if (k == "hex") {
            d.init_kind = HostBufDecl::InitKind::Hex;
            d.init_payload = require_string(pv, "init.hex");
          } else if (k == "file") {
            d.init_kind = HostBufDecl::InitKind::File;
            d.init_payload = require_string(pv, "init.file");
          } else {
            schema_fail("unknown init kind: " + k);
          }
        } else {
          schema_fail("init must be string or object");
        }
      }
      host_buf_decls.emplace(name, std::move(d));
    }

    for (const auto& [name, vv] : device) {
      if (!vv.is_object()) schema_fail("buffers.device entry must be object: " + name);
      const auto& doo = vv.as_object();
      DeviceBufDecl d;
      d.bytes = require_u64(require_field(doo, "bytes"), "buffers.device.bytes");
      if (const auto* av = vv.get("align")) d.align = require_u64(*av, "buffers.device.align");
      dev_buf_decls.emplace(name, std::move(d));
    }

    const auto& mods = require_field(o, "modules").as_object();
    for (const auto& [name, vv] : mods) {
      if (!vv.is_object()) schema_fail("modules entry must be object: " + name);
      const auto& mo = vv.as_object();
      ModuleDecl md;
      md.ptx = require_string(require_field(mo, "ptx"), "modules.ptx");
      md.ptx_isa = require_string(require_field(mo, "ptx_isa"), "modules.ptx_isa");
      md.inst_desc = require_string(require_field(mo, "inst_desc"), "modules.inst_desc");
      modules.emplace(name, std::move(md));
    }

    const auto& streams = require_field(o, "streams").as_object();

    // Pre-collect event names deterministically.
    std::set<std::string> event_names;
    for (const auto& [sname, sv] : streams) {
      if (!sv.is_object()) schema_fail("streams entry must be object: " + sname);
      const auto& so = sv.as_object();
      const auto& cmds = require_field(so, "commands").as_array();
      for (const auto& cmdv : cmds) {
        if (!cmdv.is_object()) schema_fail("command must be object");
        const auto& co = cmdv.as_object();
        if (co.size() != 1) schema_fail("command must be oneof {copy,kernel,event_record,event_wait,sync}");
        const auto& [ck, cv] = *co.begin();
        if (ck == "event_record" || ck == "event_wait") {
          if (!cv.is_object()) schema_fail("event command payload must be object");
          const auto& eo = cv.as_object();
          auto en = require_string(require_field(eo, "event"), "event");
          event_names.insert(en);
        }
      }
    }

    // --- Materialize IDs/resources ---
    std::unordered_map<std::string, std::pair<HostBufId, std::uint64_t>> host_bufs;
    for (const auto& [name, decl] : host_buf_decls) {
      auto id = host_alloc(decl.bytes);

      if (decl.init_kind == HostBufDecl::InitKind::Hex) {
        auto bytes = parse_hex_bytes(decl.init_payload);
        if (bytes.size() > decl.bytes) schema_fail("host init hex bigger than buffer: " + name);
        host_write(id, 0, bytes);
      } else if (decl.init_kind == HostBufDecl::InitKind::File) {
        auto bytes_str = slurp(decl.init_payload);
        std::vector<std::uint8_t> bytes(bytes_str.begin(), bytes_str.end());
        if (bytes.size() > decl.bytes) schema_fail("host init file bigger than buffer: " + name);
        host_write(id, 0, bytes);
      }

      host_bufs.emplace(name, std::make_pair(id, decl.bytes));
    }

    std::unordered_map<std::string, std::pair<DevicePtr, std::uint64_t>> dev_bufs;
    for (const auto& [name, decl] : dev_buf_decls) {
      if (decl.bytes == 0) schema_fail("device buffer bytes must be >=1: " + name);
      auto ptr = device_malloc(decl.bytes, decl.align);
      dev_bufs.emplace(name, std::make_pair(ptr, decl.bytes));
    }

    std::map<std::string, StreamId> stream_ids;
    StreamId next_stream_id = 1;
    for (const auto& [name, _] : streams) {
      stream_ids.emplace(name, next_stream_id++);
    }

    std::map<std::string, EventId> event_ids;
    EventId next_event_id = 1;
    for (const auto& name : event_names) {
      event_ids.emplace(name, next_event_id++);
    }

    // --- Build queues ---
    std::unordered_map<StreamId, std::deque<Command>> queues;
    CmdId next_cmd_id = 1;
    std::uint64_t ts = 1;

    auto emit = [&](const std::string& action, StreamId sid, CmdId cid, std::optional<EventId> eid = std::nullopt) {
      obs_.emit(make_stream_event(ts++, action, sid, cid, eid));
    };

    for (const auto& [sname, sv] : streams) {
      auto sid_it = stream_ids.find(sname);
      if (sid_it == stream_ids.end()) ref_fail("stream not found: " + sname);
      StreamId sid = sid_it->second;

      const auto& so = sv.as_object();
      const auto& cmds = require_field(so, "commands").as_array();
      for (const auto& cmdv : cmds) {
        if (!cmdv.is_object()) schema_fail("command must be object");
        const auto& co = cmdv.as_object();
        if (co.size() != 1) schema_fail("command must be oneof {copy,kernel,event_record,event_wait,sync}");
        const auto& [ck, cv] = *co.begin();

        Command cmd;
        cmd.cmd_id = next_cmd_id++;
        cmd.stream_id = sid;

        if (ck == "copy") {
          cmd.kind = CommandKind::Copy;
          if (!cv.is_object()) schema_fail("copy payload must be object");
          const auto& po = cv.as_object();

          auto kind_s = require_string(require_field(po, "kind"), "copy.kind");
          BufRef dst = parse_buf_ref(require_field(po, "dst"), "copy.dst");
          BufRef src = parse_buf_ref(require_field(po, "src"), "copy.src");
          auto bytes = require_u64(require_field(po, "bytes"), "copy.bytes");

          CopyPayload cp;
          cp.bytes = bytes;

          if (kind_s == "H2D") {
            cp.kind = CopyKind::H2D;
            auto hs = host_bufs.find(src.name);
            if (hs == host_bufs.end()) ref_fail("host buffer not found: " + src.name);
            auto dd = dev_bufs.find(dst.name);
            if (dd == dev_bufs.end()) ref_fail("device buffer not found: " + dst.name);
            if (src.offset + bytes > hs->second.second) schema_fail("H2D src out of range: " + src.name);
            if (dst.offset + bytes > dd->second.second) schema_fail("H2D dst out of range: " + dst.name);

            cp.host = hs->second.first;
            cp.host_off = src.offset;
            cp.dev = dd->second.first;
            cp.dev_off = dst.offset;
          } else if (kind_s == "D2H") {
            cp.kind = CopyKind::D2H;
            auto dd = dev_bufs.find(src.name);
            if (dd == dev_bufs.end()) ref_fail("device buffer not found: " + src.name);
            auto hd = host_bufs.find(dst.name);
            if (hd == host_bufs.end()) ref_fail("host buffer not found: " + dst.name);
            if (src.offset + bytes > dd->second.second) schema_fail("D2H src out of range: " + src.name);
            if (dst.offset + bytes > hd->second.second) schema_fail("D2H dst out of range: " + dst.name);

            cp.host = hd->second.first;
            cp.host_off = dst.offset;
            cp.dev = dd->second.first;
            cp.dev_off = src.offset;
          } else {
            schema_fail("unsupported copy kind (v0 supports H2D/D2H): " + kind_s);
          }

          cmd.copy = cp;

        } else if (ck == "kernel") {
          cmd.kind = CommandKind::Kernel;
          if (!cv.is_object()) schema_fail("kernel payload must be object");
          const auto& ko = cv.as_object();

          auto module_name = require_string(require_field(ko, "module"), "kernel.module");
          auto entry = require_string(require_field(ko, "entry"), "kernel.entry");
          auto grid_dim = parse_dim3(require_field(ko, "grid_dim"), "kernel.grid_dim");
          auto block_dim = parse_dim3(require_field(ko, "block_dim"), "kernel.block_dim");

          auto mit = modules.find(module_name);
          if (mit == modules.end()) ref_fail("module not found: " + module_name);

          KernelPayload kp;
          kp.mod = mit->second;
          kp.module_name = module_name;
          kp.entry = entry;
          kp.launch.grid_dim = grid_dim;
          kp.launch.block_dim = block_dim;
          kp.launch.warp_size = cfg_.sim.warp_size;

          if (const auto* av = cv.get("args")) {
            if (!av->is_object()) schema_fail("kernel.args must be object");
            for (const auto& [aname, avv] : av->as_object()) {
              kp.args.emplace(aname, parse_arg_value(avv, "kernel.args"));
            }
          } else {
            schema_fail("kernel.args missing");
          }

          cmd.kernel = std::move(kp);

        } else if (ck == "event_record") {
          cmd.kind = CommandKind::EventRecord;
          if (!cv.is_object()) schema_fail("event_record payload must be object");
          const auto& eo = cv.as_object();
          auto ename = require_string(require_field(eo, "event"), "event_record.event");
          auto eit = event_ids.find(ename);
          if (eit == event_ids.end()) ref_fail("event not found: " + ename);
          cmd.event = EventPayload{ eit->second, ename };

        } else if (ck == "event_wait") {
          cmd.kind = CommandKind::EventWait;
          if (!cv.is_object()) schema_fail("event_wait payload must be object");
          const auto& eo = cv.as_object();
          auto ename = require_string(require_field(eo, "event"), "event_wait.event");
          auto eit = event_ids.find(ename);
          if (eit == event_ids.end()) ref_fail("event not found: " + ename);
          cmd.event = EventPayload{ eit->second, ename };

        } else if (ck == "sync") {
          cmd.kind = CommandKind::Sync;

        } else {
          schema_fail("unknown command kind: " + ck);
        }

        queues[sid].push_back(cmd);
        emit("cmd_enq", sid, cmd.cmd_id, cmd.event ? std::optional<EventId>(cmd.event->event_id) : std::nullopt);
      }
    }

    // --- Execute schedule ---
    std::unordered_map<EventId, bool> signaled;
    for (const auto& [_, id] : event_ids) signaled[id] = false;

    std::uint64_t total_steps = 0;

    while (true) {
      bool any_remaining = false;
      bool progressed = false;

      for (const auto& [sname, sid] : stream_ids) {
        auto& q = queues[sid];
        if (q.empty()) continue;
        any_remaining = true;

        auto& cmd = q.front();
        bool ready = true;
        if (cmd.kind == CommandKind::EventWait) {
          ready = signaled[cmd.event->event_id];
        }

        if (!ready) continue;

        emit("cmd_ready", sid, cmd.cmd_id, cmd.event ? std::optional<EventId>(cmd.event->event_id) : std::nullopt);
        emit("cmd_submit", sid, cmd.cmd_id, cmd.event ? std::optional<EventId>(cmd.event->event_id) : std::nullopt);

        if (cmd.kind == CommandKind::Copy) {
          auto cp = *cmd.copy;
          if (cp.kind == CopyKind::H2D) {
            memcpy_h2d(cp.dev + cp.dev_off, cp.host, cp.host_off, cp.bytes);
          } else if (cp.kind == CopyKind::D2H) {
            memcpy_d2h(cp.host, cp.host_off, cp.dev + cp.dev_off, cp.bytes);
          }

        } else if (cmd.kind == CommandKind::Kernel) {
          const auto& kp = *cmd.kernel;
          KernelArgs ka = pack_kernel_args(*this, kp.mod, kp.entry, kp.args, dev_bufs);
          auto r = run_ptx_kernel_with_args_entry_launch(kp.mod.ptx, kp.mod.ptx_isa, kp.mod.inst_desc, kp.entry, ka, kp.launch);
          total_steps += r.sim.steps;
          if (r.sim.diag) {
            out.sim = r.sim;
            return out;
          }
          if (!r.sim.completed) {
            out.sim = r.sim;
            out.sim.steps = total_steps;
            return out;
          }

        } else if (cmd.kind == CommandKind::EventRecord) {
          signaled[cmd.event->event_id] = true;
          obs_.emit(make_stream_event(ts++, "event_record", sid, cmd.cmd_id, cmd.event->event_id));

        } else if (cmd.kind == CommandKind::EventWait) {
          obs_.emit(make_stream_event(ts++, "event_wait_done", sid, cmd.cmd_id, cmd.event->event_id));

        } else if (cmd.kind == CommandKind::Sync) {
          // No-op barrier in this synchronous runtime.
        }

        emit("cmd_complete", sid, cmd.cmd_id, cmd.event ? std::optional<EventId>(cmd.event->event_id) : std::nullopt);
        q.pop_front();
        progressed = true;
      }

      if (!any_remaining) break;
      if (!progressed) {
        out.sim.diag = Diagnostic{ "runtime", "E_WORKLOAD_DEADLOCK", "no commands ready; possible missing event_record", std::nullopt,
                                  std::nullopt, std::nullopt };
        out.sim.completed = false;
        out.sim.steps = total_steps;
        return out;
      }
    }

    out.sim.completed = true;
    out.sim.steps = total_steps;
    return out;

  } catch (const SchemaError& se) {
    out.sim.diag = se.diag;
    out.sim.completed = false;
    out.sim.steps = 0;
    return out;
  } catch (const json::ParseError& pe) {
    out.sim.diag = Diagnostic{ "runtime", "E_WORKLOAD_SCHEMA", pe.what(), std::nullopt, std::nullopt, std::nullopt };
    out.sim.completed = false;
    out.sim.steps = 0;
    return out;
  } catch (const std::exception& e) {
    out.sim.diag = Diagnostic{ "runtime", "E_WORKLOAD_FATAL", e.what(), std::nullopt, std::nullopt, std::nullopt };
    out.sim.completed = false;
    out.sim.steps = 0;
    return out;
  }
}

} // namespace gpusim
