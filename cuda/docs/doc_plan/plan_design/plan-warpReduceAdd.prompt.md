## Plan: warp_reduce_add Instruction Design

Add a new warp-level collective instruction `warp_reduce_add dst, src` with broadcast semantics over active lanes, implemented as a dedicated warp-collective micro-op path (not as per-lane ADD chaining), and validated with focused descriptor/exec/SIMT tests. This aligns with the provided spec and existing simulator architecture where instruction metadata is data-driven but semantic behavior is implemented in execution units.

**Steps**
1. Phase 1 - Semantics freeze and scope lock: lock first version semantics to `f32` + broadcast-to-active-lanes + inactive-lanes-unchanged + warp-local scope only; explicitly treat participating lanes as `exec_mask = warp.active & uop.guard`; define non-goals (timing/pipeline/perf modeling) and compatibility with current SIMT divergence semantics.
2. Phase 1 - Opcode and naming decision: choose stable internal names for IR opcode and micro-op op string (for example IR `warp_reduce_add`, micro-op `WARP_REDUCE_ADD`) and keep naming consistent across PTX ISA mapping, descriptor assets, parser mapping, and execution switch; record string constants as a compatibility boundary for future assets.
3. Phase 2 - Compiler-surface and quick integration path: define how developers generate `warp_reduce_add` from CUDA sources for early validation, with two tracks: (a) short-term fast path using `.cu` inline PTX asm that emits the target opcode directly, and (b) longer-term compiler-lowering path (clang/NVVM builtin/intrinsic or custom lowering) for non-inline-asm sources. For v1 acceptance, require track (a) to be documented and testable end-to-end.
4. Phase 2 - Frontend/descriptor asset wiring: add PTX-ISA mapping entry in `assets/ptx_isa` so the frontend can emit IR opcode `warp_reduce_add`; add instruction descriptor entry in `assets/inst_desc` with operand kinds `[reg, reg]`, type modifier constraints for v1 (`f32` only), and one EXEC uop using the new warp-collective op. This step depends on Steps 2-3.
5. Phase 2 - Contract and loader support: extend `MicroOpOp` enum with the new op and update descriptor registry string parser (`parse_uop_op`) to accept the new `WARP_REDUCE_ADD` token; keep loader fail-fast behavior for unknown op strings. This step depends on Step 4.
6. Phase 3 - Execution semantics in ExecCore: implement a dedicated `MicroOpOp::WarpReduceAdd` execution branch in `ExecCore::step` with two-pass logic: (a) accumulate sum over participating lanes only, (b) write identical result to destination register of participating lanes only; preserve inactive lanes and lanes masked out by guard; keep behavior deterministic under same mask and inputs. This step depends on Step 5.
7. Phase 3 - Type handling policy for v1: enforce `f32` input/output path explicitly (or guarded by existing type metadata/attrs) to avoid silent integer reinterpretation; define expected behavior for NaN/Inf as normal IEEE float add sequence under deterministic lane traversal; postpone multi-type expansion (`u32/s32/f64/...`) to later revision.
8. Phase 4 - Unit tests: add or extend targeted tests for (a) descriptor registry loads new op, (b) exec semantics full warp broadcast, (c) partial active-mask reduction, (d) predicated guard reduction (`warp.active & uop.guard`), (e) in-place form `dst==src`, (f) empty participating mask as no-op.
9. Phase 4 - Negative/error tests: add coverage that unknown uop op string still fails in loader and malformed descriptor shape is rejected; ensure no regression in existing descriptor lookup behavior.
10. Phase 5 - End-to-end compiler/shim validation: add a minimal `.cu` demo kernel using inline PTX `asm` for `warp_reduce_add`, compile it with the existing clang+CUDA path, and execute through the CUDA Runtime shim using `GPUSIM_CUDART_SHIM_PTX_OVERRIDE`; verify numerics against a CPU reference and confirm lane-mask behavior in at least one predicated case.
11. Phase 5 - Observability and contracts check: verify the new op integrates without breaking existing event/error contracts (especially unsupported-uop paths now avoided for this opcode) and ensure output contracts stay stable.
12. Phase 5 - Documentation/update set: update instruction docs/spec references and any architecture/design notes for warp-collective op support; include semantic examples for full and partial mask cases, the inline-asm bring-up workflow, and explicitly state excluded timing model.
13. Phase 6 - Regression run and acceptance gate: execute focused tests first (inst-desc, builtins/exec, simt), then full relevant suites including the shim demo/integration path; accept only if no regressions and new semantics pass deterministic checks.

**Parallelism and dependencies**
1. Can run in parallel after Step 2: compiler-surface drafting (Step 3), asset wiring (Step 4), and test design drafting (part of Step 8).
2. Blocks on contracts: Step 5 blocks Step 6.
3. Can run in parallel after Step 6: positive tests (Step 8), negative tests (Step 9), and `.cu` inline-asm integration wiring (Step 10).
4. Final gate: Step 13 depends on Steps 8-12.

**Relevant files**
- `/home/hanzz/ProtoGPU/assets/ptx_isa/demo_ptx64.json` - add PTX opcode to IR opcode mapping for `warp_reduce_add`.
- `/home/hanzz/ProtoGPU/assets/inst_desc/demo_desc.json` - add descriptor entry and EXEC uop mapping to new op string.
- `/home/hanzz/ProtoGPU/include/gpusim/contracts.h` - extend `MicroOpOp` enum (and related contract types if needed).
- `/home/hanzz/ProtoGPU/src/instruction/descriptor_registry.cpp` - extend `parse_uop_op` mapping and preserve fail-fast behavior.
- `/home/hanzz/ProtoGPU/src/units/exec_core.cpp` - implement warp-collective reduction execution path.
- `/home/hanzz/ProtoGPU/tests/unit/inst_desc_registry_tests.cpp` - validate descriptor parsing/lookup for the new op.
- `/home/hanzz/ProtoGPU/tests/unit/builtins_tests.cpp` - add semantic execution tests for writeback correctness.
- `/home/hanzz/ProtoGPU/tests/unit/simt_predication_controlflow_tests.cpp` - add active-mask/predication interaction coverage.
- `/home/hanzz/ProtoGPU/cuda/demo/` - add a minimal `.cu` demo that emits `warp_reduce_add` via inline PTX asm for fast bring-up.
- `/home/hanzz/ProtoGPU/tests/integration/` - add/extend integration checks that run the new demo through the CUDA Runtime shim path.
- `/home/hanzz/ProtoGPU/cuda/docs/doc_spec/inst_warp_reduce_add.md` - source specification; keep synchronized with implementation constraints and v1 type scope.

**Verification**
1. Run descriptor-focused tests to confirm schema/registry acceptance and lookup determinism for `warp_reduce_add`.
2. Run execution semantic tests validating full-mask broadcast and partial-mask behavior, including in-place form.
3. Run SIMT predication/control-flow tests validating `exec_mask`-scoped participation and unchanged non-participating lanes.
4. Compile and run a `.cu` inline-PTX demo through the CUDA Runtime shim (`GPUSIM_CUDART_SHIM_PTX_OVERRIDE`) to validate compiler-adjacent integration quickly.
5. Run config parse and observability contract tests to ensure no collateral regressions.
6. Run targeted binaries/suites in `build/` first, then broader integration suite if required by project gate.

**Decisions**
- Included in v1: functional semantics only (no cycle/perf model), broadcast variant, active-lane-only reduction, deterministic behavior, `f32` first.
- Excluded in v1: leader-only variant, inter-warp/block reduction semantics, hardware cost/timing model, broad multi-type rollout.
- Compiler integration decision: v1 uses `.cu` inline PTX asm as the shortest validated path; dedicated frontend/compiler intrinsic lowering is tracked as a follow-up deliverable.
- Architectural choice: dedicated warp-collective micro-op execution path instead of lowering to per-lane temp-based sequences, consistent with existing descriptor constraints and cleaner semantic correctness.

**Further Considerations**
1. Type rollout strategy after v1: Option A keep `f32` only until stability; Option B add `u32/s32` next with explicit overflow policy; Option C add all declared types with expanded test matrix.
2. Deterministic accumulation order policy: Option A fixed increasing lane index (recommended); Option B implementation-defined order with weaker reproducibility guarantees.
3. PTX surface naming: Option A expose internal `warp_reduce_add` in custom ISA assets now; Option B add compatibility alias to expected PTX-style spelling in mapping layer.
4. Compiler generation path after quick bring-up: Option A keep inline PTX asm only for experimental use; Option B add a helper macro/wrapper API in demo utilities; Option C add true compiler builtin/lowering and deprecate raw asm usage.
