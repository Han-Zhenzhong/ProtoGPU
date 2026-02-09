#include "cudart_shim/kernel_args_pack.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>

namespace gpusim_cudart_shim {

cudaError_t pack_kernel_args_mvp(const std::vector<gpusim::ParamDesc>& layout,
                                void** kernelParams,
                                const DeviceMemory& device_memory,
                                gpusim::KernelArgs& out_args,
                                std::string& out_error) {
  out_error.clear();

  const bool log_args = []() -> bool {
    if (const char* v = std::getenv("GPUSIM_CUDART_SHIM_LOG_ARGS")) {
      return v && *v != '\0';
    }
    return false;
  }();

  if (!kernelParams && !layout.empty()) {
    out_error = "kernelParams is null";
    return cudaErrorInvalidValue;
  }

  out_args.layout = layout;

  std::uint64_t blob_size = 0;
  for (const auto& p : layout) {
    blob_size = std::max<std::uint64_t>(blob_size, static_cast<std::uint64_t>(p.offset) + p.size);
  }
  out_args.blob.assign(static_cast<std::size_t>(blob_size), 0);

  if (log_args) {
    std::fprintf(stderr, "[gpu-sim-cudart-shim] pack_kernel_args(layout=%zu, kernelParams=%p, blob_size=%zu)\n",
                 static_cast<std::size_t>(layout.size()),
                 static_cast<void*>(kernelParams),
                 static_cast<std::size_t>(out_args.blob.size()));
  }

  for (std::size_t i = 0; i < layout.size(); i++) {
    const auto& p = layout[i];
    void* src_ptr = kernelParams[i];
    if (!src_ptr) {
      out_error = "kernelParams[" + std::to_string(i) + "] is null";
      return cudaErrorInvalidValue;
    }

    if (static_cast<std::uint64_t>(p.offset) + p.size > out_args.blob.size()) {
      out_error = "param layout out of bounds";
      return cudaErrorInvalidValue;
    }

    auto* dst = out_args.blob.data() + p.offset;

    switch (p.type) {
    case gpusim::ParamType::U32: {
      if (p.size != 4) {
        out_error = "unsupported u32 param size=" + std::to_string(p.size);
        return cudaErrorNotSupported;
      }
      std::uint32_t v = 0;
      std::memcpy(&v, src_ptr, sizeof(v));
      std::memcpy(dst, &v, sizeof(v));
      if (log_args) {
        std::fprintf(stderr,
                     "[gpu-sim-cudart-shim]  arg[%zu] %s U32 off=%u size=%u src=%p val=%u (0x%08x)\n",
                     i,
                     p.name.c_str(),
                     static_cast<unsigned>(p.offset),
                     static_cast<unsigned>(p.size),
                     src_ptr,
                     static_cast<unsigned>(v),
                     static_cast<unsigned>(v));
      }
      break;
    }
    case gpusim::ParamType::U64: {
      if (p.size != 8) {
        out_error = "unsupported u64 param size=" + std::to_string(p.size);
        return cudaErrorNotSupported;
      }

      // MVP policy: treat .u64 params as device pointers and validate them.
      std::uint64_t v = 0;
      std::memcpy(&v, src_ptr, sizeof(v));
      if (v != 0 && !device_memory.is_valid_device_range(static_cast<gpusim::DevicePtr>(v), 1)) {
        out_error = "invalid device pointer arg[" + std::to_string(i) + "]=" + std::to_string(v);
        return cudaErrorInvalidDevicePointer;
      }

      std::memcpy(dst, &v, sizeof(v));
      if (log_args) {
        std::fprintf(stderr,
                     "[gpu-sim-cudart-shim]  arg[%zu] %s U64 off=%u size=%u src=%p val=0x%016llx\n",
                     i,
                     p.name.c_str(),
                     static_cast<unsigned>(p.offset),
                     static_cast<unsigned>(p.size),
                     src_ptr,
                     static_cast<unsigned long long>(v));
      }
      break;
    }
    default:
      out_error = "unsupported param type";
      return cudaErrorNotSupported;
    }
  }

  if (log_args) {
    // Print first few bytes of the packed blob for quick sanity checks.
    const std::size_t n = std::min<std::size_t>(out_args.blob.size(), 32);
    std::fprintf(stderr, "[gpu-sim-cudart-shim]  blob[0:%zu]=", n);
    for (std::size_t k = 0; k < n; k++) {
      std::fprintf(stderr, "%02x", static_cast<unsigned>(out_args.blob[k]));
    }
    std::fprintf(stderr, "\n");
  }

  return cudaSuccess;
}

} // namespace gpusim_cudart_shim
