# Public Runtime API (C++)

> Chinese version: [public_api.zh-CN.md](public_api.zh-CN.md)

This page is for users who want to embed ProtoGPU into their own project as a C++ library:

- No dependency on the repo directory layout (no need for `assets/...` file paths)
- You can pass **PTX text** and **JSON assets text** (`ptx_isa` / `inst_desc`) directly
- You can use runtime-provided host/device memory and memcpy helpers

Entry header: `include/gpusim/runtime.h`

---

## 1. Two entry styles: file-path vs in-memory

### 1.1 file-path (equivalent to the CLI)

If you already have file-based assets (PTX/JSON on disk), use:

- `Runtime::run_ptx_kernel(...)`
- `Runtime::run_ptx_kernel_with_args(...)`
- `Runtime::run_ptx_kernel_*_entry_launch(...)`

These APIs follow the same semantics as `gpu-sim-cli` flags like `--ptx/--ptx-isa/--inst-desc/--grid/--block`.

### 1.2 in-memory (for packaging/embedding)

If you want to “package PTX and JSON assets inside the program” (strings, fetched over network, read from DB, etc.), use:

- `Runtime::run_ptx_kernel_text(ptx_text, ptx_isa_json_text, inst_desc_json_text)`
- `Runtime::run_ptx_kernel_with_args_text(..., args)`
- and the corresponding `*_launch` / `*_entry_launch` variants

Notes

- These overloads **do not change semantics**: internally it still runs the same Parser → Mapper → Descriptor → SIMT execution chain.
- `*_entry_launch` allows selecting a specific `.entry` within a PTX module.

---

## 2. LaunchConfig (grid/block/warp_size)

### 2.1 Defaults (non-`_launch` variants)

Default launch for `run_ptx_kernel_text(...)` / `run_ptx_kernel_with_args_text(...)`:

- `grid_dim = (1,1,1)`
- `block_dim = (warp_size,1,1)` (warp_size comes from `AppConfig.sim.warp_size`)
- `launch.warp_size = AppConfig.sim.warp_size`

### 2.2 Explicit launch

To control `grid/block` or override warp_size, use the `*_launch` variants.

---

## 3. Kernel parameters (KernelArgs / param blob)

Parameters are passed via `KernelArgs`:

- `KernelArgs.blob`: little-endian bytes, written in the `.param` layout order

Recommended approach

- Keep a small pack helper on the user side (e.g. pack u32/u64/pointer).
- Explicitly populate `KernelArgs.blob` before each run.

Note

- The Runtime clears the param blob on “runs without args” to avoid implicit reuse across runs.

---

## 4. Memory and memcpy helpers

The Runtime provides a minimal set of helpers to make regression tests and embedded demos easier:

- Host buffer: `host_alloc` / `host_write` / `host_read`
- Device global: `device_malloc`
- Copies: `memcpy_h2d` / `memcpy_d2h`

Baseline semantics

- Device memory follows the no-cache + addrspace memory model.
- Out-of-bounds global reads/writes or access to unallocated regions will fail fast (typically surfaced via Diagnostic or exceptions in a few helpers).

---

## 5. Error handling and Diagnostic

All runs return `RunOutputs`:

- `out.sim.completed`: whether execution completed
- `out.sim.diag`: diagnostics on failure (module/code/message and optional source location, etc.)

Suggested calling convention

- **Check** `completed` / `diag` first, then do follow-up reads (e.g. D2H readback).

Common diagnostic codes (not exhaustive)

- `runtime:E_ENTRY_NOT_FOUND`: specified entry name does not exist
- `runtime:E_LAUNCH_DIM` / `runtime:E_LAUNCH_OVERFLOW`: illegal launch dimensions
- `instruction:DESC_NOT_FOUND`: PTX ISA map missing an opcode mapping entry
- `instruction:OPERAND_FORM_MISMATCH` / `frontend:OPERAND_PARSE_FAIL`: operand form mismatch or parse failure
- `simt:E_DESC_MISS`: missing inst_desc semantics for an IR instruction
- `simt:E_DIVERGENCE_UNSUPPORTED`: divergent control flow encountered (unsupported at current milestone)
- `simt:E_MEMORY_MODEL`: config selects an unknown memory model (and strict mode is enabled)

---

## 6. Minimum requirements for asset JSON (ptx_isa / inst_desc)

You must provide both:

- `ptx_isa`: maps PTX forms to an IR opcode (`ir_op`)
- `inst_desc`: provides executable µop semantics for each IR opcode

If you add a new `ir_op` or a new operand form in `ptx_isa`, you must also add the corresponding entry in `inst_desc`, otherwise SIMT execution will hit a descriptor-miss.

---

## 7. Regression entry

The repo provides a regression test that does not depend on any `assets/` paths:

- CTest: `gpu-sim-public-api-tests`

It covers:

- the minimal execution chain for in-memory PTX + in-memory JSON assets
- a parameterized kernel with global writeback + host readback
