# CUDA Runtime Shim user guide (run `cuda/demo/demo` on gpu-sim)

This doc explains how to run the clang-built CUDA host demo against gpu-sim via the CUDA Runtime shim.

Scope:
- MVP: Linux/WSL, `cuda/demo/demo` loads `libcudart.so.12` from this repo build.
- The shim currently implements: `cudaMalloc/cudaFree/cudaMemcpy/cudaDeviceSynchronize/cudaGetLastError/cudaGetErrorString` plus the `__cudaRegister*` and `__cudaPush/PopCallConfiguration` hooks.
- Kernel launch (`cudaLaunchKernel`) is implemented for the demo path using fatbin→PTX extraction + PTX `.param` arg packing.

---

## 1) Build first

Follow the build instructions:
- `cuda/docs/doc_build/cuda-shim-build.md`

---

## 2) Run the demo with the shim

Prereq:
- Run on Linux or WSL2 (the demo binary is ELF).

From repo root:

```bash
# Make sure the dynamic loader can find our shim first
export LD_LIBRARY_PATH="$PWD/build:${LD_LIBRARY_PATH}"

# Optional: enable shim logging
export GPUSIM_CUDART_SHIM_LOG=1

./cuda/demo/demo
```

Notes:
- If you forget to set `LD_LIBRARY_PATH`, the demo will likely load your system CUDA `libcudart` instead (and you may see errors like "CUDA driver version is insufficient for CUDA runtime version").
- If you see `libcudart.so.12: no version information available`, ensure you are running a recent build of the shim (it uses an ELF version script on Linux/WSL).

If you want to verify linkage:

```bash
ldd ./cuda/demo/demo | grep -E 'cudart|not found'
```

---

## 3) Assets input policy (env override or embedded fallback)

The shim loads 3 assets using **two-level priority**:

1) If env var is set, load from that path (fail-fast if unreadable/unparseable)
2) Otherwise, use embedded JSON compiled into the shim

Env vars:
- `GPUSIM_CONFIG` — config json
- `GPUSIM_PTX_ISA` — PTX ISA mapping json
- `GPUSIM_INST_DESC` — instruction descriptor json

Example:

```bash
export GPUSIM_CONFIG=$PWD/assets/configs/demo_config.json
export GPUSIM_PTX_ISA=$PWD/assets/ptx_isa/demo_ptx64.json
export GPUSIM_INST_DESC=$PWD/assets/inst_desc/demo_desc.json
```

Fail-fast behavior:
- If any env var is set to a bad path, the first shim API call that needs init will return `cudaErrorInitializationError` and latch the error for subsequent calls.

---

## 4) Debugging tips

### 4.1 Check last error

The shim maintains per-thread last error.

- `cudaGetLastError()` returns and clears last error.
- `cudaGetErrorString()` returns a string for shim-defined error codes.

### 4.2 Enable logging

```bash
export GPUSIM_CUDART_SHIM_LOG=1
```

This prints basic shim events to stderr.

### 4.3 Debug fatbin / PTX extraction

Optional env vars (MVP debugging aids):

- `GPUSIM_CUDART_SHIM_DUMP_FATBIN=<path>`: dump up to 4MiB starting at the fatbin payload pointer.
- `GPUSIM_CUDART_SHIM_DUMP_FATBIN_WRAPPER=<path>`: dump the first 32 bytes of the wrapper passed to `__cudaRegisterFatBinary`.
- `GPUSIM_CUDART_SHIM_DUMP_PTX=<pathPrefix>`: dump extracted PTX modules as `<pathPrefix>_<i>.ptx`.

If your toolchain embeds PTX in a tokenized/compressed form inside the fatbin (so the dumped `.ptx` looks garbled),
you can provide a known-good PTX file to the shim:

- `GPUSIM_CUDART_SHIM_PTX_OVERRIDE=<path/to/module.ptx>`: use this PTX text instead of extracting PTX from the fatbin.

---

## 5) Current limitations (MVP in progress)

- Fatbin parsing is MVP-grade and is only intended to handle the clang-produced demo format in this repo.
- Streams are treated as synchronous (FIFO model not yet exposed as real async).
- No `cudaStreamCreate/Destroy` yet.

---

## 6) Tests

On Linux/WSL, there is an end-to-end integration test that runs the prebuilt demo binary against the shim.

Via CTest:

```bash
ctest --test-dir build -V -R "^gpu-sim-cudart-shim-demo-integration$"
```

Via repo script (also invoked by `scripts/run_integration_tests.sh`):

```bash
bash scripts/run_cuda_shim_demo_integration.sh build
```
