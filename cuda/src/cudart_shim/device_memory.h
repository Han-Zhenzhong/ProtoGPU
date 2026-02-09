#pragma once

#include "gpusim/abi.h"
#include "cudart_shim/cuda_abi_min.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>

namespace gpusim {
class Runtime;
} // namespace gpusim

namespace gpusim_cudart_shim {

struct Allocation final {
  gpusim::DevicePtr base = 0;
  std::uint64_t bytes = 0;
  std::uint64_t align = 0;
};

class DeviceMemory final {
public:
  cudaError_t malloc(gpusim::Runtime& rt, void** out_dev_ptr, std::size_t bytes);
  cudaError_t free(void* dev_ptr);

  cudaError_t memcpy(gpusim::Runtime& rt,
                     void* dst,
                     const void* src,
                     std::size_t bytes,
                     cudaMemcpyKind kind);

  // For arg packing: validate that a device pointer value is within an allocation.
  bool is_valid_device_range(gpusim::DevicePtr ptr, std::uint64_t bytes = 1) const;

private:
  std::optional<Allocation> find_allocation_containing(gpusim::DevicePtr ptr) const;

  // keyed by base pointer
  std::map<gpusim::DevicePtr, Allocation> allocs_;
};

// Helper to encode/decode DevicePtr <-> host void*.
inline gpusim::DevicePtr decode_device_ptr(const void* p) {
  return static_cast<gpusim::DevicePtr>(reinterpret_cast<std::uintptr_t>(p));
}
inline void* encode_device_ptr(gpusim::DevicePtr p) {
  return reinterpret_cast<void*>(static_cast<std::uintptr_t>(p));
}

} // namespace gpusim_cudart_shim
