# CUDA Runtime Shim Multi-GPU Scheduling — Requirements Analysis

This document analyzes requirements for extending the CUDA Runtime shim to support **multi-GPU** (multi-device) host programs and to enable **multi-device scheduling semantics** on top of gpu-sim.

It complements:
- Base shim requirements: `cuda/docs/cuda-shim-requirement.md`
- Logical design: `cuda/docs/doc_design/cuda-shim-logical-design.md`
- Streams lifecycle design: `cuda/docs/doc_design/cuda-shim-streams-design.md`

---

## 1) Problem statement

Today the shim behaves as a **single global device**:
- One process-wide `RuntimeContext`
- One `gpusim::Runtime`
- One `DeviceMemory` allocation map
- Streams are keyed by `cudaStream_t` but executed synchronously

This is sufficient for `cuda/demo/demo`, but host programs that use multiple devices (e.g. `cudaSetDevice(0/1)` and launch work on both) cannot be modeled correctly.

We need a clear definition of:
- what “multi-GPU scheduling” means in this project,
- what CUDA runtime surface is required,
- what correctness guarantees are expected,
- and what the minimal increment is that unlocks real workloads.

---

## 2) Definitions

- **Device**: a CUDA-visible GPU instance selected by `cudaSetDevice(deviceId)`.
- **Default device**: device 0 if a program never calls `cudaSetDevice`.
- **Multi-GPU scheduling**: host-visible semantics where work submitted to different devices targets different simulated runtimes and their queues progress independently (optionally in parallel).
- **Correctness** in this doc is about **functional results + ordering** (not performance).

---

## 3) Goals / non-goals

### 3.1 Goals (phased)

**G0 — Device identity & isolation (minimum viable multi-device)**
- Expose basic device selection APIs so host code can target multiple devices.
- Maintain strict isolation:
  - device memory allocations are per-device
  - streams are per-device
  - module/kernel registration is per-device (or has a consistent cross-device policy)

**G1 — Multi-device progress / scheduling semantics**
- Operations enqueued on device A must not accidentally execute on device B.
- `cudaDeviceSynchronize()` must synchronize only the *current device* (match CUDA behavior).
- `cudaStreamSynchronize(stream)` (future) synchronizes that stream’s device.

**G2 — Optional parallelism**
- Allow devices to progress concurrently (e.g. a worker thread per device) as an optional mode.
- Determinism controls: ability to run devices in a deterministic serial mode.

### 3.2 Non-goals (initial implementation)

- Unified Virtual Addressing (UVA) and peer access (`cudaDeviceEnablePeerAccess`) semantics.
- Multi-device cooperative groups / clusters.
- CUDA graphs.
- Full driver/runtime parity.

---

## 4) User stories (what must work)

### 4.1 Basic multi-device
1. A program calls `cudaGetDeviceCount` and sees `N >= 2`.
2. It calls `cudaSetDevice(0)`, allocates memory, launches kernels, copies back results.
3. It calls `cudaSetDevice(1)`, repeats the same.
4. Results are correct and isolated (device 0 output doesn’t overwrite device 1 output).

### 4.2 Mixed device scheduling
1. Create stream(s) on device 0 and device 1.
2. Launch kernels to each device.
3. Synchronize each device independently.
4. Optional: run both devices concurrently if enabled.

---

## 5) Required CUDA Runtime API surface (proposed minimum)

### 5.1 Device APIs
- `cudaError_t cudaGetDeviceCount(int* count)`
- `cudaError_t cudaGetDevice(int* device)`
- `cudaError_t cudaSetDevice(int device)`
- `cudaError_t cudaDeviceSynchronize(void)` (already present; must become per-device)

Optional but commonly needed soon:
- `cudaError_t cudaDeviceReset(void)`
- `cudaError_t cudaGetDeviceProperties(cudaDeviceProp* prop, int device)` (can be stubbed with minimal fields)

### 5.2 Stream APIs (device-scoped)
- `cudaStreamCreate/cudaStreamDestroy` (already planned)

Optional next:
- `cudaStreamSynchronize(cudaStream_t stream)`

### 5.3 Memory APIs (device-scoped)
Existing APIs become per-device implicitly:
- `cudaMalloc/cudaFree`
- `cudaMemcpy` (host<->device per current device)

Optional next:
- `cudaMemcpyAsync(..., stream)`

---

## 6) Core semantic requirements

### 6.1 Per-thread current device
- CUDA maintains a per-host-thread “current device”.
- The shim must maintain TLS state:

```text
thread_local int current_device = 0;
```

### 6.2 Per-device contexts
Introduce `DeviceContext` indexed by deviceId:

- `gpusim::Runtime` instance (with its own `AppConfig`)
- `DeviceMemory` allocator map
- registries:
  - `FatbinRegistry`
  - `KernelRegistry`
  - (future) `StreamRegistry`
- per-device `StreamScheduler`

### 6.3 Default stream and stream ownership
- Default stream (`nullptr`) is per-device.
- Non-default streams must be associated with a device at creation time.
- Passing a stream created on device A while current device is B must be rejected (or coerced in a clearly documented way).

Recommended correctness policy:
- Reject with `cudaErrorInvalidValue` (or a more accurate code if added later).

### 6.4 Registration semantics across devices
CUDA runtime registration callbacks (`__cudaRegisterFatBinary`, `__cudaRegisterFunction`) can occur before any explicit device selection.

We must choose and document one policy:

**Policy R0 (simplest): register to “current device” (TLS)**
- Registration goes into `DeviceContext[current_device]`.
- Risk: static constructors may register before user calls `cudaSetDevice`, causing all modules to land on device 0.

**Policy R1 (conservative): register globally, instantiate per-device on first use**
- Keep a global fatbin/module store.
- On first launch on device D, lazily “materialize” module state for that device.
- This matches typical CUDA behavior where code is available on all devices.

Recommended for multi-device usability: **R1**.

### 6.5 Device pointer identity
We must prevent device pointers from different devices colliding.

Two options:

**P0: Per-device address spaces (no tagging)**
- Each `DeviceContext` has its own allocator starting at e.g. `0x1000`.
- Pointers are only meaningful within a device context.
- If user mixes pointers across devices, detect and error.

**P1: Tagged pointers**
- Encode deviceId into high bits of the returned pointer.
- Allows fast “belongs to which device?” checks.

Recommended MVP: **P0** + explicit validation.

---

## 7) Scheduling model requirements

### 7.1 Correctness ordering
- Within a device:
  - same-stream FIFO ordering
  - device sync waits for all streams
- Across devices:
  - no ordering guarantees unless host synchronizes explicitly (e.g. sync both devices)

### 7.2 Execution modes
Define two modes:

**Mode S0: Serial multi-device (deterministic)**
- All commands execute synchronously on call sites.
- Simplest correctness model.

**Mode S1: Per-device worker threads (optional)**
- Each device has a worker that drains its queues.
- Requires additional synchronization in the shim.

Requirement: implement S0 first; S1 is follow-up.

---

## 8) Observability and diagnostics

Required diagnostics:
- Every API that is device-scoped must include deviceId in error messages when relevant.
- Add debug env vars:
  - `GPUSIM_CUDART_SHIM_LOG_DEVICE=1` (prints current device changes)
  - `GPUSIM_CUDART_SHIM_LOG_STREAMS=1` (stream create/destroy + device)

---

## 9) Risks and open questions

- How many devices should the shim report via `cudaGetDeviceCount`?
  - Option: configurable via `GPUSIM_DEVICE_COUNT`, default 1.
- Are per-device configs identical or can they differ?
  - Option: allow `GPUSIM_CONFIG_DEVICE0`, `...DEVICE1` later.
- Module registration policy (R0 vs R1) impacts compatibility.
- Future support for peer access/UVA will require pointer tagging or a shared address space model.

---

## 10) Acceptance criteria (for initial multi-device milestone)

A “multi-device smoke” program should pass:
- `cudaGetDeviceCount` returns `>= 2` when configured
- `cudaSetDevice(0)` + allocate/launch/copy -> correct
- `cudaSetDevice(1)` + allocate/launch/copy -> correct
- Cross-device misuse is detected:
  - using a device0 pointer on device1 launch fails with a clear error

---

## 11) Implementation sketch (non-normative)

- Add TLS `current_device`.
- Add `DeviceManager` singleton:
  - owns `vector<DeviceContext>`
  - lazily constructs device contexts
- Modify existing exports:
  - `cudaMalloc/cudaFree/cudaMemcpy/cudaLaunchKernel/cudaDeviceSynchronize` to route through `DeviceManager::current()`.
- Add exports:
  - `cudaGetDeviceCount/cudaGetDevice/cudaSetDevice`.

This document defines requirements only; detailed design choices should be captured in a follow-up design doc once scope is confirmed.
