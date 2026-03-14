#include <cuda_runtime.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

// End-to-end demo for the custom PTX instruction:
//   warp_reduce_add.f32 dst, src
// This is expected to run through the ProtoGPU CUDA Runtime shim with
// GPUSIM_CUDART_SHIM_PTX_OVERRIDE pointing to text PTX generated from this file.

__global__ void warp_reduce_add_kernel(float* out) {
  const std::uint32_t tid = static_cast<std::uint32_t>(threadIdx.x);
  float x = static_cast<float>(tid + 1u);
  float y = 0.0f;

  // ProtoGPU executes warp_reduce_add through pre-generated PTX instead of fatbin.
  // Generate PTX including warp_reduce_add from warp_reduce_add_demo_ptx.cu with clang++ and cuda toolkit 12.1, specifying PTX6.4 and SM70.
  // asm volatile("warp_reduce_add.f32 %0, %1;" : "=f"(y) : "f"(x));

  out[tid] = y;
}

static void check(cudaError_t err, const char* what) {
  if (err != cudaSuccess) {
    std::fprintf(stderr, "CUDA error %s: %s\n", what, cudaGetErrorString(err));
    std::exit(1);
  }
}

int main() {
  constexpr std::uint32_t n = 8; // Keep block smaller than warp_size to exercise partial active mask.
  constexpr std::size_t bytes = static_cast<std::size_t>(n) * sizeof(float);

  float* h_out = static_cast<float*>(std::malloc(bytes));
  if (!h_out) {
    std::fprintf(stderr, "host malloc failed\n");
    return 1;
  }

  float* d_out = nullptr;
  check(cudaMalloc(reinterpret_cast<void**>(&d_out), bytes), "cudaMalloc d_out");

  const dim3 block(n, 1, 1);
  const dim3 grid(1, 1, 1);
  warp_reduce_add_kernel<<<grid, block>>>(d_out);
  check(cudaGetLastError(), "kernel launch");
  check(cudaDeviceSynchronize(), "cudaDeviceSynchronize");

  check(cudaMemcpy(h_out, d_out, bytes, cudaMemcpyDeviceToHost), "cudaMemcpy D2H out");

  bool ok = true;
  constexpr float expected = 36.0f;
  for (std::uint32_t i = 0; i < n; i++) {
    if (h_out[i] != expected) {
      std::fprintf(stderr, "mismatch at lane %u: got %.9g expected %.9g\n", i, static_cast<double>(h_out[i]), static_cast<double>(expected));
      ok = false;
      break;
    }
  }

  cudaFree(d_out);
  std::free(h_out);

  std::printf(ok ? "OK\n" : "FAIL\n");
  return ok ? 0 : 1;
}
