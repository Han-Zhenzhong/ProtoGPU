# gpu-sim-cli (command line)

> Chinese version: [cli.zh-CN.md](cli.zh-CN.md)

`gpu-sim-cli` is the minimal runnable entry point: it loads config/descriptor/PTX, executes the simulation, and writes trace/stats.

## Arguments

- `--workload <file>`: run a WorkloadSpec (streams/commands) JSON (mutually exclusive with single-kernel mode)
- `--ptx <file>`: PTX input file path
- `--ptx-isa <file>`: PTX ISA mapping JSON path (**required**: PTX instruction form → `ir_op` + `operand_kinds`)
- `--inst-desc <file>`: instruction descriptor JSON (inst_desc, IR semantics library: `ir_op → uops`)
  - Compatible alias: `--desc <file>`
- `--config <file>`: runtime config JSON path
- `--trace <file>`: trace output path (jsonl)
- `--stats <file>`: stats output path (json)
- `--io-demo`: enable the minimal end-to-end Kernel I/O + ABI demo (see below)
- `--grid x,y,z`: set 3D gridDim (default `1,1,1`)
- `--block x,y,z`: set 3D blockDim (default `<warp_size>,1,1`; warp_size comes from `config.sim.warp_size`)

## Default inputs

When running from the repo root, the defaults are:

- PTX: `assets/ptx/demo_kernel.ptx`
- PTX ISA map: `assets/ptx_isa/demo_ptx64.json`
- Instruction descriptor (inst_desc): `assets/inst_desc/demo_desc.json`
- Config: `assets/configs/demo_config.json`

## Typical run

```bash
./build/gpu-sim-cli \
  --ptx assets/ptx/demo_kernel.ptx \
  --ptx-isa assets/ptx_isa/demo_ptx64.json \
  --inst-desc assets/inst_desc/demo_desc.json \
  --config assets/configs/demo_config.json \
  --grid 1,1,1 \
  --block 32,1,1 \
  --trace out/trace.jsonl \
  --stats out/stats.json
```

## Multi-SM parallel execution (06.02)

`gpu-sim-cli` supports a baseline “one host thread per SM” parallel execution mode. Configuration and limitations:

- [sm_parallel_execution.md](sm_parallel_execution.md)

## Modular architecture selection (10: profile/components)

If you want to switch CTA/warp scheduling strategies and memory model selectors by configuration only, see:

- [modular_hw_sw_mapping.md](modular_hw_sw_mapping.md)

Tip: the trace writes a one-time `RUN_START` event, which lets you observe which profile/components were selected for a run.

## Output format contract (trace/stats) (v1 baseline)

trace (`--trace`)

- Output is JSONL (one JSON object per line).
- Line 1 is always `TRACE_HEADER`: includes metadata like `format_version/schema/profile/deterministic` (for versioning/backward compatibility).
- Then comes the event stream (current Tier‑0 minimal set includes: `RUN_START`/`FETCH`/`UOP`/`COMMIT`).
- Compatibility note: `RUN_START.extra` is still a *stringified JSON* field (double-layer JSON); script-dependent action names (like `RUN_START`) remain stable.

Extra events for SIMT divergence/reconvergence (M5) (`EventCategory::Ctrl`)

- `SIMT_SPLIT`: divergent `bra` splits taken/fallthrough paths
- `SIMT_MERGE`: a path reaches reconv point and merges mask into the join frame
- `SIMT_POP`: pop an empty path frame (e.g. active mask becomes empty)

stats (`--stats`)

- Output is a single JSON object.
- Top-level fields: `format_version/schema/profile/deterministic/counters` (additive only; do not remove existing fields).

## Public Runtime API (C++)

If you want to embed gpu-sim as a C++ library (especially for the “in-memory PTX + in-memory JSON assets” packaging style), see:

- [public_api.md](public_api.md)

That document includes:

- conventions for in-memory overloads like `Runtime::run_ptx_kernel_text(...)`
- `KernelArgs` / param blob usage
- host/device memory and memcpy helpers

## Common diagnostic codes (troubleshooting quick index)

Failures are typically surfaced as `Diagnostic{module, code, message, ...}` (or thrown as exceptions in a few helpers). Common ones include:

- `runtime:E_ENTRY_NOT_FOUND`: entry name does not exist (no such `.entry` in the PTX module)
- `runtime:E_LAUNCH_DIM` / `runtime:E_LAUNCH_OVERFLOW`: illegal grid/block dimensions or multiplication overflow
- `instruction:DESC_NOT_FOUND`: `--ptx-isa` is missing a mapping entry for an opcode
- `instruction:DESC_AMBIGUOUS`: multiple ISA entries match the same PTX instruction (make `operand_kinds/type_mod` more specific)
- `frontend:OPERAND_PARSE_FAIL`: operand tokens cannot be parsed as the expected `operand_kinds`
- `simt:E_DESC_MISS`: `--inst-desc` is missing semantics for an IR instruction (`ir_op.type_mod(operand_kinds...)`)
- `simt:E_RECONV_MISS`: a divergent `bra` is missing a reconvergence point (CFG/ipdom analysis failed or unsupported control flow)
- `simt:E_RECONV_INVALID`: invalid reconv point / CFG (e.g. out-of-range branch target, broken join frame)
- `simt:E_MEMORY_MODEL`: config selects an unknown memory model (and `allow_unknown_selectors=false`)

## WorkloadSpec (`--workload`: streams/commands)

Purpose

- Describe replayable inputs via a JSON file: buffers/modules/streams/commands.
- Supports multi-stream FIFO and event dependencies via `event_record` / `event_wait`; the trace can replay the command lifecycle: `cmd_enq/cmd_ready/cmd_submit/cmd_complete`.

Docs

- Abstract design: `doc_design/modules/07_runtime_streaming.md`, `doc_design/modules/07.01_stream_input_workload_spec.md`
- Implementation design: `doc_dev/modules/07_runtime_streaming.md`, `doc_dev/modules/07.01_stream_input_workload_spec.md`

Example

```bash
./build/gpu-sim-cli \
  --config assets/configs/demo_config.json \
  --workload assets/workloads/smoke_single_stream.json \
  --trace out/workload.trace.jsonl \
  --stats out/workload.stats.json
```

Mutual exclusivity

- When `--workload` is provided, single-kernel arguments are forbidden: `--ptx/--ptx-isa/--inst-desc/--grid/--block/--io-demo`.

WorkloadSpec v0 (current implementation)

- buffers
  - `buffers.host[name]`: `{ bytes, init? }`, where init supports: `zeros`, `{hex: "..."}`, `{file: "path"}`
  - `buffers.device[name]`: `{ bytes, align? }`
- modules
  - `modules[name]`: `{ ptx, ptx_isa, inst_desc }`
- streams
  - `streams[name].commands[]`: oneof: `copy/kernel/event_record/event_wait/sync`

Supported commands (v0 baseline)

- `copy.kind`: currently only `H2D` and `D2H` are supported (`D2D/MEMSET` errors)
- `kernel`: must include `module/entry/grid_dim/block_dim/args`
  - `grid_dim/block_dim` support two forms: `{x,y,z}` or `[x,y,z]`
  - `args` oneof: `{u32: N}` / `{u64: N}` / `{ptr_device: "dev_buf_name"}` / `{bytes_hex: "..."}`
- `event_record/event_wait`: `{ event: "NAME" }` (event names deterministically map to EventId)
- `sync {}`: explicit sync point (currently a barrier/no-op, but appears in the trace command lifecycle)

Workload schema

- `schemas/workload.schema.json` exists as the field-level contract.
- The current implementation uses structured validation + semantic reference validation, not a generic JSON Schema engine.

Error handling (baseline)

- JSON parse/structure invalid: `runtime:E_WORKLOAD_SCHEMA`
- Referencing missing buffer/module/event: `runtime:E_WORKLOAD_REF`
- Kernel entry not found: `runtime:E_ENTRY_NOT_FOUND`
- Missing args / type mismatch: `runtime:E_BAD_ARGS`
- Illegal launch dims: `runtime:E_LAUNCH_DIM` / `runtime:E_LAUNCH_OVERFLOW`
- No stream head is ready (often due to waiting on an event that is never recorded): `runtime:E_WORKLOAD_DEADLOCK`

Notes (current limitations)

- The runtime/streaming “engines layering + async tick” is still evolving; WorkloadSpec v0 currently executes synchronously while enforcing FIFO + event dependency semantics.
- When running kernels via `--workload`, modules are loaded and executed using `modules[name]` paths (`ptx/ptx_isa/inst_desc`); to ensure reproducibility, bind these paths explicitly in the workload.

## 3D kernel launch semantics (grid/block)

`--grid` and `--block` control the execution domain for a kernel launch:

- `grid_dim = (grid.x, grid.y, grid.z)`: number of CTAs (blocks)
- `block_dim = (block.x, block.y, block.z)`: threads per CTA is `block.x * block.y * block.z`

SIMT deterministically enumerates CTA/warp/lane and sets `active_mask` for some warps:

- If `threads_per_block` is not a multiple of `warp_size`, the last warp only activates the first `threads_per_block % warp_size` lanes.

Example

```bash
# 2 CTAs; each CTA has 40 threads (2 warps, where the 2nd warp has only 8 active lanes)
./build/gpu-sim-cli --grid 2,1,1 --block 40,1,1
```

Error handling (baseline)

- Any dimension is 0: `runtime:E_LAUNCH_DIM`
- `block.x * block.y * block.z` multiplication overflow: `runtime:E_LAUNCH_OVERFLOW`

## Builtins (e.g. %tid.x / %ctaid.x)

This project supports builtins as an operand kind (`special`). Current baseline supports:

- `%tid.{x,y,z}`, `%ntid.{x,y,z}`, `%ctaid.{x,y,z}`, `%nctaid.{x,y,z}`
- `%laneid`, `%warpid`

Notes

- `%laneid/%warpid` are scalar builtins (no `.x/.y/.z` suffix).
- Whether you can “execute through to builtin reads” depends on whether your `--ptx-isa` / `--inst-desc` include the corresponding form (e.g. `mov.u32 (reg, special)`).
- The demo assets include a minimal form: `mov.u32 %r0, %tid.x;` can be mapped and executed.

## PTX op → IR op mapping (`--ptx-isa`)

As a user, you only need to care about “what PTX instructions look like (baseline is PTX 6.4) and which generic IR opcode they map to”. This is handled by the PTX ISA map provided via `--ptx-isa`:

- `ptx_opcode`: PTX instruction name (e.g. `mov/add/ld/ret`)
- `type_mod`: type modifier (e.g. `u32/u64`; empty string means wildcard)
- `operand_kinds`: expected operand-kind sequence for this PTX form (e.g. `reg/imm/symbol/addr/pred`)
- `ir_op`: mapped IR opcode (execution semantics then come from `--inst-desc`, which expands to µops)

Example: the same PTX opcode `mov.u32` may have multiple forms, but they can all map to the same IR opcode `mov`:

```json
{
  "ptx_opcode": "mov",
  "type_mod": "u32",
  "operand_kinds": ["reg", "imm"],
  "ir_op": "mov"
}
```

```json
{
  "ptx_opcode": "mov",
  "type_mod": "u32",
  "operand_kinds": ["reg", "symbol"],
  "ir_op": "mov"
}
```

The mapper parses tokens according to `operand_kinds`:

- `mov.u32 %r0, 0;` → matches `(reg, imm)`
- `mov.u32 %r0, foo;` → matches `(reg, symbol)`

If multiple entries match the same PTX instruction, it reports `DESC_AMBIGUOUS` and lists the candidate signatures in the message (`(operand_kinds) -> ir_op.type_mod`), so you can refine the PTX ISA map until exactly one entry matches.

Note: `--ptx-isa` only determines PTX→IR mapping; execution semantics come from `--inst-desc` (IR opcode → µops). If you introduce a new `ir_op` or operand form in `--ptx-isa`, you must also provide matching semantics in inst_desc.

## Kernel I/O demo (`--io-demo`)

Purpose

- Validate: `.param` argument input → kernel writes global memory → host explicitly reads back via D2H

Run

```bash
./build/gpu-sim-cli --ptx assets/ptx/write_out.ptx --io-demo
```

Expected

- Console prints: `io-demo u32 result: 42`
- `out/trace.jsonl` and `out/stats.json` are generated

## Tests (scripts/)

For quick unit/integration regression, use the repo-root script entry points:

- [scripts.md](scripts.md)
