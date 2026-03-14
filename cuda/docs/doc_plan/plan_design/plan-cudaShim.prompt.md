# CUDA Runtime Shim (cudart) — Design Plan (ProtoGPU)

## 0) Goal

Build a CUDA runtime shim that lets a host program compiled by clang/nvcc (e.g. `cuda/demo/demo`) run unmodified while redirecting CUDA runtime calls into `ProtoGPU`.

Key acceptance criteria:
- Host binary dynamically links against a shim named like `libcudart.so.12` (platform equivalent) and runs.
- No workload JSON required for kernel launches or argument injection.
- Kernel launch configuration is fully supported from day 1: `<<<grid, block, sharedBytes, stream>>>`.
- Stream semantics are correct at the runtime level:
  - Same-stream work is FIFO.
  - `cudaDeviceSynchronize` waits for all streams.
  - `cudaStreamSynchronize` waits for that stream.
- Kernel entry identity comes from registration callbacks (`__cudaRegisterFunction`) and must be validated against PTX `.entry`.
- Argument values come from host `kernelParams`, but layout comes from PTX `.param`.
- GPU-sim JSON assets are resolved by **(1) env override** else **(2) embedded fallback**; no implicit “default relative path” mode.

## 1) Non-goals (initially)

- No real GPU concurrency / overlap; a single-threaded scheduler is OK.
- No full CUDA Driver API.
- No full fatbin support for all toolchains/versions; start with clang’s format(s) used by this repo.
- No full `cudaMemcpyAsync` overlap; can be synchronous within stream ordering.

## 2) High-level architecture

Implement a shared library target: **CUDA runtime shim**
- Exports the subset of `libcudart` symbols required by the demo and the requirement doc.
- Internally links to `gpusim_core` (or equivalent static/obj libs).

Core subsystems inside shim:
1) **RuntimeContext**: global singleton owning GPU-sim runtime instance, registries, and stream state.
2) **FatbinRegistry**: manages fatbin handles and extracts PTX text (and metadata) from registered fatbins.
3) **KernelRegistry**: maps host function pointers to device kernel name (`deviceName`) and owning fatbin.
4) **LaunchConfigStack**: supports `__cudaPushCallConfiguration` / `__cudaPopCallConfiguration`.
5) **StreamScheduler**: per-stream FIFO queues, command execution, synchronization.
6) **DeviceMemory**: implements `cudaMalloc/cudaFree` and memcpy routing to/from GPU-sim address spaces.
7) **ErrorState**: implements CUDA last error model (`cudaGetLastError`, `cudaPeekAtLastError`, `cudaGetErrorString`).

## 3) Public ABI surface (exported symbols)

Minimum set (as driven by `nm -u` and the requirements doc):

Runtime basics:
- `cudaError_t cudaGetLastError(void)`
- `cudaError_t cudaPeekAtLastError(void)`
- `const char* cudaGetErrorString(cudaError_t)`
- `cudaError_t cudaDeviceSynchronize(void)`

Memory:
- `cudaError_t cudaMalloc(void** devPtr, size_t size)`
- `cudaError_t cudaFree(void* devPtr)`
- `cudaError_t cudaMemcpy(void* dst, const void* src, size_t count, cudaMemcpyKind kind)`
  - (Optional early) `cudaMemcpyAsync` (implemented as stream-enqueued synchronous copy)

Kernel launch:
- `cudaError_t cudaLaunchKernel(const void* func, dim3 gridDim, dim3 blockDim, void** args, size_t sharedMem, cudaStream_t stream)`
- `cudaError_t cudaConfigureCall(dim3 gridDim, dim3 blockDim, size_t sharedMem, cudaStream_t stream)` (if needed by toolchain)
- `cudaError_t cudaSetupArgument(const void* arg, size_t size, size_t offset)` (if needed)
- `cudaError_t cudaLaunch(const void* func)` (if needed)

Registration / fatbin (toolchain-dependent, but expected):
- `void** __cudaRegisterFatBinary(const void* fatCubin)` / or `__cudaRegisterFatBinaryEnd`
- `void __cudaUnregisterFatBinary(void** handle)`
- `void __cudaRegisterFunction(void** handle, const void* hostFun, char* deviceFun, const char* deviceName, int thread_limit, uint3* tid, uint3* bid, dim3* bDim, dim3* gDim, int* wSize)`
- `unsigned __cudaPushCallConfiguration(dim3 gridDim, dim3 blockDim, size_t sharedMem, void* stream)`
- `cudaError_t __cudaPopCallConfiguration(dim3* gridDim, dim3* blockDim, size_t* sharedMem, void* stream)`

Notes:
- Exact signatures must match what clang emits for the host binary you’re targeting; define them by inspecting the binary symbols and (if needed) LLVM’s CUDA runtime ABI expectations.

## 4) Data model & key mappings

### 4.1 FatbinHandle
Represents one registered fat binary.
- `id` (opaque, stable)
- `raw_fatbin_ptr` for diagnostic only
- `ptx_modules[]`: list of `{ptx_text, arch, maybe entry list}`

### 4.2 Kernel registration
On `__cudaRegisterFunction`:
- Key: `hostFun` pointer
- Value:
  - `deviceName` (authoritative kernel name)
  - `fatbin_handle_id`
  - optional `deviceFun` (string) if useful

### 4.3 Launch config stack
`__cudaPushCallConfiguration` pushes `{grid, block, sharedBytes, stream}`.
`__cudaPopCallConfiguration` pops into caller-provided output pointers.

### 4.4 Stream
Maintain a `cudaStream_t` abstraction with:
- numeric id
- FIFO queue of commands
- status: pending / running / done

Default stream:
- `stream == nullptr` maps to stream 0.

## 5) Fatbin → PTX extraction plan

Implement `FatbinRegistry::extract_ptx_texts(fatCubin)`.

Strategy:
1) Detect clang fatbin container format used by current demo.
2) Parse enough to extract embedded PTX text sections.
3) Store PTX text verbatim for feeding into ProtoGPU runtime.

Incremental approach:
- Start with “known-good minimal parser” for the demo’s format.
- Add robust error messages: if parsing fails, report “fatbin format unsupported” and advise rebuilding with PTX emitted to file as a temporary workaround.

Implementation notes:
- Do not depend on `libcuda` or real NVIDIA runtime.
- Keep parser isolated (no GPU-sim coupling) so it can be unit-tested.

## 6) Kernel launch flow (end-to-end)

### 6.1 `cudaLaunchKernel`
Inputs: `func (hostFun)`, `gridDim`, `blockDim`, `kernelParams`, `sharedMem`, `stream`.

Steps:
1) Resolve stream object (default stream if null).
2) Look up `hostFun → deviceName + fatbin_handle`.
   - If missing: set last error to `cudaErrorInvalidDeviceFunction`.
3) Resolve PTX module from fatbin; pick best match by arch profile (initial: first PTX).
4) Validate PTX contains `.entry deviceName`.
5) Ensure GPU-sim runtime is initialized (assets loaded via env/embedded policy).
6) Parse PTX to obtain parameter layout for `deviceName`.
   - Reuse existing parser output structure (`KernelTokens.params` or equivalent).
7) Pack arguments:
   - Values come from `kernelParams[i]` memory on host.
   - Sizes/alignments/offsets come from PTX `.param` layout.
   - Produce `KernelArgs` / arg blob as expected by GPU-sim runtime.
8) Enqueue a “LaunchKernelCommand” into the stream FIFO.
9) Execute per stream scheduler policy:
   - If single-threaded: execute immediately or on `synchronize`.

### 6.2 Execution inside GPU-sim
Reuse existing runtime entrypoints:
- Prefer a “run PTX text with args & entry” method.
- Ensure it can accept:
  - PTX text
  - entry name
  - packed args blob
  - launch dims
  - shared memory size

## 7) Stream & synchronization semantics

### 7.1 Command types
- `MemcpyH2D`
- `MemcpyD2H`
- `MemcpyD2D` (optional)
- `LaunchKernel`
- `EventRecord` / `EventWait` (optional; only if required by demo/tests)

### 7.2 FIFO guarantees
- Commands submitted to the same stream execute in order.
- Different streams may be interleaved in any order unless synchronized.

### 7.3 Synchronization APIs
- `cudaDeviceSynchronize`: drain all stream queues.
- `cudaStreamSynchronize(stream)`: drain that stream queue.

Simplest scheduler:
- Single global mutex.
- Execute commands synchronously when enqueued, but still enforce FIFO per stream.
  - This still satisfies ordering; it just removes overlap.

## 8) Memory model for `cudaMalloc` and memcpy

### 8.1 Device pointers
Create a `DevicePtr` concept owned by shim:
- Stores ProtoGPU global address (e.g., base address in `AddrSpaceManager`).
- Returned to host as an opaque pointer-like value.

### 8.2 Allocation
`cudaMalloc`:
- allocate in ProtoGPU global address space.
- record allocation size; maintain mapping `void* (opaque) → {base, size}`.

`cudaFree`:
- remove mapping; optionally free in ProtoGPU.

### 8.3 Memcpy
- `H2D`: write bytes into ProtoGPU global.
- `D2H`: read bytes from ProtoGPU global.
- `D2D`: global→global copy.

If ProtoGPU global reads are sparse, missing bytes should read as 0 per earlier decision.

## 9) Assets loading policy (env override + embedded fallback)

All required JSON assets:
- PTX ISA json
- instruction descriptor json
- any runtime/module config needed to execute

Policy:
1) If env vars indicate overrides, load those file contents.
2) Else use embedded JSON strings compiled into the shim (or linked object).

Implementation plan:
- Define a single `AssetProvider` interface:
  - `std::string get_ptx_isa_json_text()`
  - `std::string get_inst_desc_json_text()`
  - `std::string get_default_config_json_text()` (if needed)
- Two implementations:
  - `EnvAssetProvider`: reads from env paths.
  - `EmbeddedAssetProvider`: returns compiled-in strings.

## 10) Error model & diagnostics

Implement CUDA-like last error:
- Maintain thread-local (or global if single-thread) `last_error`.
- Every API sets `last_error` on failure and returns same error.

Diagnostics:
- Add a shim-internal logging channel controlled by env var, e.g. `GPUSIM_CUDART_SHIM_LOG=1`.
- Print:
  - fatbin registration events
  - kernel registration mapping
  - PTX module selection
  - `.entry` validation failures
  - arg packing summary (param count, total bytes)
  - memcpy failures and first-missing offset if applicable

## 11) Build & packaging (CMake)

Add a new target (TODO: choose target name, platform-specific output):
- Linux: `libcudart.so.12`
- Windows: `cudart64_120.dll` (or toolchain-expected DLL name)

Link against:
- ProtoGPU core libraries

Export:
- Ensure C ABI symbol visibility for exported cudart functions.

## 12) Test & validation plan

### 12.1 Unit tests
- Fatbin parser:
  - given a known fatbin blob, extract PTX text.
- Kernel registry:
  - register and lookup hostFun → deviceName.
- Launch config stack:
  - push/pop correctness.
- Arg packing:
  - for a small PTX snippet with `.param`, verify produced arg blob matches expected offsets and sizes.

### 12.2 Integration tests
- Build and run `cuda/demo/demo` with shim in dynamic loader path.
- Validate:
  - kernel executes
  - memcpy results match expected host output
  - trace/stats artifacts generated (if enabled)

## 13) Milestones (implementation order)

1) Scaffold shim library target + export minimal error APIs.
2) Implement `__cudaRegisterFatBinary` / `__cudaRegisterFunction` registries.
3) Implement fatbin→PTX extraction for demo’s format.
4) Implement `cudaMalloc/cudaFree/cudaMemcpy` routing to ProtoGPU.
5) Implement `__cudaPush/PopCallConfiguration` + `cudaLaunchKernel`.
6) Reuse GPU-sim PTX parser to derive `.param` layout and pack `kernelParams`.
7) Implement stream FIFO and synchronization APIs.
8) Add embedded assets provider + env overrides.
9) Add tests and wire demo run instructions.
