#include "protogpu/broker_blocking_executor.h"

#include "gpusim/observability.h"
#include "gpusim/runtime.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace protogpu {
namespace {

struct DeviceBufferState final {
  gpusim::DevicePtr device_ptr = 0;
  std::size_t bytes = 0;
  std::uint32_t flags = 0;
  MappedBuffer* mapped = nullptr;
};

BrokerExecutionResult fail(protogpu_status_code code, const std::string& message) {
  BrokerExecutionResult out;
  out.status = code;
  out.diagnostic = message;
  return out;
}

std::string format_diag(const gpusim::Diagnostic& diag) {
  std::ostringstream oss;
  oss << diag.module << ":" << diag.code << ": " << diag.message;
  if (diag.function) {
    oss << " [function=" << *diag.function << "]";
  }
  return oss.str();
}

bool checked_add(std::uint64_t a, std::uint64_t b, std::uint64_t& out) {
  if (a > UINT64_MAX - b) return false;
  out = a + b;
  return true;
}

bool blob_in_bounds(const protogpu_blob_ref& ref, const std::vector<std::uint8_t>& arena) {
  std::uint64_t end = 0;
  return checked_add(ref.offset, ref.size, end) && end <= arena.size();
}

bool records_in_bounds(const protogpu_record_ref& ref, std::size_t elem_size, const std::vector<std::uint8_t>& arena) {
  std::uint64_t bytes = 0;
  if (!checked_add(0, static_cast<std::uint64_t>(ref.count) * static_cast<std::uint64_t>(elem_size), bytes)) return false;
  return blob_in_bounds(protogpu_blob_ref{ ref.offset, bytes }, arena);
}

std::string_view blob_view(const protogpu_blob_ref& ref, const std::vector<std::uint8_t>& arena) {
  return std::string_view(reinterpret_cast<const char*>(arena.data() + ref.offset), static_cast<std::size_t>(ref.size));
}

template <typename T>
std::vector<T> read_records(const protogpu_record_ref& ref, const std::vector<std::uint8_t>& arena) {
  if (!records_in_bounds(ref, sizeof(T), arena)) {
    throw std::runtime_error("record span out of bounds");
  }
  std::vector<T> out(ref.count);
  if (!out.empty()) {
    std::memcpy(out.data(), arena.data() + ref.offset, out.size() * sizeof(T));
  }
  return out;
}

void write_le(std::vector<std::uint8_t>& blob, std::uint32_t off, std::uint64_t value, std::uint32_t size) {
  if (static_cast<std::uint64_t>(off) + size > blob.size()) {
    throw std::runtime_error("kernel arg blob write out of range");
  }
  for (std::uint32_t i = 0; i < size; i++) {
    blob[static_cast<std::size_t>(off + i)] = static_cast<std::uint8_t>((value >> (8u * i)) & 0xFFu);
  }
}

gpusim::KernelArgs pack_kernel_args(const std::string& ptx_text,
                                    const std::string& entry,
                                    const std::vector<protogpu_arg_desc>& arg_descs,
                                    const std::vector<std::uint8_t>& arena,
                                    const std::unordered_map<std::uint64_t, DeviceBufferState>& buffers) {
  gpusim::Parser parser;
  auto mod_tokens = parser.parse_ptx_text_tokens(ptx_text, "<protogpu-job>");

  gpusim::Binder binder;
  gpusim::KernelTokens kernel_tokens = binder.bind_kernel_by_name(mod_tokens, entry);

  if (arg_descs.size() != kernel_tokens.params.size()) {
    throw std::runtime_error("kernel arg count mismatch");
  }

  gpusim::KernelArgs args;
  args.layout = kernel_tokens.params;

  std::uint32_t blob_bytes = 0;
  for (const auto& param : args.layout) {
    blob_bytes = std::max(blob_bytes, static_cast<std::uint32_t>(param.offset + param.size));
  }
  args.blob.assign(static_cast<std::size_t>(blob_bytes), 0);

  for (std::size_t i = 0; i < args.layout.size(); i++) {
    const auto& param = args.layout[i];
    const auto& arg = arg_descs[i];

    switch (arg.kind) {
      case PROTOGPU_ARG_U32:
        if (param.type != gpusim::ParamType::U32 || param.size != 4) {
          throw std::runtime_error("u32 arg type mismatch at index " + std::to_string(i));
        }
        write_le(args.blob, param.offset, arg.value, 4);
        break;
      case PROTOGPU_ARG_U64:
        if (param.type != gpusim::ParamType::U64 || param.size != 8) {
          throw std::runtime_error("u64 arg type mismatch at index " + std::to_string(i));
        }
        write_le(args.blob, param.offset, arg.value, 8);
        break;
      case PROTOGPU_ARG_BUFFER_HANDLE: {
        if (param.type != gpusim::ParamType::U64 || param.size != 8) {
          throw std::runtime_error("buffer arg requires u64 param at index " + std::to_string(i));
        }
        auto it = buffers.find(arg.value);
        if (it == buffers.end()) {
          throw std::runtime_error("buffer handle not bound: " + std::to_string(arg.value));
        }
        write_le(args.blob, param.offset, it->second.device_ptr, 8);
        break;
      }
      case PROTOGPU_ARG_BYTES: {
        if (!blob_in_bounds(arg.bytes, arena)) {
          throw std::runtime_error("raw bytes arg out of bounds");
        }
        if (arg.bytes.size != param.size) {
          throw std::runtime_error("raw bytes arg size mismatch at index " + std::to_string(i));
        }
        std::memcpy(args.blob.data() + param.offset, arena.data() + arg.bytes.offset, static_cast<std::size_t>(arg.bytes.size));
        break;
      }
      default:
        throw std::runtime_error("unknown arg kind: " + std::to_string(arg.kind));
    }
  }

  return args;
}

std::string build_trace_jsonl(const gpusim::AppConfig& cfg, const gpusim::Runtime& rt) {
  auto events = rt.obs().trace_snapshot();
  if (events.empty()) return {};

  gpusim::TraceHeader header;
  header.profile = cfg.sim.profile.empty() ? "protogpu_virtual_hw" : cfg.sim.profile;
  header.deterministic = cfg.sim.deterministic;

  std::string out = gpusim::trace_header_to_json_line(header);
  for (const auto& event : events) {
    out += gpusim::event_to_json_line(event);
  }
  return out;
}

std::string build_stats_json(const gpusim::AppConfig& cfg, const gpusim::Runtime& rt) {
  gpusim::StatsMeta meta;
  meta.profile = cfg.sim.profile.empty() ? "protogpu_virtual_hw" : cfg.sim.profile;
  meta.deterministic = cfg.sim.deterministic;
  return gpusim::stats_to_json(rt.obs().counters_snapshot(), meta);
}

}  // namespace

BrokerExecutionResult execute_blocking_job(const protogpu_submit_job& job,
                                           const std::vector<std::uint8_t>& arena,
                                           std::vector<MappedBuffer>& mapped_buffers) {
  try {
    if (job.uapi_version != PROTOGPU_UAPI_VERSION_V1) {
      return fail(PROTOGPU_STATUS_INVALID_JOB, "unsupported UAPI version");
    }

    const auto required = {
      job.module.ptx_text,
      job.module.ptx_isa_json,
      job.module.inst_desc_json,
      job.module.config_json,
      job.module.entry_name,
    };
    for (const auto& ref : required) {
      if (!blob_in_bounds(ref, arena)) {
        return fail(PROTOGPU_STATUS_INVALID_JOB, "module blob out of bounds");
      }
    }
    if (!records_in_bounds(job.launch.args, sizeof(protogpu_arg_desc), arena)) {
      return fail(PROTOGPU_STATUS_INVALID_JOB, "arg descriptor span out of bounds");
    }
    if (!records_in_bounds(job.buffer_bindings, sizeof(protogpu_buffer_binding), arena)) {
      return fail(PROTOGPU_STATUS_INVALID_JOB, "buffer binding span out of bounds");
    }

    const std::string ptx_text(blob_view(job.module.ptx_text, arena));
    const std::string ptx_isa_json(blob_view(job.module.ptx_isa_json, arena));
    const std::string inst_desc_json(blob_view(job.module.inst_desc_json, arena));
    const std::string config_json(blob_view(job.module.config_json, arena));
    const std::string entry(blob_view(job.module.entry_name, arena));
    const auto arg_descs = read_records<protogpu_arg_desc>(job.launch.args, arena);
    const auto bindings = read_records<protogpu_buffer_binding>(job.buffer_bindings, arena);

    auto cfg = gpusim::load_app_config_json_text(config_json);
    gpusim::Runtime rt(cfg);

    std::unordered_map<std::uint64_t, DeviceBufferState> device_buffers;
    for (const auto& binding : bindings) {
      auto it = std::find_if(mapped_buffers.begin(), mapped_buffers.end(), [&](const MappedBuffer& buffer) {
        return buffer.handle == binding.handle;
      });
      if (it == mapped_buffers.end()) {
        return fail(PROTOGPU_STATUS_INVALID_BUFFER, "mapped buffer not found for handle " + std::to_string(binding.handle));
      }
      if (!device_buffers.emplace(binding.handle, DeviceBufferState{}).second) {
        return fail(PROTOGPU_STATUS_INVALID_BUFFER, "duplicate buffer binding for handle " + std::to_string(binding.handle));
      }

      auto& state = device_buffers[binding.handle];
      state.flags = binding.flags;
      state.bytes = it->bytes.size();
      state.mapped = &(*it);
      state.device_ptr = rt.device_malloc(static_cast<std::uint64_t>(state.bytes), 16);

      if ((binding.flags & PROTOGPU_BUFFER_INPUT) != 0u && !it->bytes.empty()) {
        auto host = rt.host_alloc(static_cast<std::uint64_t>(it->bytes.size()));
        rt.host_write(host, 0, it->bytes);
        rt.memcpy_h2d(state.device_ptr, host, 0, static_cast<std::uint64_t>(it->bytes.size()));
      }
    }

    gpusim::KernelArgs kernel_args = pack_kernel_args(ptx_text, entry, arg_descs, arena, device_buffers);

    gpusim::LaunchConfig launch;
    launch.grid_dim = gpusim::Dim3{ job.launch.grid_dim.x, job.launch.grid_dim.y, job.launch.grid_dim.z };
    launch.block_dim = gpusim::Dim3{ job.launch.block_dim.x, job.launch.block_dim.y, job.launch.block_dim.z };
    launch.warp_size = cfg.sim.warp_size;

    auto run = rt.run_ptx_kernel_with_args_text_entry_launch(ptx_text, ptx_isa_json, inst_desc_json, entry, kernel_args, launch);

    BrokerExecutionResult out;
    out.steps = run.sim.steps;
    out.trace_jsonl = build_trace_jsonl(cfg, rt);
    out.stats_json = build_stats_json(cfg, rt);

    if (run.sim.diag) {
      out.status = PROTOGPU_STATUS_SIMULATION_ERROR;
      out.diagnostic = format_diag(*run.sim.diag);
      return out;
    }
    if (!run.sim.completed) {
      out.status = PROTOGPU_STATUS_RUNTIME_ERROR;
      out.diagnostic = "simulation did not complete";
      return out;
    }

    for (auto& [_, state] : device_buffers) {
      if ((state.flags & PROTOGPU_BUFFER_OUTPUT) == 0u || state.bytes == 0) continue;
      auto host = rt.host_alloc(static_cast<std::uint64_t>(state.bytes));
      rt.memcpy_d2h(host, 0, state.device_ptr, static_cast<std::uint64_t>(state.bytes));
      auto bytes = rt.host_read(host, 0, static_cast<std::uint64_t>(state.bytes));
      if (!bytes) {
        return fail(PROTOGPU_STATUS_RUNTIME_ERROR, "failed to read output buffer from ProtoGPU runtime");
      }
      state.mapped->bytes = *bytes;
    }

    out.status = PROTOGPU_STATUS_OK;
    return out;
  } catch (const std::exception& ex) {
    return fail(PROTOGPU_STATUS_RUNTIME_ERROR, ex.what());
  }
}

}  // namespace protogpu
