#include <cuda_runtime.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

// A small streaming CUDA C demo intended to be compiled with clang + CUDA Toolkit
// and executed against gpu-sim via the CUDA Runtime shim.
//
// Note: the shim's fatbin -> PTX extraction is MVP-grade, so running this demo
// currently expects using GPUSIM_CUDART_SHIM_PTX_OVERRIDE to provide a text PTX
// compiled from this same source file.

__global__ void add_kernel(const std::uint32_t* a, const std::uint32_t* b, std::uint32_t* c, std::uint32_t n) {
  const std::uint32_t i = static_cast<std::uint32_t>(blockIdx.x * blockDim.x + threadIdx.x);
  if (i < n) c[i] = a[i] + b[i];
}

static void check(cudaError_t err, const char* what) {
  if (err != cudaSuccess) {
    std::fprintf(stderr, "CUDA error %s: %s\n", what, cudaGetErrorString(err));
    std::exit(1);
  }
}

static void fill(std::uint32_t* p, std::uint32_t n, std::uint32_t base) {
  for (std::uint32_t i = 0; i < n; i++) p[i] = base + i;
}

static bool verify(const std::uint32_t* a, const std::uint32_t* b, const std::uint32_t* c, std::uint32_t n) {
  for (std::uint32_t i = 0; i < n; i++) {
    const std::uint32_t exp = a[i] + b[i];
    if (c[i] != exp) {
      std::fprintf(stderr, "mismatch at %u: got %u expected %u\n", i, c[i], exp);
      return false;
    }
  }
  return true;
}

int main() {
  const std::uint32_t n = 4096;
  const std::size_t bytes = static_cast<std::size_t>(n) * sizeof(std::uint32_t);

  // Two independent streams, to validate stream argument propagation.
  cudaStream_t s0 = nullptr;
  cudaStream_t s1 = nullptr;
  check(cudaStreamCreate(&s0), "cudaStreamCreate s0");
  check(cudaStreamCreate(&s1), "cudaStreamCreate s1");

  // Host buffers.
  auto* h0_a = static_cast<std::uint32_t*>(std::malloc(bytes));
  auto* h0_b = static_cast<std::uint32_t*>(std::malloc(bytes));
  auto* h0_c = static_cast<std::uint32_t*>(std::malloc(bytes));
  auto* h1_a = static_cast<std::uint32_t*>(std::malloc(bytes));
  auto* h1_b = static_cast<std::uint32_t*>(std::malloc(bytes));
  auto* h1_c = static_cast<std::uint32_t*>(std::malloc(bytes));
  if (!h0_a || !h0_b || !h0_c || !h1_a || !h1_b || !h1_c) {
    std::fprintf(stderr, "host malloc failed\n");
    return 1;
  }

  fill(h0_a, n, 100);
  fill(h0_b, n, 200);
  fill(h1_a, n, 1000);
  fill(h1_b, n, 2000);

  // Device buffers (two independent sets).
  std::uint32_t *d0_a = nullptr, *d0_b = nullptr, *d0_c = nullptr;
  std::uint32_t *d1_a = nullptr, *d1_b = nullptr, *d1_c = nullptr;
  check(cudaMalloc(reinterpret_cast<void**>(&d0_a), bytes), "cudaMalloc d0_a");
  check(cudaMalloc(reinterpret_cast<void**>(&d0_b), bytes), "cudaMalloc d0_b");
  check(cudaMalloc(reinterpret_cast<void**>(&d0_c), bytes), "cudaMalloc d0_c");
  check(cudaMalloc(reinterpret_cast<void**>(&d1_a), bytes), "cudaMalloc d1_a");
  check(cudaMalloc(reinterpret_cast<void**>(&d1_b), bytes), "cudaMalloc d1_b");
  check(cudaMalloc(reinterpret_cast<void**>(&d1_c), bytes), "cudaMalloc d1_c");

  const dim3 block(256, 1, 1);
  const dim3 grid((n + block.x - 1) / block.x, 1, 1);

  // Enqueue per-stream H2D -> kernel -> D2H.
  check(cudaMemcpyAsync(d0_a, h0_a, bytes, cudaMemcpyHostToDevice, s0), "cudaMemcpyAsync H2D s0 a");
  check(cudaMemcpyAsync(d0_b, h0_b, bytes, cudaMemcpyHostToDevice, s0), "cudaMemcpyAsync H2D s0 b");
  add_kernel<<<grid, block, 0, s0>>>(d0_a, d0_b, d0_c, n);
  check(cudaGetLastError(), "kernel launch s0");
  check(cudaMemcpyAsync(h0_c, d0_c, bytes, cudaMemcpyDeviceToHost, s0), "cudaMemcpyAsync D2H s0 c");

  check(cudaMemcpyAsync(d1_a, h1_a, bytes, cudaMemcpyHostToDevice, s1), "cudaMemcpyAsync H2D s1 a");
  check(cudaMemcpyAsync(d1_b, h1_b, bytes, cudaMemcpyHostToDevice, s1), "cudaMemcpyAsync H2D s1 b");
  add_kernel<<<grid, block, 0, s1>>>(d1_a, d1_b, d1_c, n);
  check(cudaGetLastError(), "kernel launch s1");
  check(cudaMemcpyAsync(h1_c, d1_c, bytes, cudaMemcpyDeviceToHost, s1), "cudaMemcpyAsync D2H s1 c");

  check(cudaStreamSynchronize(s0), "cudaStreamSynchronize s0");
  check(cudaStreamSynchronize(s1), "cudaStreamSynchronize s1");

  const bool ok0 = verify(h0_a, h0_b, h0_c, n);
  const bool ok1 = verify(h1_a, h1_b, h1_c, n);
  const bool ok = ok0 && ok1;

  cudaFree(d0_a);
  cudaFree(d0_b);
  cudaFree(d0_c);
  cudaFree(d1_a);
  cudaFree(d1_b);
  cudaFree(d1_c);
  cudaStreamDestroy(s0);
  cudaStreamDestroy(s1);

  std::free(h0_a);
  std::free(h0_b);
  std::free(h0_c);
  std::free(h1_a);
  std::free(h1_b);
  std::free(h1_c);

  std::printf(ok ? "OK\n" : "FAIL\n");
  return ok ? 0 : 1;
}
