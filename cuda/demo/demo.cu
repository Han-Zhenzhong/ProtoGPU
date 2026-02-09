#include <cuda_runtime.h>

#include <cstdio>
#include <cstdlib>
#include <cstdint>

__global__ void add_kernel(const std::uint32_t* a, const std::uint32_t* b, std::uint32_t* c, std::uint32_t n) {
  const std::uint32_t i = static_cast<std::uint32_t>(blockIdx.x * blockDim.x + threadIdx.x);
  if (i < n) {
    c[i] = a[i] + b[i];
  }
}

static void check(cudaError_t err, const char* what) {
  if (err != cudaSuccess) {
    std::fprintf(stderr, "CUDA error %s: %s\n", what, cudaGetErrorString(err));
    std::exit(1);
  }
}

int main() {
  const std::uint32_t n = 256;
  const std::size_t bytes = n * sizeof(std::uint32_t);

  std::uint32_t* h_a = nullptr;
  std::uint32_t* h_b = nullptr;
  std::uint32_t* h_c = nullptr;
  h_a = static_cast<std::uint32_t*>(std::malloc(bytes));
  h_b = static_cast<std::uint32_t*>(std::malloc(bytes));
  h_c = static_cast<std::uint32_t*>(std::malloc(bytes));
  if (!h_a || !h_b || !h_c) {
    std::fprintf(stderr, "host malloc failed\n");
    return 1;
  }

  for (std::uint32_t i = 0; i < n; i++) {
    h_a[i] = i;
    h_b[i] = 2 * i;
  }

  std::uint32_t* d_a = nullptr;
  std::uint32_t* d_b = nullptr;
  std::uint32_t* d_c = nullptr;
  check(cudaMalloc(&d_a, bytes), "cudaMalloc d_a");
  check(cudaMalloc(&d_b, bytes), "cudaMalloc d_b");
  check(cudaMalloc(&d_c, bytes), "cudaMalloc d_c");

  check(cudaMemcpy(d_a, h_a, bytes, cudaMemcpyHostToDevice), "cudaMemcpy H2D a");
  check(cudaMemcpy(d_b, h_b, bytes, cudaMemcpyHostToDevice), "cudaMemcpy H2D b");

  const dim3 block(128, 1, 1);
  const dim3 grid((n + block.x - 1) / block.x, 1, 1);
  add_kernel<<<grid, block>>>(d_a, d_b, d_c, n);
  check(cudaGetLastError(), "kernel launch");
  check(cudaDeviceSynchronize(), "cudaDeviceSynchronize");

  check(cudaMemcpy(h_c, d_c, bytes, cudaMemcpyDeviceToHost), "cudaMemcpy D2H c");

  bool ok = true;
  for (std::uint32_t i = 0; i < n; i++) {
    if (h_c[i] != h_a[i] + h_b[i]) {
      std::fprintf(stderr, "mismatch at %u: got %u expected %u\n", i, h_c[i], h_a[i] + h_b[i]);
      ok = false;
      break;
    }
  }

  std::printf(ok ? "OK\n" : "FAIL\n");

  cudaFree(d_a);
  cudaFree(d_b);
  cudaFree(d_c);
  std::free(h_a);
  std::free(h_b);
  std::free(h_c);
  return ok ? 0 : 1;
}
