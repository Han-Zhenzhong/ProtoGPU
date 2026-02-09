# CUDA Runtime Shim Streams Design (`cudaStreamCreate` / `cudaStreamDestroy`)

This document extends the CUDA Runtime shim logical design with support for stream object lifecycle APIs.

It is derived from:
- Requirements: `cuda/docs/cuda-shim-requirement.md`
- Logical design: `cuda/docs/doc_design/cuda-shim-logical-design.md`

Scope of this doc:
- Add `cudaStreamCreate` / `cudaStreamDestroy` so host programs can create non-default streams and pass them into kernel launches.
- Preserve correctness under the shim’s current “single-threaded / synchronous” execution model.

---

## 1) Goals & non-goals

### 1.1 Goals (correctness-first MVP)

- Export CUDA Runtime symbols:
  - `cudaStreamCreate(cudaStream_t* pStream)`
  - `cudaStreamDestroy(cudaStream_t stream)`
- Allow host code to use non-null `cudaStream_t` handles in:
  - `cudaLaunchKernel(..., stream)`
  - (future) `cudaMemcpyAsync(..., stream)`
- Ordering semantics:
  - Operations enqueued to the same `cudaStream_t` execute in program order.
  - `cudaDeviceSynchronize()` waits for completion of all previously enqueued work across all streams.
- Safe lifetime semantics:
  - Destroying a stream that still has queued work is safe under the shim model (it completes work before releasing).

### 1.2 Non-goals (this iteration)

- True asynchronous execution / overlap between streams.
- Events (`cudaEventCreate/Record/Wait`) and stream wait APIs.
- Full “per-thread default stream” behavior.

---

## 2) Current runtime behavior (baseline)

Current shim behavior (as implemented today):

- A per-stream FIFO queue exists conceptually (`StreamScheduler`), but `submit()` executes commands synchronously.
- `cudaLaunchKernel` runs under a process-wide mutex (`RuntimeContext::global_mutex()`), which serializes launches.
- `cudaDeviceSynchronize` drains the scheduler; in the current MVP this is a no-op because submissions execute immediately.

Implication:
- Adding stream creation/destruction mainly enables host code to obtain non-null `cudaStream_t` handles.
- Correctness is preserved because execution is serialized; stream ordering is trivially satisfied.

---

## 3) CUDA stream model we implement

### 3.1 Stream handle types

- `cudaStream_t` is treated as an opaque handle in the shim ABI: `using cudaStream_t = void*;`.
- `nullptr` is the **default stream**.
- Non-default streams are represented as heap-allocated objects and their addresses are used as handles.

Rationale:
- Host code treats streams opaquely; it only passes values back to CUDA APIs.
- Heap addresses are stable for the lifetime of the stream.
- They do not collide with device pointers (device pointers in this project are small numeric values like `0x1000`, while heap pointers are in a different address range).

### 3.2 Default stream semantics

CUDA has nuanced default stream semantics (legacy vs per-thread default stream). For this shim:

- **MVP semantics:** treat `nullptr` as a valid stream key.
- **Conservative correctness:** because launches are serialized (global mutex + synchronous submit), default stream behavior is effectively “legacy-default-like” (i.e., it cannot overlap with other streams).

Future (when async is implemented):
- Explicitly implement **legacy default stream** semantics (default stream synchronizes with all streams) because it is the most conservative and matches many existing expectations.

---

## 4) API semantics

### 4.1 `cudaStreamCreate`

Signature:

```c
cudaError_t cudaStreamCreate(cudaStream_t* pStream);
```

Behavior:
- Validate `pStream != nullptr`.
- Allocate a `Stream` object and return its handle in `*pStream`.
- Initialize per-stream queue state in `StreamScheduler` (optional in MVP; it may lazily create the queue on first submit).

Error handling:
- On allocation failure, return `cudaErrorMemoryAllocation`.
- On invalid value (`pStream == nullptr`), return `cudaErrorInvalidValue`.

Thread-safety:
- Must be safe if called concurrently with other CUDA APIs. Use the `RuntimeContext::global_mutex()` to protect stream registry state.

### 4.2 `cudaStreamDestroy`

Signature:

```c
cudaError_t cudaStreamDestroy(cudaStream_t stream);
```

Behavior:
- Destroying `nullptr` (default stream) returns `cudaSuccess` (CUDA allows destroying the default stream? CUDA typically treats default stream as special; for the shim, this is the least surprising behavior).
- Validate that `stream` was created by this shim (exists in registry).
- Ensure all queued work on that stream completes before releasing resources.
  - In MVP synchronous mode, work is already complete after each submit.
  - Still call `StreamScheduler::drain(stream)` (to be introduced) or a conservative `drain_all()` before deletion.
- Remove the stream from the registry and free its `Stream` object.

Error handling:
- If `stream` is not recognized, return `cudaErrorInvalidValue` (MVP). A more accurate CUDA error is `cudaErrorInvalidResourceHandle`, but this shim currently uses a minimal error enum.

---

## 5) Internal design

### 5.1 New data structures

Add a stream object type under `cuda/src/cudart_shim/`:

```text
struct Stream {
  uint64_t id;
  // Future: flags, priority, capture state, etc.
};
```

Stream registry (owned by `RuntimeContext` or a dedicated singleton):

```text
StreamRegistry {
  mutex
  unordered_map<cudaStream_t, unique_ptr<Stream>> streams
  next_id
}
```

`StreamScheduler` extensions:
- Keep current per-stream FIFO structure.
- Add optional per-stream drain method:

```text
void drain(cudaStream_t stream);
```

MVP implementation can implement `drain(stream)` as a no-op (since submit runs immediately), but having it simplifies correctness of `cudaStreamDestroy` and future async work.

### 5.2 Interaction with existing launch path

Current launch:
- `cudaLaunchKernel(..., stream)` calls `StreamScheduler::submit(stream, cmd)`.

With created streams:
- The handle passed by host code will be the pointer returned by `cudaStreamCreate`.
- `submit()` uses that handle as the key.

### 5.3 Lifetime and ownership

- The shim owns all `Stream` objects it creates.
- The raw pointer value is used as the handle and remains valid until `cudaStreamDestroy`.
- After destroy, the handle becomes invalid; further use returns `cudaErrorInvalidValue`.

---

## 6) Concurrency and ordering

Even with synchronous execution, we explicitly define ordering to match CUDA expectations:

- **Same-stream FIFO:** submissions on the same stream execute in program order.
- **Cross-stream ordering:** no ordering guarantees unless the program synchronizes (device sync, events, etc.).

In the current shim:
- The global mutex makes execution effectively single-threaded, so cross-stream interleaving does not happen.
- This is conservative and should preserve functional correctness.

---

## 7) Diagnostics and observability

Recommended debug env vars (optional):
- `GPUSIM_CUDART_SHIM_LOG_STREAMS=1`:
  - log `cudaStreamCreate` handle + id
  - log `cudaStreamDestroy` handle
  - log submits with stream key

---

## 8) Tests

Add unit/smoke tests under `tests/` or `cuda/src/cudart_shim/` tests:

1) `StreamCreateDestroySmoke`
- `cudaStreamCreate` -> non-null handle
- `cudaStreamDestroy` -> success

2) `StreamOrderingKernelLaunch`
- Create two streams `s0`, `s1`
- Launch kernels into both streams in alternating order
- Because execution is synchronous, results should match a sequential execution model

3) `InvalidHandle`
- Destroy an unknown pointer -> `cudaErrorInvalidValue`

---

## 9) Future work

- `cudaStreamCreateWithFlags`, `cudaStreamCreateWithPriority`
- Async engine:
  - background executor thread(s)
  - `cudaMemcpyAsync` support
  - `cudaStreamSynchronize`
  - events and cross-stream dependencies
- Implement accurate CUDA error codes (extend `cudaError_t` subset)

