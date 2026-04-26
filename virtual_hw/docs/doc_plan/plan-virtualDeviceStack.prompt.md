## Plan: Virtual HW Dev Plan

Implement a minimal but sound virtual GPU stack in phases: freeze a v1 UAPI first, then build a simple blocking driver path, then add a broker that invokes ProtoGPU through the in-memory API, then add mmap-backed buffers, diagnostics, and an example app. Keep ProtoGPU unchanged and treat PTX plus asset blobs as user-supplied payloads.

**Steps**
1. Phase 1 - Freeze the v1 object model and UAPI. Define context, buffer object, module, launch, and job objects; choose whether v1 uses separate `SUBMIT_MODULE` plus `SUBMIT_LAUNCH` or a single blocking `SUBMIT_JOB`; define versioning, handle semantics, and the error-code space. This blocks all implementation.
2. Phase 1 - Specify exact payload representation. Standardize how PTX text, config JSON, PTX ISA JSON, inst-desc JSON, entry names, and kernel arguments are encoded in submissions. Recommended v1: text blobs for PTX and assets and typed arg descriptors translated by the broker into ProtoGPU `KernelArgs`. This depends on step 1.
3. Phase 1 - Define the broker transport contract. Choose the initial driver-to-broker transport for bring-up and the steady-state transport. Recommended path: start with a simple control path suitable for one-job-at-a-time execution, while designing the record layout so it can later map onto a shared command/completion ring without changing job semantics. This depends on steps 1-2.
4. Phase 2 - Implement the minimal driver shell. Add `/dev/protogpu0`, per-open context lifetime, minimal `ioctl` dispatch, handle tables, and blocking job completion plumbing. Start with one in-flight job per context and reject submissions when the broker is unavailable. This depends on steps 1-3.
5. Phase 2 - Implement buffer object allocation and mapping. Add `ALLOC_BO`, `FREE_BO`, and `mmap` support for input/output buffers; validate ranges and flags in the driver; define how the broker resolves BO handles into shared payload memory. This can begin in parallel with late step 4 once handle semantics are frozen, but it blocks launch testing.
6. Phase 2 - Implement the broker daemon skeleton. Add broker startup, driver connection, job fetch loop, payload reconstruction, status reporting, and completion signaling. Initially support only the minimal blocking execution path. This depends on steps 1-4 and integrates with step 5 for mapped buffers.
7. Phase 3 - Implement ProtoGPU API invocation in the broker. Translate module payloads and launch descriptors into ProtoGPU API inputs; build `LaunchConfig`; convert typed arg descriptors into `KernelArgs`; call `gpusim::Runtime::run_ptx_kernel_with_args_text_entry_launch(...)`. This depends on step 6.
8. Phase 3 - Wire result movement and diagnostic retrieval. Copy output bytes into mapped BOs, store per-job status, expose `WAIT_JOB`, `QUERY_JOB`, and `READ_DIAG`, and define optional trace/stats retrieval as either immediate payloads or deferred job outputs. This depends on steps 5-7.
9. Phase 4 - Add example user program and bring-up tests. Write a minimal C or C++ sample that opens the device, allocates buffers, submits PTX and assets, launches a kernel, waits, and validates output. Add focused tests for invalid handles, bad dims, missing entry names, broker absence, and ProtoGPU diagnostic propagation. This depends on steps 4-8.
10. Phase 4 - Evolve toward queue-based execution without changing UAPI semantics. Once the blocking path is stable, add multi-job submission, command/completion rings, and poll or eventfd notifications while preserving the same object model and error contracts. This depends on step 8 and is excluded from v1 bring-up.

**Relevant files**
- `/home/hanzz/ProtoGPU/virtual_hw/docs/doc_design/virtual_device_stack.md` — source architecture and scope boundaries for the virtual hardware stack
- `/home/hanzz/ProtoGPU/virtual_hw/docs/doc_plan/` — target location for the eventual workspace plan document
- `/home/hanzz/ProtoGPU/src/runtime/runtime.cpp` — broker-side ProtoGPU execution entrypoint via `Runtime::run_ptx_kernel_with_args_text_entry_launch(...)`
- `/home/hanzz/ProtoGPU/docs/doc_user/cli.md` — confirms the CLI exists but should remain bring-up-only, not the steady-state broker boundary
- `/home/hanzz/ProtoGPU/virtual_hw/driver/` — expected home for the independent Linux driver implementation
- `/home/hanzz/ProtoGPU/virtual_hw/broker/` — expected home for the broker daemon implementation

**Verification**
1. Review the UAPI plan and confirm the v1 object model: context, BO, module, launch, job, and diag objects.
2. Validate that the chosen kernel arg representation can be deterministically translated into ProtoGPU `KernelArgs` for at least one scalar-only kernel and one pointer-argument kernel.
3. Bring up a single blocking job path end-to-end: open device, alloc BOs, mmap, submit PTX/assets, launch, wait, read output, validate result.
4. Exercise failure paths: broker unavailable, invalid BO handle, out-of-range buffer access, missing PTX entry, illegal launch dimensions, and ProtoGPU descriptor or ISA failures.
5. Confirm that the broker uses the API path rather than shelling out to `gpu-sim-cli` in the steady-state implementation.
6. After v1 is stable, benchmark whether the transport contract can support a later command/completion ring without breaking the initial UAPI.

**Decisions**
- Included scope: direct app-to-driver programming model, driver resource ownership, broker-mediated execution, ProtoGPU API invocation, mmap-backed buffers, basic waits, and diagnostics.
- Excluded scope: CUDA runtime compatibility, in-kernel PTX or JSON handling, DRM-grade scheduling or VM, production multi-process isolation, and advanced queueing in v1.
- Recommended v1 simplification: prefer a single blocking `SUBMIT_JOB` if that materially reduces bring-up cost; split into `SUBMIT_MODULE` and `SUBMIT_LAUNCH` only if module reuse is needed immediately.
- Recommended transport strategy: keep the first transport simple, but design job records so they can later be carried over a shared ring with minimal semantic churn.

**Further Considerations**
1. Decide whether v1 should optimize for simplicity or module reuse. Recommendation: single blocking `SUBMIT_JOB` first; add reusable module objects later.
2. Decide whether assets are passed as text blobs or paths. Recommendation: text blobs for determinism and to keep the driver/broker contract self-contained.
3. Decide how much of trace and stats retrieval belongs in v1. Recommendation: diagnostics are mandatory in v1; trace and stats can be optional job outputs if they slow bring-up.
