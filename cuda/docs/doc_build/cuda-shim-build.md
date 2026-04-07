# CUDA Runtime Shim build guide (libcudart.so.12)

This doc describes how to build the CUDA Runtime shim that lets `cuda/demo/demo` run on ProtoGPU (no workload JSON).

Prereqs:
- CMake >= 3.20
- C++17 compiler
- A Linux userland to *run* the demo (`cuda/demo/demo` is ELF). On Windows, use WSL2.

Related docs:
- Requirements: `cuda/docs/cuda-shim-requirement.md`
- Design: `cuda/docs/doc_design/cuda-shim-logical-design.md`
- Implementation guide: `cuda/docs/doc_dev/cuda-shim-dev.md`

---

## 1) Build (recommended: out-of-source)

From repo root:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Artifacts:
- Shim library target: `gpu-sim-cudart-shim`
  - Produces `libcudart.so.12`-compatible SONAME (`libcudart.so.12` symlink behavior depends on platform/toolchain)
- Smoke test: `gpu-sim-cudart-shim-smoke-tests`

Notes:
- The shim implementation lives under `cuda/src/cudart_shim/`.
- The shim links against `gpusim_core` and uses in-memory Runtime APIs.

---

## 2) Embedded assets generation

The shim embeds 3 JSON assets by default:
- `assets/configs/demo_config.json`
- `assets/ptx_isa/demo_ptx64.json`
- `assets/inst_desc/demo_desc.json`

During build, CMake generates a C++ file:
- `${build}/generated/cudart_shim/assets_embedded.cpp`

The generator script is:
- `cmake/generate_cudart_shim_assets.cmake`

If you change any of the 3 JSON files, rebuilding will regenerate the embedded asset TU.

---

## 3) Run unit smoke test (recommended)

After building:

```bash
ctest --test-dir build -V -R "^gpu-sim-cudart-shim-smoke-tests$"
```

This validates:
- `cudaMalloc` / `cudaMemcpy` (H2D, D2H) / `cudaFree`
- Embedded assets initialization path

---

## 3.0) Run streaming shim test (no CUDA toolkit required)

This builds and runs a small C++ executable that links against the shim and exercises:
- `cudaStreamCreate/Destroy/Synchronize`
- `cudaMemcpyAsync` on two streams (MVP semantics: synchronous execution, FIFO ordering)

```bash
ctest --test-dir build -V -R "^gpu-sim-cudart-shim-streaming-tests$"
```

---

## 3.1) Run end-to-end demo integration (Linux/WSL)

This runs the shim demo path against `cuda/demo/demo.cu`. When clang + CUDA Toolkit are available, the script compiles the host binary and generates text PTX on the fly; otherwise it falls back to the prebuilt `cuda/demo/demo` and `cuda/demo/demo.ptx` artifacts. In both cases it runs the binary against the shim via `LD_LIBRARY_PATH` and asserts it prints `OK`.

Via CTest:

```bash
ctest --test-dir build -V -R "^gpu-sim-cudart-shim-demo-integration$"
```

Via repo script:

```bash
bash scripts/run_cuda_shim_e2e_demo_integration.sh build
```

---

## 4) Troubleshooting build issues

### 4.1 Missing `libcudart.so.12` at runtime

The shim target is named `gpu-sim-cudart-shim`, but the produced file name is controlled via:
- `OUTPUT_NAME cudart`
- `SOVERSION 12`

Inspect build output directory:

```bash
ls -la build | grep cudart
```

### 4.2 Symbol versioning (`cudaMalloc@libcudart.so.12`)

Some toolchains may require GNU ld symbol version scripts.

This repo currently enables symbol versioning for the shim on Linux/WSL:
- Version script: `cuda/src/cudart_shim/libcudart.so.12.map`
- Linker wiring: root `CMakeLists.txt` adds `-Wl,--version-script=...` to `gpu-sim-cudart-shim` when `UNIX AND NOT APPLE`.

If you want to verify the output contains versioned symbols:

```bash
readelf --version-info build/libcudart.so.12.0 | sed -n '1,160p'
nm -D build/libcudart.so.12.0 | grep -E 'cudaMalloc@|__cudaRegisterFatBinary@' || true
```
