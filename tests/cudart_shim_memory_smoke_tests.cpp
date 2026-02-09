#include "cudart_shim/cuda_abi_min.h"

#include <cstdint>
#include <cstring>
#include <iostream>

extern "C" {
  gpusim_cudart_shim::cudaError_t cudaMalloc(void** devPtr, std::size_t size);
  gpusim_cudart_shim::cudaError_t cudaFree(void* devPtr);
  gpusim_cudart_shim::cudaError_t cudaMemcpy(void* dst, const void* src, std::size_t count, gpusim_cudart_shim::cudaMemcpyKind kind);
  gpusim_cudart_shim::cudaError_t cudaGetLastError();
  const char* cudaGetErrorString(gpusim_cudart_shim::cudaError_t err);
}


static int fail(const char* where, gpusim_cudart_shim::cudaError_t e) {
  std::cerr << "FAIL " << where << ": " << cudaGetErrorString(e) << " (" << static_cast<int>(e) << ")\n";
  auto le = cudaGetLastError();
  if (le != gpusim_cudart_shim::cudaSuccess) {
    std::cerr << "  last error: " << cudaGetErrorString(le) << "\n";
  }
  return 1;
}

int main() {
  using namespace gpusim_cudart_shim;

  std::uint32_t host_in = 0x12345678u;
  std::uint32_t host_out = 0;

  void* dev = nullptr;
  if (auto e = cudaMalloc(&dev, sizeof(host_in)); e != cudaSuccess) return fail("cudaMalloc", e);

  if (auto e = cudaMemcpy(dev, &host_in, sizeof(host_in), cudaMemcpyHostToDevice); e != cudaSuccess) return fail("cudaMemcpy H2D", e);
  if (auto e = cudaMemcpy(&host_out, dev, sizeof(host_out), cudaMemcpyDeviceToHost); e != cudaSuccess) return fail("cudaMemcpy D2H", e);

  if (std::memcmp(&host_in, &host_out, sizeof(host_in)) != 0) {
    std::cerr << "FAIL memcmp: expected " << std::hex << host_in << " got " << host_out << "\n";
    return 1;
  }

  if (auto e = cudaFree(dev); e != cudaSuccess) return fail("cudaFree", e);

  std::cout << "OK\n";
  return 0;
}
