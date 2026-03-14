## Plan: Introduce PTX `cvt` for Clang-Generated Demo Paths

Implement minimum viable `cvt` support needed by the split-source `warp_reduce_add` demo pipeline, while keeping behavior deterministic and fail-fast for unsupported conversions.

### Scope (v1)
1. Parse and map PTX opcode `cvt` for forms required by current generated PTX.
2. Support execution semantics for `cvt.rn.f32.u32 dst, src`.
3. Preserve existing error behavior for unsupported forms.
4. Add focused unit coverage for descriptor lookup and exec semantics.

### Non-Goals (v1)
1. Full PTX `cvt` matrix support.
2. Saturating/rounding mode variants beyond currently emitted forms.
3. Performance/timing modeling.

### Required Deliverables
1. PTX ISA mapping entry for `cvt` with `f32` type-mod and `reg,reg` operands.
2. Instruction descriptor entry expanding to EXEC `CVT` uop.
3. `ExecCore` `MicroOpOp::Cvt` implementation for `u32 -> f32` bit-correct writeback.
4. Tests for happy-path conversion and unsupported-type diagnostics.
5. End-to-end validation via `scripts/run_cuda_shim_warp_reduce_add_demo_cu.sh`.

### Acceptance Criteria
1. Split-source demo no longer fails with `DESC_NOT_FOUND` for `cvt`.
2. `cvt.rn.f32.u32` produces expected float bit patterns.
3. Existing suites for unrelated ops remain unaffected.
