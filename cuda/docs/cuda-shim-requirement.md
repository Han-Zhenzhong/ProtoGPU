# CUDA runtime shim requirements

This document defines the minimum requirements for implementing a CUDA Runtime (`libcudart.so.12`) shim that routes CUDA host code execution into gpu-sim.

## Scope / non-goals

In scope (MVP for `cuda/demo/demo`):

- `cudaMalloc`, `cudaFree`, `cudaMemcpy`
- `cudaLaunchKernel`
- `cudaDeviceSynchronize`, `cudaGetLastError`, `cudaGetErrorString`
- CUDA registration + launch configuration symbols used by clang output:
     - `__cudaRegisterFatBinary`, `__cudaRegisterFatBinaryEnd`, `__cudaRegisterFunction`, `__cudaUnregisterFatBinary`
     - `__cudaPushCallConfiguration`, `__cudaPopCallConfiguration`

Explicitly out of scope for MVP (can be added later):

- Streams/events concurrency semantics beyond “default stream + synchronous execution”
- Cooperative groups, dynamic parallelism, textures/surfaces, unified memory, graphs
- JIT compilation of PTX to SASS; only PTX execution via gpu-sim

## Required `libcudart.so.12` symbols

The demo binary (`cuda/demo/demo`) currently depends on the following undefined CUDA Runtime symbols:

```text
hanzz@DESKTOP-DPLK3ES:~/gpu-sim$ nm -u cuda/demo/demo
                                 w _ITM_deregisterTMCloneTable
                                 w _ITM_registerTMCloneTable
                                 U __cudaPopCallConfiguration@libcudart.so.12
                                 U __cudaPushCallConfiguration@libcudart.so.12
                                 U __cudaRegisterFatBinary@libcudart.so.12
                                 U __cudaRegisterFatBinaryEnd@libcudart.so.12
                                 U __cudaRegisterFunction@libcudart.so.12
                                 U __cudaUnregisterFatBinary@libcudart.so.12
                                 U __cxa_atexit@GLIBC_2.2.5
                                 w __cxa_finalize@GLIBC_2.2.5
                                 w __gmon_start__
                                 U __libc_start_main@GLIBC_2.34
                                 U cudaDeviceSynchronize@libcudart.so.12
                                 U cudaFree@libcudart.so.12
                                 U cudaGetErrorString@libcudart.so.12
                                 U cudaGetLastError@libcudart.so.12
                                 U cudaLaunchKernel@libcudart.so.12
                                 U cudaMalloc@libcudart.so.12
                                 U cudaMemcpy@libcudart.so.12
                                 U exit@GLIBC_2.2.5
                                 U fprintf@GLIBC_2.2.5
                                 U free@GLIBC_2.2.5
                                 U malloc@GLIBC_2.2.5
                                 U printf@GLIBC_2.2.5
                                 U stderr@GLIBC_2.2.5
```

## `ptx_isa` / `inst_desc` / `config` input

Input priority (recommended):

1. Environment variables (override):
     - `GPUSIM_CONFIG=<path/to/config.json>`
     - `GPUSIM_PTX_ISA=<path/to/ptx_isa.json>`
     - `GPUSIM_INST_DESC=<path/to/inst_desc.json>`
2. Embedded JSON text in the shim/library (fallback; no default-path mode):
     - Embed the JSON blobs at build time (via CMake-generated `.cpp`) and load via `*load_json_text*` APIs.

Behavioral requirements:

- If any of `GPUSIM_CONFIG`, `GPUSIM_PTX_ISA`, `GPUSIM_INST_DESC` is set, the shim must load that file path.
- If an env var is set but the file cannot be read/parsed, fail fast with a clear error.
- If no env vars are set, the shim must use embedded JSON.

Recommended: support optional env var `GPUSIM_TRACE=<path>` / `GPUSIM_STATS=<path>` later, but not required for MVP.

## PTX extraction (fatbin → PTX text)

The shim must be able to obtain PTX text corresponding to the registered fatbin/module.

Minimum requirement:

- Implement extraction for the fatbin format emitted by the chosen toolchain (clang for this repo).

Contracts:

- `__cudaRegisterFatBinary(fatbin)` provides a module handle (opaque pointer). The shim must associate that handle with the underlying fatbin content.
- The shim must be able to provide PTX text for a module handle at kernel launch time.
- If PTX cannot be extracted, fail fast with a clear diagnostic indicating the fatbin format is unsupported.

## Kernel entry name

Do **not** rely on source-level names from host C (e.g. `add_kernel`).

Kernel entry must be taken from CUDA registration callbacks:

- `__cudaRegisterFunction(..., hostFun, deviceName, ...)`
    - Record mapping: `hostFun -> deviceName`
    - `deviceName` is typically the mangled PTX `.entry` name.

At launch time:

- `cudaLaunchKernel(func, ...)` uses `func` (the `hostFun`) to look up `deviceName`, then runs that PTX entry.

Consistency requirement:

- Validate that the extracted PTX contains `.entry <deviceName>`; otherwise fail fast with a clear diagnostic.

Required mapping:

- The shim must preserve the association between `hostFun` and the fatbin/module that registered it (so it can select the correct PTX text when multiple modules exist).

## Launch configuration (`<<<grid, block, shared, stream>>>`)

The shim must correctly supply launch dimensions to gpu-sim:

- Launch configuration must be fully supported from the start:
     - `gridDim` (x,y,z)
     - `blockDim` (x,y,z)
     - `sharedMemBytes` (dynamic shared memory)
     - `stream`

Sources of truth (must support both):

- Preferred: use the explicit arguments to `cudaLaunchKernel(func, gridDim, blockDim, kernelParams, sharedMemBytes, stream)`.
- Fallback: if the compiler uses the legacy path, recover the configuration via `__cudaPushCallConfiguration` / `__cudaPopCallConfiguration`.

Required semantics:

- `sharedMemBytes` must be propagated into the gpu-sim launch path (even if the current PTX does not consume it).
     - If/when the PTX uses dynamic shared memory, gpu-sim must allocate and address it consistently with this value.
- `stream` must be respected as an execution ordering mechanism:
     - Operations enqueued to the same `cudaStream_t` execute in program order.
     - Different streams may interleave; the shim must not incorrectly reorder operations within a single stream.
     - `cudaDeviceSynchronize()` waits for completion of all previously enqueued work across all streams.
     - If stream support is implemented via a simple per-stream queue (single-threaded executor), that is acceptable for correctness; true overlap/concurrency is not required initially.

Validation:

- If a launch uses invalid dimensions (0 in any grid/block component), return an appropriate CUDA error and set last error.
- If configuration is missing/mismatched between push/pop and launch, fail fast with a clear diagnostic.

## Parameters / argument packing

Values come from host code via `cudaLaunchKernel`'s `kernelParams` (usually `void**` pointing to each argument value).

Layout comes from PTX, not from host code:

- Parse the PTX entry signature (the `.param` declarations) to get param order/size/alignment/offset.
- Pack `kernelParams` into a param blob according to that layout (`KernelArgs.layout` + `KernelArgs.blob`).

Pointer arguments:

- `cudaMalloc` returns a device pointer value managed by the shim.
- When packing args, write that device pointer value into the corresponding `.u64` param.

## Device pointer and memory model

Requirements:

- `cudaMalloc` must return a stable device pointer value usable as a PTX `.u64` pointer parameter.
- `cudaFree` must release the corresponding allocation (at least to avoid unbounded growth during tests).
- `cudaMemcpy` must support at minimum:
     - `cudaMemcpyHostToDevice`
     - `cudaMemcpyDeviceToHost`
     - `cudaMemcpyDeviceToDevice`

Recommended behavior:

- For MVP, treat device pointers as gpu-sim `DevicePtr` values (no pointer tagging needed).
- Validate copy ranges and fail fast on invalid pointers/ranges.

## Error handling and diagnostics

Requirements:

- Maintain a per-thread “last error” state compatible with `cudaGetLastError`.
- Provide `cudaGetErrorString` strings for at least the errors produced by the shim (e.g. invalid value, launch failure, invalid device pointer).

When failing fast, error messages should include:

- Which API failed (`cudaLaunchKernel`, `__cudaRegisterFunction`, etc.)
- Kernel device name (if available)
- Whether the failure was due to missing registration, PTX extraction failure, entry not found, or argument packing mismatch

## Thread safety and lifetime

Minimum requirements:

- Multiple kernels may be registered before any launch. The shim must not assume registration and launch happen in a strict alternating sequence.
- The registry maps (fatbin handles, hostFun→deviceName) must be thread-safe enough for typical single-threaded host code; prefer a mutex around shared maps for simplicity.
- `__cudaUnregisterFatBinary` must remove module state and any associated function mappings.