# warp_reduce_add Logical Design

## 1. Goal

Define a concrete implementation design for a new warp-collective instruction:

```asm
warp_reduce_add dst, src;
```

v1 target behavior:
- Warp-local scope only.
- Broadcast reduction result to participating lanes.
- Participating lanes are defined by:
  - active warp lanes
  - instruction guard/predicate
- v1 type scope is f32 only.

This design follows the spec in `cuda/docs/doc_spec/inst_warp_reduce_add.md` and the execution plan in `cuda/docs/doc_plan/plan_design/plan-warpReduceAdd.prompt.md`.

## 2. Non-Goals (v1)

- No timing or pipeline model.
- No throughput/latency modeling.
- No inter-warp or block-wide collectives.
- No leader-only writeback mode.
- No broad multi-type support (`u32/s32/f64/...`) in v1.

## 3. Semantics Freeze

### 3.1 Participation mask

At execution time:

```text
exec_mask = warp.active_mask & uop.guard_mask
```

Only lanes in `exec_mask` contribute source values and receive writeback.

### 3.2 Computation and writeback

For each warp instruction instance:
1. Accumulate `sum = add_f32(src[i])` for all lanes `i` in `exec_mask`.
2. Write the same `sum` into `dst[i]` for all lanes `i` in `exec_mask`.
3. Lanes not in `exec_mask` remain unchanged.

### 3.3 Empty participation mask

If `exec_mask` is empty:
- No reduction is performed.
- No destination lane writeback occurs.
- Instruction is effectively a no-op.

### 3.4 Determinism policy

Accumulation order is fixed to increasing lane index to guarantee deterministic results for identical inputs and masks.

## 4. Compiler Surface and Program Generation

## 4.1 Fast integration path (required in v1)

Use `.cu` inline PTX asm to emit `warp_reduce_add` directly.

Example pattern:

```cpp
__device__ __forceinline__ float warp_reduce_add_inline_asm(float x) {
    float out;
    asm volatile("warp_reduce_add %0, %1;" : "=f"(out) : "f"(x));
    return out;
}
```

This path is the shortest route to end-to-end validation through:
- clang CUDA compilation
- PTX emission
- CUDA Runtime shim loading path
- ProtoGPU frontend + execution

## 4.2 Longer-term compiler path (post-v1)

Track a compiler lowering path where source warp-reduction idioms or a dedicated intrinsic map to `warp_reduce_add` without requiring inline asm.

Potential options:
- NVVM/LLVM intrinsic-based lowering.
- PTX transformation pass from shuffle-chain idioms.

Not required for v1 acceptance.

## 5. Frontend and Descriptor Wiring

## 5.1 PTX ISA mapping asset

File: `assets/ptx_isa/demo_ptx64.json`

Add opcode mapping so frontend parser can emit IR opcode `warp_reduce_add`.

Compatibility boundary:
- Keep opcode spelling stable once published in assets.
- If aliases are introduced later, preserve canonical mapping.

## 5.2 Instruction descriptor asset

File: `assets/inst_desc/demo_desc.json`

Add descriptor entry for `warp_reduce_add` with:
- Operand kinds: `[reg, reg]` (`dst, src`).
- Type constraints: `f32` only in v1.
- Micro-op sequence: one EXEC uop with op token `WARP_REDUCE_ADD`.

Loader behavior remains fail-fast for malformed descriptor shape or unknown uop token.

## 6. Contracts and Loader Changes

## 6.1 Micro-op enum extension

File: `include/gpusim/contracts.h`

Add:
- `MicroOpOp::WarpReduceAdd`

## 6.2 Descriptor parser string mapping

File: `src/instruction/descriptor_registry.cpp`

Extend `parse_uop_op` mapping:
- string `WARP_REDUCE_ADD` -> `MicroOpOp::WarpReduceAdd`

Preserve existing behavior:
- Unknown op string remains a hard error.

## 7. Execution Core Design

File: `src/units/exec_core.cpp`

Add dedicated execution branch for `MicroOpOp::WarpReduceAdd`.

Two-pass algorithm per warp:

```text
exec_mask = warp.active & guard
if exec_mask == 0:
    return success (no-op)

sum = 0.0f
for lane in lane_id_ascending:
    if lane in exec_mask:
        sum += read_f32(src, lane)

for lane in lane_id_ascending:
    if lane in exec_mask:
        write_f32(dst, lane, sum)
```

Design notes:
- Do not lower into chained per-lane ADD uops.
- Preserve lane values outside `exec_mask`.
- Support in-place form (`dst == src`) naturally via two-pass read-then-write.

## 8. Type Policy (v1)

`f32` is the only accepted type in v1.

Behavioral details:
- NaN/Inf follow normal IEEE float addition behavior.
- No implicit integer reinterpretation.
- Type mismatch should surface as frontend/descriptor validation failure or execution diagnostic (depending on where type checks exist in current pipeline).

## 9. Error and Diagnostics Model

- Unknown uop op string: loader failure (existing fail-fast path).
- Malformed descriptor entry: loader failure.
- Unsupported runtime op path should not trigger for correctly wired `warp_reduce_add`.

Observability requirement:
- Existing trace/stats contracts must remain stable.
- No new required output fields for v1.

## 10. End-to-End Integration Path

## 10.1 Minimal `.cu` demo

Add a focused CUDA demo that:
- Uses inline asm to emit `warp_reduce_add`.
- Runs one or more kernels covering:
  - full participation
  - partial participation (predication/mask effect)

## 10.2 Shim execution

Run the compiled demo through CUDA Runtime shim with PTX override:

```bash
export GPUSIM_CUDART_SHIM_PTX_OVERRIDE=<path-to-demo-ptx>
```

Validate kernel outputs against CPU reference calculations.

## 11. Test Plan

## 11.1 Unit and semantic tests

- `tests/unit/inst_desc_registry_tests.cpp`
  - New uop token parses and resolves.
  - Descriptor lookup deterministic for new opcode.

- `tests/unit/builtins_tests.cpp`
  - Full-mask broadcast writeback.
  - Partial-mask reduction.
  - In-place form (`dst == src`).
  - Empty mask no-op.

- `tests/unit/simt_predication_controlflow_tests.cpp`
  - Participation uses `warp.active & uop.guard`.
  - Non-participating lanes remain unchanged.

## 11.2 Negative tests

- Unknown uop string still fails loader.
- Malformed descriptor shape is rejected.
- No regression in existing descriptor registry behavior.

## 11.3 Integration tests

Under `tests/integration/` add/extend cases to:
- Build/run inline-asm `.cu` demo.
- Execute via shim + PTX override.
- Assert numerical equivalence to expected reduction outputs.

## 12. Acceptance Criteria

The feature is accepted when all are true:
1. Parser/descriptor/contract changes load and resolve `warp_reduce_add` correctly.
2. Execution semantics match broadcast-over-participating-lanes behavior.
3. `f32` semantics deterministic under fixed lane order.
4. Inline-asm `.cu` path runs end-to-end through shim and validates against reference outputs.
5. Existing tests (config parse, observability contracts, and related suites) show no regression.

## 13. Rollout Sequence

1. Freeze semantics and names.
2. Add compiler-surface quick path documentation and demo skeleton.
3. Wire PTX ISA + descriptor assets.
4. Add contract enum + descriptor parser mapping.
5. Implement ExecCore branch.
6. Add unit/negative tests.
7. Add shim integration coverage.
8. Run targeted suites then broader regression gate.

## 14. Future Work

- Add type variants (`u32/s32/f64/...`) with explicit overflow/precision policy.
- Explore leader-only variant as optional mode.
- Add compiler intrinsic/lowering path to reduce inline-asm dependency.
- Consider PTX spelling aliases if needed for external compatibility.
