# CUDA Runtime Shim（libcudart.so.12）Dev Plan（MVP for cuda/demo/demo）

Inputs:
- Requirements: `cuda/docs/cuda-shim-requirement.md`
- Logical design: `cuda/docs/doc_design/cuda-shim-logical-design.md`
- Dev guide: `cuda/docs/doc_dev/cuda-shim-dev.md`

Target outcome:
- `cuda/demo/demo` runs against ProtoGPU via a shim library named `libcudart.so.12` (Linux MVP).
- No workload JSON involved.

---

## 0) Constraints & acceptance gates

Hard requirements (must hold before calling MVP “done”):
- All required symbols in the requirement doc are exported.
- Assets policy: env override else embedded fallback; env-var points to unreadable file must fail fast.
- Kernel identity: hostFun→deviceName from `__cudaRegisterFunction` is authoritative; PTX must contain `.entry deviceName`.
- Launch config support is complete from day 1: `grid/block/shared/stream`.
- Arg packing uses PTX `.param` layout; values come from host `kernelParams`.
- Device pointers are ProtoGPU `DevicePtr` numeric values; no tagging.
- Stream semantics: same-stream FIFO; `cudaDeviceSynchronize` drains all.

Non-goals (MVP):
- True overlap/concurrency.
- Full CUDA API coverage beyond required symbols.

---

## 1) Milestone M0 — Skeleton target + ABI plumbing

### Deliverables
- New folder `src/cudart_shim/` with a minimal buildable shared library.
- Export stubs for all required symbols; they compile and link.
- A minimal internal ABI header `src/cudart_shim/cuda_abi_min.h` (no dependency on real CUDA headers).

### Steps
1) Add shim CMake target to root `CMakeLists.txt`:
   - `add_library(cudart-shim SHARED ...)`
   - `target_link_libraries(... PRIVATE gpusim_core)`
   - Set output name/soname to satisfy `libcudart.so.12` lookup.
2) Add `src/cudart_shim/exports.cpp` exporting:
   - `cudaMalloc/cudaFree/cudaMemcpy`
   - `cudaLaunchKernel`
   - `cudaDeviceSynchronize/cudaGetLastError/cudaGetErrorString`
   - `__cudaRegisterFatBinary/__cudaRegisterFatBinaryEnd/__cudaRegisterFunction/__cudaUnregisterFatBinary`
   - `__cudaPushCallConfiguration/__cudaPopCallConfiguration`
3) Add `error_state.{h,cpp}`:
   - thread-local last error
   - minimal `cudaGetErrorString` table for shim-produced errors

### Validation gate
- Build succeeds.
- `nm -D` on the produced library shows all exported symbols.

---

## 2) Milestone M1 — AssetProvider + RuntimeContext init (env/embedded)

### Deliverables
- `RuntimeContext` singleton with:
  - `fatal_init_error` latch
  - loaded asset JSON strings
  - constructed `gpusim::Runtime`
- Asset loading policy exactly per requirement.

### Steps
1) Implement `assets_provider.{h,cpp}`:
   - `EnvAssetProvider`: reads files indicated by env vars.
   - `EmbeddedAssetProvider`: returns embedded strings.
2) Add embedded assets generation:
   - CMake generates `src/cudart_shim/assets_embedded.cpp` from:
     - `assets/configs/demo_config.json`
     - `assets/ptx_isa/demo_ptx64.json` (or the correct default)
     - `assets/inst_desc/demo_desc.json` (or the correct default)
3) Implement `runtime_context.{h,cpp}`:
   - loads config JSON (env/embedded)
   - constructs `gpusim::Runtime(AppConfig)`
   - exposes accessors for asset JSON texts

### Validation gate
- With no env vars set, shim initializes using embedded assets.
- With `GPUSIM_*` set to invalid file path, first call that requires init fails fast with a clear diagnostic and sets last error.

---

## 3) Milestone M2 — DeviceMemory (cudaMalloc/cudaFree/cudaMemcpy)

### Deliverables
- Working `cudaMalloc`, `cudaFree`, `cudaMemcpy` for H2D/D2H/D2D.
- Allocation map keyed by `DevicePtr` numeric value.

### Steps
1) Implement `device_memory.{h,cpp}`:
   - `cudaMalloc`: `DevicePtr p = rt.device_malloc(bytes, align)`; return `reinterpret_cast<void*>(static_cast<uintptr_t>(p))`.
   - Track `{bytes, align}` in `alloc_map[p]`.
   - `cudaFree`: delete from map; (if ProtoGPU exposes device free later, call it; otherwise keep map-only for MVP).
2) Implement `cudaMemcpy`:
   - Decode device pointers back to `DevicePtr`.
   - Validate allocation exists and copy range in-bounds.
   - Route copy through ProtoGPU runtime memory helpers.

### Validation gate
- New unit test (or small harness) allocates device memory, writes H2D, reads D2H, compares bytes.

---

## 4) Milestone M3 — Registries: fatbin + kernel mapping

### Deliverables
- `__cudaRegisterFatBinary*` produces a stable module handle.
- `__cudaRegisterFunction` records `hostFun -> {moduleId, deviceName}`.
- `__cudaUnregisterFatBinary` deletes module and associated function mappings.

### Steps
1) Implement `fatbin_registry.{h,cpp}`:
   - allocate module id
   - store extracted PTX text(s)
2) Implement `kernel_registry.{h,cpp}`:
   - map key: `hostFun` pointer
   - value: `KernelInfo{moduleId, deviceName}`
3) Implement exported entrypoints:
   - `__cudaRegisterFatBinary`
   - `__cudaRegisterFatBinaryEnd` (toolchain-specific; implement once the host expects it)
   - `__cudaRegisterFunction`
   - `__cudaUnregisterFatBinary`

### Validation gate
- Add shim logging (`GPUSIM_CUDART_SHIM_LOG=1`) showing:
  - module registered
  - kernel registered (hostFun pointer + deviceName)
  - module unregistered

---

## 5) Milestone M4 — Fatbin → PTX extraction (clang demo format)

### Deliverables
- Extract PTX text from the demo’s clang-produced fatbin.

### Steps
1) Capture a real fatbin blob:
   - Add temporary debug option: dump the `fatCubin` memory region passed to `__cudaRegisterFatBinary` to a file.
   - Record size heuristics carefully (format dependent).
2) Implement extraction:
   - Start with a robust “PTX sniff” approach:
     - scan for `.version` markers
     - extract until a plausible PTX terminator (end-of-text / next marker)
   - Store each extracted PTX module string.
3) Add diagnostics:
   - if no PTX found: “unsupported fatbin format”

### Validation gate
- Set `GPUSIM_CUDART_SHIM_DUMP_PTX=...` (optional) and verify dumped PTX contains expected `.entry` names.

---

## 6) Milestone M5 — Launch configuration stack + cudaLaunchKernel

### Deliverables
- `__cudaPushCallConfiguration` / `__cudaPopCallConfiguration` implemented as thread-local stack.
- `cudaLaunchKernel` uses explicit args and supports fallback to push/pop if needed.
- Validate dimensions; propagate `sharedMemBytes` into shim-level `LaunchConfigEx` and log it.

### Steps
1) Implement `launch_config_stack.{h,cpp}`:
   - thread-local vector
2) Implement `cudaLaunchKernel` skeleton:
   - resolve `KernelInfo` by hostFun
   - resolve PTX for module
   - validate `.entry deviceName` exists
   - build `gpusim::LaunchConfig` from grid/block + warp_size from config
   - enqueue/execute via stream scheduler (MVP sync)

### Validation gate
- Bad dims return error and set last error.
- Missing kernel registration returns invalid device function.

---

## 7) Milestone M6 — Arg packing from PTX `.param`

### Deliverables
- `kernelParams` values packed into `gpusim::KernelArgs` using PTX-derived layout.

### Steps
1) Implement `kernel_args_pack.{h,cpp}`:
   - parse PTX text tokens via ProtoGPU Frontend
   - bind kernel by `deviceName`
   - build `KernelArgs.layout` from `KernelTokens.params`
   - allocate blob size `max(offset+size)`
   - for scalar params: copy `param.size` bytes from `kernelParams[i]`
   - for pointer params: read host `void*` value, decode to `DevicePtr`, validate in `alloc_map`, write little-endian u64
2) Integrate into `cudaLaunchKernel`.

### Validation gate
- Smoke case: demo kernel writes to global memory via pointer arg; host reads back correct value.
- Wrong arg type/invalid pointer fails fast with clear error.

---

## 8) Milestone M7 — Streams (FIFO ordering) + cudaDeviceSynchronize

### Deliverables
- Per-stream queue model (even if executed synchronously).
- Correct ordering within stream; `cudaDeviceSynchronize` drains all.

### Steps
1) Implement `stream_scheduler.{h,cpp}`:
   - map `cudaStream_t` to `StreamState`
   - enqueue `Memcpy` and `KernelLaunch` commands
   - MVP: execute immediately under a global lock
2) Implement `cudaDeviceSynchronize`:
   - drain all (or no-op if immediate execution is used)

### Validation gate
- Two operations in same stream preserve order.
- `cudaDeviceSynchronize` returns success when all work done.

---

## 9) Milestone M8 — Demo integration + stabilization

### Deliverables
- Documented command sequence to build + run demo against shim.
- Optional: minimal ld version script if `@libcudart.so.12` symbol versioning is required on the target.

### Steps
1) Add a `cuda/docs/doc_user/` note or extend `cuda/demo/README.md` with:
   - how to build shim
   - how to run demo with `LD_LIBRARY_PATH`
   - env var overrides for assets
2) If needed, add `src/cudart_shim/libcudart.map` and link flags.

### Validation gate
- `cuda/demo/demo` runs end-to-end and produces expected output.

---

## Appendix A — Task checklist (for PR breakdown)

- [x] CMake: `cudart-shim` shared library target
- [x] Minimal CUDA ABI types header
- [x] ErrorState (last error + error string)
- [x] AssetProvider (env + embedded)
- [x] Embedded asset generation (CMake; emits `${build}/generated/cudart_shim/assets_embedded.cpp`)
- [x] RuntimeContext (init + fatal latch)
- [x] DeviceMemory (malloc/free/memcpy + validation)
- [x] LaunchConfigStack (push/pop)
- [x] FatbinRegistry (handle + PTX store)
- [x] KernelRegistry (hostFun→deviceName)
- [ ] TODO: Robust fatbin → PTX extraction/decoding for non-plain-text PTX blobs (tokenized/compressed variants)
- [x] Entry validation
- [x] Arg packing via PTX `.param`
- [x] StreamScheduler + DeviceSynchronize
- [x] Demo run docs + (optional) version script

---

## Post-MVP TODOs

Parallel streams (async execution):
- TODO(cuda-shim-streams): Decide default-stream semantics (legacy vs per-thread default stream) and document.
- TODO(cuda-shim-streams): Add background execution in `StreamScheduler` (per-stream worker or shared pool) with clean shutdown.
- TODO(cuda-shim-streams): Implement `cudaStreamCreate` / `cudaStreamDestroy`.
- TODO(cuda-shim-streams): Implement `cudaStreamSynchronize` / `cudaStreamQuery`.
- TODO(cuda-shim-streams): Optional — implement `cudaMemcpyAsync` + ordering tests.

Multi-GPU scheduling:
- TODO(cuda-shim-multi-gpu): Implement `cudaGetDeviceCount` / `cudaGetDevice` / `cudaSetDevice` (thread-local current device).
- TODO(cuda-shim-multi-gpu): Split into per-device `DeviceContext` (runtime + memory + registries).
- TODO(cuda-shim-multi-gpu): Enforce pointer ownership (device pointer only valid on allocating device).
- TODO(cuda-shim-multi-gpu): Make streams device-scoped; define behavior for cross-device stream usage.
