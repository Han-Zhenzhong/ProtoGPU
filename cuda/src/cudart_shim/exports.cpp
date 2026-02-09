#include "cudart_shim/export_macros.h"

#include "cudart_shim/assets_provider.h"
#include "cudart_shim/cuda_abi_min.h"
#include "cudart_shim/device_memory.h"
#include "cudart_shim/error_state.h"
#include "cudart_shim/fatbin_registry.h"
#include "cudart_shim/kernel_registry.h"
#include "cudart_shim/kernel_args_pack.h"
#include "cudart_shim/launch_config_stack.h"
#include "cudart_shim/ptx_entry_validate.h"
#include "cudart_shim/runtime_context.h"
#include "cudart_shim/stream_scheduler.h"

#include "gpusim/frontend.h"


#include <cstdio>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <new>
#include <string>
#include <vector>

namespace gpusim_cudart_shim {

static DeviceMemory& device_memory_singleton() {
  static DeviceMemory m;
  return m;
}

static FatbinRegistry& fatbin_registry_singleton() {
  static FatbinRegistry r;
  return r;
}

static KernelRegistry& kernel_registry_singleton() {
  static KernelRegistry r;
  return r;
}

static StreamScheduler& stream_scheduler_singleton() {
  static StreamScheduler s;
  return s;
}

static void maybe_log(const char* msg) {
  const char* v = std::getenv("GPUSIM_CUDART_SHIM_LOG");
  if (v && *v != '\0') {
    std::fprintf(stderr, "[gpu-sim-cudart-shim] %s\n", msg);
  }
}

static bool runtime_log_enabled() {
  if (const char* v = std::getenv("GPUSIM_CUDART_SHIM_LOG_RUNTIME")) {
    return v && *v != '\0';
  }
  return false;
}

static std::string format_diag(const gpusim::Diagnostic& d) {
  std::string s;
  s.reserve(256);
  s += d.module;
  s += "/";
  s += d.code;
  s += ": ";
  s += d.message;
  if (d.function.has_value()) {
    s += " (func=";
    s += *d.function;
    s += ")";
  }
  if (d.inst_index.has_value()) {
    s += " (pc=";
    s += std::to_string(*d.inst_index);
    s += ")";
  }
  if (d.location.has_value()) {
    s += " (";
    s += d.location->file;
    s += ":";
    s += std::to_string(d.location->line);
    s += ")";
  }
  return s;
}

static cudaError_t fail(cudaError_t err, const std::string& msg) {
  ErrorState::tls().set(err, msg);
  maybe_log(msg.c_str());
  return err;
}

static bool ensure_init_or_fail(std::string& out_err) {
  return RuntimeContext::instance().ensure_initialized(out_err);
}

} // namespace gpusim_cudart_shim

// --- Exported CUDA Runtime API (subset) ---

GPUSIM_CUDART_EXPORT gpusim_cudart_shim::cudaError_t cudaGetLastError() {
  return gpusim_cudart_shim::ErrorState::tls().get_and_clear();
}

GPUSIM_CUDART_EXPORT const char* cudaGetErrorString(gpusim_cudart_shim::cudaError_t err) {
  using namespace gpusim_cudart_shim;
  switch (err) {
  case cudaSuccess: return "cudaSuccess";
  case cudaErrorInvalidValue: return "cudaErrorInvalidValue";
  case cudaErrorMemoryAllocation: return "cudaErrorMemoryAllocation";
  case cudaErrorInitializationError: return "cudaErrorInitializationError";
  case cudaErrorInvalidDeviceFunction: return "cudaErrorInvalidDeviceFunction";
  case cudaErrorInvalidDevicePointer: return "cudaErrorInvalidDevicePointer";
  case cudaErrorInvalidMemcpyDirection: return "cudaErrorInvalidMemcpyDirection";
  case cudaErrorInvalidPtx: return "cudaErrorInvalidPtx";
  case cudaErrorNotSupported: return "cudaErrorNotSupported";
  case cudaErrorUnknown: return "cudaErrorUnknown";
  default: return "cudaError(unknown)";
  }
}

GPUSIM_CUDART_EXPORT gpusim_cudart_shim::cudaError_t cudaDeviceSynchronize() {
  // MVP: all work is synchronous, but still honor fail-fast init behavior.
  std::string init_err;
  if (!gpusim_cudart_shim::RuntimeContext::instance().ensure_initialized(init_err)) {
    return gpusim_cudart_shim::fail(gpusim_cudart_shim::cudaErrorInitializationError, init_err);
  }
  // MVP: execute synchronously, but keep the API for correctness.
  gpusim_cudart_shim::stream_scheduler_singleton().drain_all();
  gpusim_cudart_shim::ErrorState::tls().set(gpusim_cudart_shim::cudaSuccess);
  return gpusim_cudart_shim::cudaSuccess;
}

GPUSIM_CUDART_EXPORT gpusim_cudart_shim::cudaError_t cudaMalloc(void** devPtr, std::size_t size) {
  using namespace gpusim_cudart_shim;
  try {
    std::string init_err;
    if (!ensure_init_or_fail(init_err)) {
      return fail(cudaErrorInitializationError, init_err);
    }

    auto& ctx = RuntimeContext::instance();
    std::scoped_lock lk(ctx.global_mutex());
    auto rc = device_memory_singleton().malloc(ctx.runtime(), devPtr, size);
    if (rc == cudaSuccess) ErrorState::tls().set(cudaSuccess);
    return rc;
  } catch (const std::exception& ex) {
    return gpusim_cudart_shim::fail(gpusim_cudart_shim::cudaErrorUnknown, ex.what());
  } catch (...) {
    return gpusim_cudart_shim::fail(gpusim_cudart_shim::cudaErrorUnknown, "cudaMalloc: unknown exception");
  }
}

GPUSIM_CUDART_EXPORT gpusim_cudart_shim::cudaError_t cudaFree(void* devPtr) {
  using namespace gpusim_cudart_shim;
  try {
    std::string init_err;
    if (!ensure_init_or_fail(init_err)) {
      return fail(cudaErrorInitializationError, init_err);
    }

    auto& ctx = RuntimeContext::instance();
    std::scoped_lock lk(ctx.global_mutex());
    auto rc = device_memory_singleton().free(devPtr);
    if (rc == cudaSuccess) ErrorState::tls().set(cudaSuccess);
    return rc;
  } catch (const std::exception& ex) {
    return fail(cudaErrorUnknown, ex.what());
  } catch (...) {
    return fail(cudaErrorUnknown, "cudaFree: unknown exception");
  }
}

GPUSIM_CUDART_EXPORT gpusim_cudart_shim::cudaError_t cudaMemcpy(void* dst,
                                                              const void* src,
                                                              std::size_t count,
                                                              gpusim_cudart_shim::cudaMemcpyKind kind) {
  using namespace gpusim_cudart_shim;
  try {
    std::string init_err;
    if (!ensure_init_or_fail(init_err)) {
      return fail(cudaErrorInitializationError, init_err);
    }

    auto& ctx = RuntimeContext::instance();
    std::scoped_lock lk(ctx.global_mutex());
    auto rc = device_memory_singleton().memcpy(ctx.runtime(), dst, src, count, kind);
    if (rc == cudaSuccess) ErrorState::tls().set(cudaSuccess);
    return rc;
  } catch (const std::exception& ex) {
    return fail(cudaErrorUnknown, ex.what());
  } catch (...) {
    return fail(cudaErrorUnknown, "cudaMemcpy: unknown exception");
  }
}

// --- Streams (MVP: supported API surface, synchronous execution) ---

namespace gpusim_cudart_shim {
namespace {
struct StreamHandle final {
  std::uint64_t magic = 0x5354524D5F475055ull; // "STRM_GPU" (debug aid)
};

static StreamHandle* as_stream_handle(cudaStream_t s) {
  return static_cast<StreamHandle*>(s);
}
} // namespace
} // namespace gpusim_cudart_shim

GPUSIM_CUDART_EXPORT gpusim_cudart_shim::cudaError_t cudaStreamCreate(gpusim_cudart_shim::cudaStream_t* pStream) {
  using namespace gpusim_cudart_shim;
  try {
    if (!pStream) return fail(cudaErrorInvalidValue, "cudaStreamCreate: pStream is null");

    std::string init_err;
    if (!ensure_init_or_fail(init_err)) {
      return fail(cudaErrorInitializationError, init_err);
    }

    auto* h = new (std::nothrow) StreamHandle();
    if (!h) return fail(cudaErrorMemoryAllocation, "cudaStreamCreate: allocation failed");

    *pStream = static_cast<cudaStream_t>(h);
    ErrorState::tls().set(cudaSuccess);
    return cudaSuccess;
  } catch (const std::exception& ex) {
    return fail(cudaErrorUnknown, ex.what());
  } catch (...) {
    return fail(cudaErrorUnknown, "cudaStreamCreate: unknown exception");
  }
}

GPUSIM_CUDART_EXPORT gpusim_cudart_shim::cudaError_t cudaStreamDestroy(gpusim_cudart_shim::cudaStream_t stream) {
  using namespace gpusim_cudart_shim;
  try {
    if (!stream) return fail(cudaErrorInvalidValue, "cudaStreamDestroy: cannot destroy default/null stream");
    auto* h = as_stream_handle(stream);
    delete h;
    ErrorState::tls().set(cudaSuccess);
    return cudaSuccess;
  } catch (const std::exception& ex) {
    return fail(cudaErrorUnknown, ex.what());
  } catch (...) {
    return fail(cudaErrorUnknown, "cudaStreamDestroy: unknown exception");
  }
}

GPUSIM_CUDART_EXPORT gpusim_cudart_shim::cudaError_t cudaStreamSynchronize(gpusim_cudart_shim::cudaStream_t /*stream*/) {
  using namespace gpusim_cudart_shim;
  // MVP: work is executed synchronously, but honor fail-fast init behavior.
  std::string init_err;
  if (!RuntimeContext::instance().ensure_initialized(init_err)) {
    return fail(cudaErrorInitializationError, init_err);
  }
  stream_scheduler_singleton().drain_all();
  ErrorState::tls().set(cudaSuccess);
  return cudaSuccess;
}

GPUSIM_CUDART_EXPORT gpusim_cudart_shim::cudaError_t cudaStreamQuery(gpusim_cudart_shim::cudaStream_t /*stream*/) {
  using namespace gpusim_cudart_shim;
  // MVP: always ready.
  std::string init_err;
  if (!RuntimeContext::instance().ensure_initialized(init_err)) {
    return fail(cudaErrorInitializationError, init_err);
  }
  ErrorState::tls().set(cudaSuccess);
  return cudaSuccess;
}

GPUSIM_CUDART_EXPORT gpusim_cudart_shim::cudaError_t cudaMemcpyAsync(void* dst,
                                                                   const void* src,
                                                                   std::size_t count,
                                                                   gpusim_cudart_shim::cudaMemcpyKind kind,
                                                                   gpusim_cudart_shim::cudaStream_t stream) {
  using namespace gpusim_cudart_shim;
  try {
    std::string init_err;
    if (!ensure_init_or_fail(init_err)) {
      return fail(cudaErrorInitializationError, init_err);
    }

    auto& ctx = RuntimeContext::instance();
    cudaError_t rc = cudaSuccess;

    // Copy arguments to keep command self-contained.
    void* dst_copy = dst;
    const void* src_copy = src;
    std::size_t count_copy = count;
    cudaMemcpyKind kind_copy = kind;

    stream_scheduler_singleton().submit(stream, [&]() {
      std::scoped_lock lk(ctx.global_mutex());
      rc = device_memory_singleton().memcpy(ctx.runtime(), dst_copy, src_copy, count_copy, kind_copy);
    });

    if (rc == cudaSuccess) ErrorState::tls().set(cudaSuccess);
    return rc;
  } catch (const std::exception& ex) {
    return fail(cudaErrorUnknown, ex.what());
  } catch (...) {
    return fail(cudaErrorUnknown, "cudaMemcpyAsync: unknown exception");
  }
}

GPUSIM_CUDART_EXPORT gpusim_cudart_shim::cudaError_t cudaLaunchKernel(const void* func,
                                                                     gpusim_cudart_shim::dim3 gridDim,
                                                                     gpusim_cudart_shim::dim3 blockDim,
                                                                     void** args,
                                                                     std::size_t sharedMem,
                                                                     gpusim_cudart_shim::cudaStream_t stream) {
  using namespace gpusim_cudart_shim;
  try {
    maybe_log("cudaLaunchKernel");
    if (const char* v = std::getenv("GPUSIM_CUDART_SHIM_LOG_LAUNCH")) {
      if (v && *v != '\0') {
        std::fprintf(stderr,
                     "[gpu-sim-cudart-shim] cudaLaunchKernel(func=%p grid=(%u,%u,%u) block=(%u,%u,%u) args=%p shared=%zu stream=%p)\n",
                     func,
                     static_cast<unsigned>(gridDim.x), static_cast<unsigned>(gridDim.y), static_cast<unsigned>(gridDim.z),
                     static_cast<unsigned>(blockDim.x), static_cast<unsigned>(blockDim.y), static_cast<unsigned>(blockDim.z),
                     static_cast<void*>(args),
                     sharedMem,
                     static_cast<void*>(stream));
      }
    }

    std::string init_err;
    if (!ensure_init_or_fail(init_err)) {
      return fail(cudaErrorInitializationError, init_err);
    }

    auto& ctx = RuntimeContext::instance();
    std::scoped_lock lk(ctx.global_mutex());

    // Some toolchains use push/pop config around launches.
    if (gridDim.x == 0 || blockDim.x == 0) {
      auto popped = LaunchConfigStack::tls().pop();
      if (popped) {
        gridDim = popped->grid;
        blockDim = popped->block;
        sharedMem = popped->shared;
        stream = popped->stream;
      }
    }

    if (!func) return fail(cudaErrorInvalidDeviceFunction, "cudaLaunchKernel: func is null");
    if (gridDim.x == 0 || gridDim.y == 0 || gridDim.z == 0) {
      return fail(cudaErrorInvalidValue, "cudaLaunchKernel: invalid gridDim");
    }
    if (blockDim.x == 0 || blockDim.y == 0 || blockDim.z == 0) {
      return fail(cudaErrorInvalidValue, "cudaLaunchKernel: invalid blockDim");
    }

    const auto* ki = kernel_registry_singleton().lookup(func);
    if (!ki) {
      return fail(cudaErrorInvalidDeviceFunction, "cudaLaunchKernel: kernel not registered");
    }
    if (ki->module_id == 0) {
      return fail(cudaErrorInvalidDeviceFunction, "cudaLaunchKernel: kernel module unknown");
    }

    const auto* mod = fatbin_registry_singleton().lookup(ki->module_id);
    if (!mod || mod->ptx_texts.empty()) {
      return fail(cudaErrorInvalidDeviceFunction, "cudaLaunchKernel: no PTX extracted for module");
    }

    const std::string entry = ki->device_name;
    const std::string* chosen_ptx = nullptr;
    for (const auto& ptx : mod->ptx_texts) {
      if (ptx_contains_entry_mvp(ptx, entry)) {
        chosen_ptx = &ptx;
        break;
      }
    }
    if (!chosen_ptx) {
      std::string diag = "cudaLaunchKernel: .entry '" + entry + "' not found in extracted PTX";
      diag += " (ptx_count=" + std::to_string(mod->ptx_texts.size()) + ")";

      // Best-effort diagnostics: list a few entry names we did find.
      std::vector<std::string> found;
      for (const auto& ptx : mod->ptx_texts) {
        auto names = ptx_list_entry_names_mvp(ptx, 8);
        found.insert(found.end(), names.begin(), names.end());
        if (found.size() >= 16) break;
      }
      if (!found.empty()) {
        diag += "; found entries:";
        for (std::size_t i = 0; i < found.size() && i < 16; i++) {
          diag += (i == 0 ? " " : ", ");
          diag += found[i];
        }
      }

      return fail(cudaErrorInvalidDeviceFunction, diag);
    }

    // Parse PTX tokens and bind kernel to obtain param layout.
    gpusim::Parser parser;
    gpusim::Binder binder;
    auto mod_tokens = parser.parse_ptx_text_tokens(*chosen_ptx, "<fatbin>");
    auto kernel_tokens = binder.bind_kernel_by_name(mod_tokens, entry);

    gpusim::KernelArgs kargs;
    std::string pack_err;
    auto pack_rc = pack_kernel_args_mvp(kernel_tokens.params, args, device_memory_singleton(), kargs, pack_err);
    if (pack_rc != cudaSuccess) {
      return fail(pack_rc, std::string("cudaLaunchKernel: arg pack failed: ") + pack_err);
    }

    gpusim::LaunchConfig launch;
    launch.grid_dim = gpusim::Dim3{ gridDim.x, gridDim.y, gridDim.z };
    launch.block_dim = gpusim::Dim3{ blockDim.x, blockDim.y, blockDim.z };
    launch.warp_size = ctx.warp_size();

    if (sharedMem != 0) {
      maybe_log("cudaLaunchKernel: sharedMemBytes != 0 (MVP ignores shared memory size)");
    }

    const auto& assets = ctx.assets();

    std::optional<gpusim::RunOutputs> run_out;
    stream_scheduler_singleton().submit(stream, [&]() {
      run_out = ctx.runtime().run_ptx_kernel_with_args_text_entry_launch(
        *chosen_ptx, assets.ptx_isa_json, assets.inst_desc_json, entry, kargs, launch);
    });

    if (run_out.has_value()) {
      const auto& sim = run_out->sim;
      if (sim.diag.has_value()) {
        const auto msg = format_diag(*sim.diag);
        if (runtime_log_enabled()) {
          std::fprintf(stderr, "[gpu-sim-cudart-shim] runtime diag: %s\n", msg.c_str());
        }
        return fail(cudaErrorUnknown, std::string("cudaLaunchKernel: simulator error: ") + msg);
      }
      if (!sim.completed) {
        if (runtime_log_enabled()) {
          std::fprintf(stderr, "[gpu-sim-cudart-shim] runtime: not completed (steps=%llu)\n",
                       static_cast<unsigned long long>(sim.steps));
        }
        return fail(cudaErrorUnknown, "cudaLaunchKernel: simulator did not complete");
      }
    }

    ErrorState::tls().set(cudaSuccess);
    return cudaSuccess;
  } catch (const std::exception& ex) {
    return fail(cudaErrorUnknown, ex.what());
  } catch (...) {
    return fail(cudaErrorUnknown, "cudaLaunchKernel: unknown exception");
  }
}

// --- CUDA registration API (toolchain-dependent ABI; MVP stubs) ---

GPUSIM_CUDART_EXPORT void** __cudaRegisterFatBinary(void* fatCubin) {
  using namespace gpusim_cudart_shim;
  maybe_log("__cudaRegisterFatBinary");
  // No runtime init required; registration can happen before any API calls.
  return fatbin_registry_singleton().register_fatbin(fatCubin);
}

GPUSIM_CUDART_EXPORT void __cudaRegisterFatBinaryEnd(void** /*handle*/) {
  // Some toolchains call this as a marker; keep as no-op.
  gpusim_cudart_shim::maybe_log("__cudaRegisterFatBinaryEnd");
}

GPUSIM_CUDART_EXPORT void __cudaUnregisterFatBinary(void** handle) {
  using namespace gpusim_cudart_shim;
  maybe_log("__cudaUnregisterFatBinary");

  if (const auto* mod = fatbin_registry_singleton().lookup_by_handle(handle)) {
    kernel_registry_singleton().unregister_module(mod->id);
  }
  fatbin_registry_singleton().unregister_fatbin(handle);
}

GPUSIM_CUDART_EXPORT void __cudaRegisterFunction(void** fatCubinHandle,
                                                const void* hostFun,
                                                char* /*deviceFun*/,
                                                const char* deviceName,
                                                int /*thread_limit*/,
                                                void* /*tid*/,
                                                void* /*bid*/,
                                                void* /*bDim*/,
                                                void* /*gDim*/,
                                                int* /*wSize*/) {
  using namespace gpusim_cudart_shim;
  maybe_log("__cudaRegisterFunction");
  if (const char* v = std::getenv("GPUSIM_CUDART_SHIM_LOG_LAUNCH")) {
    if (v && *v != '\0') {
      std::fprintf(stderr,
                   "[gpu-sim-cudart-shim] __cudaRegisterFunction(fatCubinHandle=%p hostFun=%p deviceName=%s)\n",
                   static_cast<void*>(fatCubinHandle),
                   hostFun,
                   deviceName ? deviceName : "<null>");
    }
  }
  ModuleId module_id = 0;
  if (const auto* mod = fatbin_registry_singleton().lookup_by_handle(fatCubinHandle)) {
    module_id = mod->id;
  }
  kernel_registry_singleton().register_function(module_id, hostFun, deviceName);
}

GPUSIM_CUDART_EXPORT int __cudaPushCallConfiguration(gpusim_cudart_shim::dim3 gridDim,
                                                    gpusim_cudart_shim::dim3 blockDim,
                                                    std::size_t sharedMem,
                                                    gpusim_cudart_shim::cudaStream_t stream) {
  using namespace gpusim_cudart_shim;
  if (const char* v = std::getenv("GPUSIM_CUDART_SHIM_LOG_LAUNCH")) {
    if (v && *v != '\0') {
      std::fprintf(stderr,
                   "[gpu-sim-cudart-shim] __cudaPushCallConfiguration(grid=(%u,%u,%u) block=(%u,%u,%u) shared=%zu stream=%p)\n",
                   static_cast<unsigned>(gridDim.x), static_cast<unsigned>(gridDim.y), static_cast<unsigned>(gridDim.z),
                   static_cast<unsigned>(blockDim.x), static_cast<unsigned>(blockDim.y), static_cast<unsigned>(blockDim.z),
                   sharedMem,
                   static_cast<void*>(stream));
    }
  }
  CallConfig c;
  c.grid = gridDim;
  c.block = blockDim;
  c.shared = sharedMem;
  c.stream = stream;
  LaunchConfigStack::tls().push(c);
  return 0;
}

GPUSIM_CUDART_EXPORT int __cudaPopCallConfiguration(gpusim_cudart_shim::dim3* gridDim,
                                                   gpusim_cudart_shim::dim3* blockDim,
                                                   std::size_t* sharedMem,
                                                   gpusim_cudart_shim::cudaStream_t* stream) {
  using namespace gpusim_cudart_shim;
  if (const char* v = std::getenv("GPUSIM_CUDART_SHIM_LOG_LAUNCH")) {
    if (v && *v != '\0') {
      std::fprintf(stderr,
                   "[gpu-sim-cudart-shim] __cudaPopCallConfiguration(grid*=%p block*=%p shared*=%p stream*=%p)\n",
                   static_cast<void*>(gridDim),
                   static_cast<void*>(blockDim),
                   static_cast<void*>(sharedMem),
                   static_cast<void*>(stream));
    }
  }
  auto c = LaunchConfigStack::tls().pop();
  if (!c) return 1;
  if (gridDim) *gridDim = c->grid;
  if (blockDim) *blockDim = c->block;
  if (sharedMem) *sharedMem = c->shared;
  if (stream) *stream = c->stream;
  return 0;
}
