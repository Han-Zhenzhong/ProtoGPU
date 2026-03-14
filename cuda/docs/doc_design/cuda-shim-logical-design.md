# CUDA Runtime Shim（libcudart.so.12）Logical Design

This document describes the logical design for a CUDA Runtime shim library that routes CUDA host code into ProtoGPU.

It is derived from:
- Requirements: `cuda/docs/cuda-shim-requirement.md`
- Plan draft: `cuda/docs/doc_plan/plan_design/plan-cudaShim.prompt.md`

The initial MVP target is `cuda/demo/demo` compiled by clang.

---

## 1) Goals & Non-goals

### 1.1 Goals (MVP)

- Provide a `libcudart.so.12`-compatible shared library surface for the symbols required by `cuda/demo/demo`.
- Route:
  - `cudaMalloc/cudaFree/cudaMemcpy`
  - `cudaLaunchKernel`
  - `cudaDeviceSynchronize`, `cudaGetLastError`, `cudaGetErrorString`
  - clang-emitted registration & launch config symbols:
    - `__cudaRegisterFatBinary`, `__cudaRegisterFatBinaryEnd`, `__cudaRegisterFunction`, `__cudaUnregisterFatBinary`
    - `__cudaPushCallConfiguration`, `__cudaPopCallConfiguration`
- No workload JSON for execution/args injection.
- Kernel entry name source of truth is `__cudaRegisterFunction(... deviceName ...)` and must be validated against PTX `.entry`.
- Argument packing:
  - values come from host `kernelParams` (from `cudaLaunchKernel`)
  - layout comes from PTX `.param` (via ProtoGPU Frontend Parser/Binder)
- Assets input policy:
  - environment override (`GPUSIM_CONFIG`, `GPUSIM_PTX_ISA`, `GPUSIM_INST_DESC`)
  - otherwise embedded JSON text (no default-path mode)

### 1.2 Non-goals (MVP)

- True overlap / concurrency (single-threaded execution is acceptable).
- Full CUDA runtime coverage beyond the required symbol list.
- PTX JIT to SASS; only PTX interpretation via ProtoGPU.
- Full fatbin format support; start with the clang format used by this repository.

---

## 2) Terminology

- **fatbin**: toolchain-produced container passed to `__cudaRegisterFatBinary*`, containing PTX (and optionally other code).
- **module handle**: opaque pointer returned by `__cudaRegisterFatBinary` (or produced by `__cudaRegisterFatBinaryEnd`).
- **hostFun**: function pointer passed to `__cudaRegisterFunction` and later used as `cudaLaunchKernel(func, ...)`.
- **deviceName**: kernel entry name string passed to `__cudaRegisterFunction` (typically mangled), expected to match PTX `.entry`.
- **device pointer**: value returned by `cudaMalloc`, passed back into kernels as `.u64` params.

---

## 3) Top-level architecture

The shim is a new shared library target that:

1) Exports the required CUDA runtime symbols with a C ABI.
2) Internally embeds and orchestrates ProtoGPU runtime components:
   - PTX parsing/binding (Frontend)
   - PTX ISA mapping & instruction descriptors
   - runtime/memory helpers for host/device buffers and D2H/H2D

### 3.1 Component diagram (logical)

```text
Host Program
  |
  |  (dynamic link)
  v
CUDA Runtime Shim (this design)
  |
  +-- RuntimeContext (singleton, thread-safe)
  |     |
  |     +-- AssetProvider (Env override or Embedded)
  |     +-- FatbinRegistry
  |     +-- KernelRegistry (hostFun -> KernelInfo)
  |     +-- LaunchConfigStack (per-thread)
  |     +-- StreamScheduler (per-stream FIFO)
  |     +-- DeviceMemory (alloc map + memcpy)
  |     +-- ErrorState (per-thread last error)
  |
  +-- ProtoGPU Runtime (gpusim::Runtime)
        |
        +-- Frontend Parser/Binder
        +-- PtxIsaRegistry / DescriptorRegistry
        +-- SimtExecutor / MemUnit / AddrSpaceManager
```

### 3.2 Key integration points in ProtoGPU

MVP uses the in-memory Runtime entrypoint:
- `gpusim::Runtime::run_ptx_kernel_with_args_text_entry_launch(ptx_text, ptxIsaJsonText, instDescJsonText, entry, args, launch)`

Rationale:
- PTX is extracted from fatbin in-memory.
- Assets follow env/embedded policy and are available as JSON text.
- Entry selection is explicit and validated.

---

## 4) Exported ABI & symbol requirements

### 4.1 Exported symbols (MVP)

The shim must export (at least) these symbols (names exact):

- `cudaMalloc`
- `cudaFree`
- `cudaMemcpy`
- `cudaLaunchKernel`
- `cudaDeviceSynchronize`
- `cudaGetLastError`
- `cudaGetErrorString`

- `__cudaRegisterFatBinary`
- `__cudaRegisterFatBinaryEnd`
- `__cudaRegisterFunction`
- `__cudaUnregisterFatBinary`

- `__cudaPushCallConfiguration`
- `__cudaPopCallConfiguration`

### 4.2 Platform packaging expectations

Linux:
- Build a shared object with SONAME compatible with `libcudart.so.12`.
- Ensure symbol versioning is acceptable for the demo (the demo’s undefined symbols show `@libcudart.so.12`).

Windows:
- For MVP focus on the repository’s Linux demo path; Windows naming/ABI mapping is tracked as a follow-up.

---

## 5) RuntimeContext (global state)

`RuntimeContext` is a process-global singleton created on first use.

Responsibilities:
- Own a `gpusim::Runtime` instance configured using `GPUSIM_CONFIG` (env) or embedded config JSON.
- Provide access to loaded assets (PTX ISA JSON, inst_desc JSON) as strings.
- Own registries (fatbin, kernels) and stream scheduler.
- Provide error state helpers.

Thread safety:
- Global mutex protecting registries, stream objects, and device allocation map.
- Per-thread state:
  - last CUDA error
  - launch config stack (push/pop)

---

## 6) Assets policy (env override + embedded fallback)

### 6.1 Inputs

Environment variables:
- `GPUSIM_CONFIG=<path>`
- `GPUSIM_PTX_ISA=<path>`
- `GPUSIM_INST_DESC=<path>`

Behavior:
- If any env var is set, the shim loads that file.
- If an env var is set but the file cannot be read/parsed, fail fast with a clear diagnostic.
- If no env vars are set, use embedded JSON strings.

Definition of “fail fast” for assets:
- The shim reports a clear error (stderr + optional shim logger) at the point of initialization.
- The shim records an internal “fatal init error” and subsequent CUDA API calls return an error (and set last error) rather than continuing with partial/unknown assets.

### 6.2 AssetProvider abstraction

Logical interface:

```text
AssetProvider {
  get_config_json_text(): string
  get_ptx_isa_json_text(): string
  get_inst_desc_json_text(): string
}
```

Implementations:
- `EnvAssetProvider`: reads files from env paths.
- `EmbeddedAssetProvider`: returns compiled-in strings produced at build time.

Note: the shim does not rely on repo-relative default asset paths.

---

## 7) FatbinRegistry (fatbin → PTX)

### 7.1 Responsibilities

- Implement `__cudaRegisterFatBinary*` entrypoints.
- Associate an opaque module handle with:
  - raw fatbin bytes pointer (for diagnostics)
  - extracted PTX text(s)
- Provide PTX text selection at launch time.

### 7.2 Minimum parsing contract

- Support the clang fatbin format used by this repository.
- Extract PTX text byte sequences and store as UTF-8/ASCII strings.
- If extraction fails, fail fast with “unsupported fatbin format” including enough context to debug.

### 7.3 PTX module selection

MVP selection policy:
- If multiple PTX modules are present, select the first module unless a better discriminator is available.

Future policy:
- Prefer PTX matching configured profile/arch if fatbin metadata provides it.

---

## 8) KernelRegistry (hostFun → deviceName + module)

### 8.1 Registration flow

`__cudaRegisterFunction(moduleHandle, hostFun, ..., deviceName, ...)` must:

- Record mapping:
  - key: `hostFun`
  - value: `KernelInfo{ moduleHandleId, deviceName }`

Constraints:
- Registration may occur many times before any launch.
- The same module may register multiple kernels.

### 8.2 Unregistration

`__cudaUnregisterFatBinary(moduleHandle)` must:
- Remove the module from `FatbinRegistry`.
- Remove any associated `hostFun` mappings from `KernelRegistry`.

---

## 9) Launch configuration support

The shim must fully support `<<<grid, block, shared, stream>>>` from the first version.

### 9.1 Preferred path: `cudaLaunchKernel` explicit args

`cudaLaunchKernel(func, gridDim, blockDim, kernelParams, sharedMemBytes, stream)` directly provides:
- gridDim
- blockDim
- sharedMemBytes
- stream

The shim uses these values as the source of truth.

### 9.2 Fallback path: push/pop call configuration

Some compiler paths may use:
- `__cudaPushCallConfiguration(grid, block, shared, stream)`
- later: `__cudaPopCallConfiguration(&grid, &block, &shared, &stream)`

Design:
- Implement per-thread stack semantics:
  - Push returns success code.
  - Pop returns the most recent config or an error if stack empty.

Validation:
- If any grid/block dimension is 0, return an appropriate CUDA error and set last error.

### 9.3 sharedMemBytes propagation

ProtoGPU’s current `gpusim::LaunchConfig` has only:
- `grid_dim`, `block_dim`, `warp_size`

MVP design requirement: sharedMemBytes must be “propagated into the ProtoGPU launch path” even if not consumed.

Logical design approach:
- Introduce a shim-level `LaunchConfigEx`:

```text
LaunchConfigEx {
  grid_dim: Dim3
  block_dim: Dim3
  warp_size: u32
  dynamic_shared_bytes: u32
}
```

- For MVP, the shim:
  - validates `dynamic_shared_bytes` and logs it
  - passes `grid_dim/block_dim/warp_size` to ProtoGPU `LaunchConfig`

Follow-up (recommended):
- Extend `gpusim::LaunchConfig` to include `dynamic_shared_bytes` and thread it into Shared memory allocation.

---

## 10) Streams & ordering

### 10.1 Requirements

- Same stream is FIFO.
- Different streams may interleave.
- `cudaDeviceSynchronize()` waits for all streams.

MVP allows single-threaded execution as long as ordering is correct.

### 10.2 Stream model

- `cudaStream_t` is an opaque handle from the host’s perspective.
- MVP accepts `stream == nullptr` and treats it as the default stream.

Maintain:
- `StreamState { id, queue<Command>, in_flight }`

### 10.3 Command types

- `Memcpy` (H2D/D2H/D2D)
- `KernelLaunch`

### 10.4 Execution policy

MVP simplest correct policy:
- Single global lock around enqueue + execute.
- Execute commands synchronously at enqueue time, but still model the stream queue for diagnostics and future async.

This satisfies:
- same-stream FIFO (trivial)
- device synchronize (drain queues; no-ops if already executed)

---

## 11) Device memory model

### 11.1 Device pointers

- `cudaMalloc` returns a stable value usable as a PTX `.u64` pointer argument.
- Internally, ProtoGPU uses `gpusim::DevicePtr` (a `uint64_t`).

Shim representation (MVP):
- Treat the host-visible `void*` as a direct encoding of ProtoGPU `DevicePtr`.
- Internally, store allocation metadata keyed by `DevicePtr`.

```text
alloc_map: device_ptr(u64) -> { bytes: u64, align: u64 }
```

Rationale:
- Matches the requirement recommendation: device pointers are ProtoGPU `DevicePtr` values; no additional tagging scheme is required.
- Keeps kernel argument packing simple: pointer params are `.u64` and receive the same `DevicePtr` numeric value.

Safety notes:
- This assumes a 64-bit process where `uintptr_t` can represent the `DevicePtr` value.
- All pointer validation is done against `alloc_map` and bounds checks; the pointer is never dereferenced as a host address.

### 11.2 cudaMemcpy

Support at minimum:
- `cudaMemcpyHostToDevice`: write bytes into ProtoGPU global memory at DevicePtr
- `cudaMemcpyDeviceToHost`: read bytes from ProtoGPU global memory
- `cudaMemcpyDeviceToDevice`: copy within ProtoGPU global memory

Validation:
- Ensure source/destination pointer refers to a known allocation.
- Ensure range is within allocation bounds.
- On failure, set last error and print a diagnostic including address and size.

---

## 12) Kernel launch end-to-end flow

### 12.1 `cudaLaunchKernel` flow

Inputs:
- `func` (hostFun)
- `gridDim`, `blockDim`
- `kernelParams` (`void**`)
- `sharedMemBytes`
- `stream`

Steps:

1) Resolve `KernelInfo` by `hostFun`:
   - If missing: return `cudaErrorInvalidDeviceFunction` and set last error.

2) Resolve PTX text from `moduleHandle` via `FatbinRegistry`.

3) Validate kernel entry exists:
   - Confirm PTX contains `.entry <deviceName>`.
   - If missing: fail fast with a diagnostic identifying module + deviceName.

4) Build ProtoGPU launch config:
   - `LaunchConfig{ grid_dim, block_dim, warp_size }`
   - `warp_size` is taken from the loaded `GPUSIM_CONFIG` (ProtoGPU runtime config)

5) Pack args into `gpusim::KernelArgs`:
   - layout from PTX `.param`
   - values from `kernelParams`

6) Enqueue into stream scheduler:
   - `KernelLaunchCommand{ ptx_text, entry=deviceName, args, launch, sharedMemBytes }`

7) Execute (MVP synchronous):
   - Call `gpusim::Runtime::run_ptx_kernel_with_args_text_entry_launch(...)`
   - If ProtoGPU returns a diagnostic, translate to a CUDA error and set last error.

### 12.2 Entry validation mechanics

MVP implementation may do a cheap validation:
- string search for `.entry <deviceName>`

Preferred (future):
- parse PTX module tokens and ensure binder can bind `deviceName`.

---

## 13) Argument packing design

### 13.1 Source of layout

Use ProtoGPU Frontend:
- Parse PTX text → module tokens
- Bind kernel by name (`deviceName`) → `KernelTokens`
- Use `KernelTokens.params` as `KernelArgs.layout`

### 13.2 Packing rule (MVP)

- Param order: use the order in `KernelTokens.params`.
- Blob size: `max(param.offset + param.size)`.
- Endianness: little-endian byte copy.

For each parameter index `i`:
- Host provides `kernelParams[i]` pointing to the argument value storage.
- Copy `param.size` bytes from `kernelParams[i]` into blob at `param.offset`.

Pointer parameters:
- PTX pointer params are `.u64`.
- Host argument is a device pointer token returned from `cudaMalloc`.
- Packing must write the ProtoGPU `DevicePtr` (u64) into blob.

Design for pointer args:
1) Read `void* host_devptr = *reinterpret_cast<void**>(kernelParams[i])`.
2) Look up `host_devptr` in `alloc_map`.
3) Write the mapped `device_ptr` (u64) into blob.
4) If not found, return invalid device pointer error.

Scalar parameters:
- MVP supports at least `u32` and `u64` as represented by `gpusim::ParamType`.
- If PTX contains unsupported param types, fail fast with a clear diagnostic.

Missing args:
- If `kernelParams` is null or does not have enough entries, set last error and fail.

---

## 14) Error model & diagnostics

### 14.1 Last error

Maintain per-thread last error state:
- `cudaGetLastError()` returns the last error and clears it.
- `cudaPeekAtLastError()` (optional) returns without clearing.

### 14.2 Error translation

MVP translation table (logical):
- invalid dims → `cudaErrorInvalidValue`
- missing registration / entry not found → `cudaErrorInvalidDeviceFunction`
- unsupported fatbin format → `cudaErrorInvalidPtx`
- invalid device pointer / memcpy range → `cudaErrorInvalidDevicePointer` (or `cudaErrorInvalidValue` if unavailable)
- ProtoGPU diag present → `cudaErrorLaunchFailure`

### 14.3 Diagnostics

When failing fast, include:
- API name
- kernel `deviceName` (if available)
- failure class:
  - missing registration
  - PTX extraction unsupported
  - entry not found
  - argument packing mismatch
  - memcpy pointer/range invalid

Optional logging:
- gated by env var (e.g. `GPUSIM_CUDART_SHIM_LOG=1`)

---

## 15) Thread safety & lifetime

Requirements:
- Multiple kernels may be registered before launch.
- Prefer simple mutex around shared maps.

Design choices:
- `KernelRegistry`, `FatbinRegistry`, and `alloc_map` are guarded by `RuntimeContext::mutex`.
- Per-thread launch config stack and last-error are stored in thread-local storage.

Unregistration:
- When a module is unregistered, associated hostFun entries are removed.
- Outstanding streams/commands referencing that module are an error; MVP may fail fast if detected.

---

## 16) Validation plan (MVP)

### 16.1 Unit-level validation

- Fatbin parser:
  - given the demo fatbin blob, extract at least one PTX text.
- Kernel registry:
  - register hostFun→deviceName and retrieve.
- Launch config stack:
  - push/pop ordering and empty-pop behavior.
- Arg packer:
  - for a known PTX entry signature, verify blob offsets and pointer translation.

### 16.2 Integration validation

- Run `cuda/demo/demo` with the shim in the dynamic loader path.
- Validate:
  - kernel launch completes
  - D2H data matches expected result
  - `cudaGetLastError()` returns success after successful execution

---

## 17) Open questions / follow-ups

- How to best implement Linux symbol versioning to satisfy `@libcudart.so.12` on the target platform.
- Whether to extend `gpusim::LaunchConfig` to carry `dynamic_shared_bytes` in core, and how Shared memory allocation will consume it.
- Broader fatbin format coverage (nvcc variants, multiple PTX per arch).
- Optional APIs (streams create/destroy, async memcpy, events) if future demos require them.

---

## Appendix A: Requirement traceability

- Required symbols: see §4.1.
- Asset input priority + embedded fallback: see §6.
- Fatbin→PTX extraction + failure behavior: see §7.
- hostFun→deviceName mapping + entry validation: see §8 and §12.2.
- Launch config (grid/block/shared/stream) + push/pop fallback: see §9.
- Argument packing (values from kernelParams, layout from PTX): see §13.
- Device pointer and memory model for malloc/free/memcpy: see §11 and §11.2.
- Error handling + last error + diagnostics: see §14.
- Thread safety + unregister lifetime rules: see §15.
