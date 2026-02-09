#pragma once

#include <cstddef>
#include <cstdint>

// Minimal CUDA Runtime ABI surface needed by cuda/demo/demo.
// Avoid depending on CUDA headers; keep ABI-compatible types only.

namespace gpusim_cudart_shim {

// CUDA-like error codes (subset). Values chosen to match common CUDA Runtime values.
enum cudaError_t : int {
  cudaSuccess = 0,
  cudaErrorInvalidValue = 1,
  cudaErrorMemoryAllocation = 2,
  cudaErrorInitializationError = 3,
  cudaErrorInvalidDeviceFunction = 8,
  cudaErrorInvalidDevicePointer = 17,
  cudaErrorInvalidMemcpyDirection = 21,
  cudaErrorInvalidPtx = 218,
  cudaErrorNotSupported = 801,
  cudaErrorUnknown = 999,
};

enum cudaMemcpyKind : int {
  cudaMemcpyHostToHost = 0,
  cudaMemcpyHostToDevice = 1,
  cudaMemcpyDeviceToHost = 2,
  cudaMemcpyDeviceToDevice = 3,
  cudaMemcpyDefault = 4,
};

// CUDA dim3 is (effectively) three 32-bit integers on the host ABI.
// Keep the layout minimal and avoid over-alignment; some toolchains pass this as a 12-byte aggregate.
struct dim3 {
  std::uint32_t x;
  std::uint32_t y;
  std::uint32_t z;
};

using cudaStream_t = void*;

} // namespace gpusim_cudart_shim
