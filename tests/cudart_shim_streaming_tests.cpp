#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

// A small host-side shim test that exercises streams + async copies.
// This intentionally avoids depending on CUDA headers/toolkit: it targets the shim ABI.

extern "C" {

typedef int cudaError_t;

typedef void* cudaStream_t;

enum cudaMemcpyKind : int {
  cudaMemcpyHostToHost = 0,
  cudaMemcpyHostToDevice = 1,
  cudaMemcpyDeviceToHost = 2,
  cudaMemcpyDeviceToDevice = 3,
  cudaMemcpyDefault = 4,
};

enum {
  cudaSuccess = 0,
};

cudaError_t cudaGetLastError();
const char* cudaGetErrorString(cudaError_t);

cudaError_t cudaDeviceSynchronize();

cudaError_t cudaMalloc(void** devPtr, std::size_t size);
cudaError_t cudaFree(void* devPtr);

cudaError_t cudaMemcpyAsync(void* dst, const void* src, std::size_t count, cudaMemcpyKind kind, cudaStream_t stream);

cudaError_t cudaStreamCreate(cudaStream_t* pStream);
cudaError_t cudaStreamDestroy(cudaStream_t stream);
cudaError_t cudaStreamSynchronize(cudaStream_t stream);
}

static void check(cudaError_t err, const char* what) {
  if (err != cudaSuccess) {
    std::fprintf(stderr, "CUDA error %s: %s\n", what, cudaGetErrorString(err));
    std::exit(1);
  }
}

static std::uint32_t pattern(std::uint32_t seed, std::uint32_t i) {
  // cheap non-crypto mixing
  std::uint32_t x = seed ^ (i * 2654435761u);
  x ^= (x >> 16);
  x *= 2246822519u;
  x ^= (x >> 13);
  return x;
}

int main() {
  const std::uint32_t n = 4096;
  const std::size_t bytes = static_cast<std::size_t>(n) * sizeof(std::uint32_t);

  cudaStream_t s0 = nullptr;
  cudaStream_t s1 = nullptr;
  check(cudaStreamCreate(&s0), "cudaStreamCreate s0");
  check(cudaStreamCreate(&s1), "cudaStreamCreate s1");

  void* d0 = nullptr;
  void* d1 = nullptr;
  check(cudaMalloc(&d0, bytes), "cudaMalloc d0");
  check(cudaMalloc(&d1, bytes), "cudaMalloc d1");

  std::vector<std::uint32_t> h0_src(n), h0_dst(n);
  std::vector<std::uint32_t> h1_src(n), h1_dst(n);

  // Enqueue work on two streams, then synchronize.
  // The shim currently executes synchronously, but we still validate the API surface and ordering.
  for (std::uint32_t iter = 0; iter < 4; iter++) {
    const std::uint32_t seed0 = 0x12340000u + iter;
    const std::uint32_t seed1 = 0xABCD0000u + iter;

    for (std::uint32_t i = 0; i < n; i++) {
      h0_src[i] = pattern(seed0, i);
      h1_src[i] = pattern(seed1, i);
      h0_dst[i] = 0;
      h1_dst[i] = 0;
    }

    check(cudaMemcpyAsync(d0, h0_src.data(), bytes, cudaMemcpyHostToDevice, s0), "cudaMemcpyAsync H2D s0");
    check(cudaMemcpyAsync(h0_dst.data(), d0, bytes, cudaMemcpyDeviceToHost, s0), "cudaMemcpyAsync D2H s0");

    check(cudaMemcpyAsync(d1, h1_src.data(), bytes, cudaMemcpyHostToDevice, s1), "cudaMemcpyAsync H2D s1");
    check(cudaMemcpyAsync(h1_dst.data(), d1, bytes, cudaMemcpyDeviceToHost, s1), "cudaMemcpyAsync D2H s1");
  }

  check(cudaStreamSynchronize(s0), "cudaStreamSynchronize s0");
  check(cudaStreamSynchronize(s1), "cudaStreamSynchronize s1");
  check(cudaDeviceSynchronize(), "cudaDeviceSynchronize");

  // Validate the final iteration results.
  bool ok = true;
  const std::uint32_t seed0 = 0x12340000u + 3;
  const std::uint32_t seed1 = 0xABCD0000u + 3;
  for (std::uint32_t i = 0; i < n; i++) {
    const std::uint32_t exp0 = pattern(seed0, i);
    const std::uint32_t exp1 = pattern(seed1, i);
    if (h0_dst[i] != exp0) {
      std::fprintf(stderr, "mismatch s0 at %u: got %u expected %u\n", i, h0_dst[i], exp0);
      ok = false;
      break;
    }
    if (h1_dst[i] != exp1) {
      std::fprintf(stderr, "mismatch s1 at %u: got %u expected %u\n", i, h1_dst[i], exp1);
      ok = false;
      break;
    }
  }

  check(cudaFree(d0), "cudaFree d0");
  check(cudaFree(d1), "cudaFree d1");
  check(cudaStreamDestroy(s0), "cudaStreamDestroy s0");
  check(cudaStreamDestroy(s1), "cudaStreamDestroy s1");

  std::printf(ok ? "OK\n" : "FAIL\n");
  return ok ? 0 : 1;
}