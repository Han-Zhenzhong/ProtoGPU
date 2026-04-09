## Plan: warp_reduce_add Development Execution

Implement warp_reduce_add as a v1 functional warp-collective instruction in ProtoGPU with deterministic f32 broadcast semantics over participating lanes (active & guard), using a dedicated ExecCore micro-op path and a required .cu inline-PTX integration route for fast end-to-end validation.

**Steps**
1. Phase 0 - Scope freeze and naming contract.
Lock v1 semantics: warp-local only, broadcast writeback, inactive/non-participating lanes unchanged, empty mask no-op, deterministic lane-order accumulation. Freeze canonical tokens warp_reduce_add and WARP_REDUCE_ADD.
2. Phase 1 - Frontend asset wiring.
Update PTX opcode mapping and instruction descriptor assets so parser to IR to uop path emits the new operation with f32 constraints and consistent dst/src roles.
3. Phase 1 - Contract/parser extension.
Extend micro-op enum and descriptor token parser for WARP_REDUCE_ADD; keep fail-fast behavior for unknown tokens and malformed descriptors. Depends on step 1; parallel with step 2 after names freeze.
4. Phase 2 - Execution implementation.
Add dedicated ExecCore branch with two-pass behavior: accumulate participating lanes first, then broadcast writeback to participating lanes only. Depends on steps 2 and 3.
5. Phase 2 - Type enforcement and diagnostics.
Enforce f32-only v1 path and keep mismatch reporting consistent with existing diagnostics contracts. Depends on step 4.
6. Phase 3 - Unit and semantic tests.
Add positive coverage: full mask, partial mask, predication (active & guard), in-place dst==src, empty mask no-op. Add negative coverage: unknown token and malformed descriptor. Depends on steps 4 and 5.
7. Phase 3 - Compiler-adjacent integration path.
Add minimal CUDA demo flow using inline PTX asm and run through shim PTX override; compare to CPU reference and include one predicated/partial-lane case. Depends on steps 2 and 4; parallel with step 6.
8. Phase 4 - Regression and acceptance gate.
Run focused suites first, then shim/integration and broader non-regression suites (including observability/config parse checks).
9. Phase 4 - Documentation closure.
Sync user/dev docs with finalized behavior, inline-asm workflow, deterministic policy, and non-goals.

**Relevant files**
- [cuda/docs/doc_design/warp-reduce-add-logical-design.md](cuda/docs/doc_design/warp-reduce-add-logical-design.md) - design baseline and acceptance expectations.
- [cuda/docs/doc_spec/inst_warp_reduce_add.md](cuda/docs/doc_spec/inst_warp_reduce_add.md) - semantic source-of-truth.
- [assets/ptx_isa/demo_ptx64.json](assets/ptx_isa/demo_ptx64.json) - PTX opcode to IR mapping.
- [assets/inst_desc/demo_desc.json](assets/inst_desc/demo_desc.json) - descriptor and EXEC uop token wiring.
- [include/gpusim/contracts.h](include/gpusim/contracts.h) - MicroOpOp enum extension.
- [src/instruction/descriptor_registry.cpp](src/instruction/descriptor_registry.cpp) - token parser and loader validation behavior.
- [src/units/exec_core.cpp](src/units/exec_core.cpp) - warp_reduce_add execution logic.
- [tests/unit/inst_desc_registry_tests.cpp](tests/unit/inst_desc_registry_tests.cpp) - descriptor/token coverage.
- [tests/unit/builtins_tests.cpp](tests/unit/builtins_tests.cpp) - semantic correctness coverage.
- [tests/unit/simt_predication_controlflow_tests.cpp](tests/unit/simt_predication_controlflow_tests.cpp) - active-mask/predication behavior.
- [cuda/demo](cuda/demo) - inline-asm demo location.
- [tests/integration](tests/integration) - shim end-to-end validation.

**Verification**
1. Descriptor/registry acceptance: warp_reduce_add and WARP_REDUCE_ADD resolve deterministically.
2. Execution correctness: full and partial mask broadcast, in-place correctness, empty-mask no-op.
3. SIMT correctness: participation is exactly active & guard; non-participating lanes unchanged.
4. Integration correctness: inline-PTX .cu demo runs via shim PTX override and matches CPU reference.
5. Regression safety: observability/config parse and related suites remain green.

**Decisions**
- Included in v1: functional f32 semantics, deterministic lane-order accumulation, broadcast to participating lanes.
- Excluded in v1: timing/perf model, leader-only semantics, inter-warp collectives, broad type expansion.
- Compiler decision: inline PTX asm in .cu is required for v1 quick integration; intrinsic/lowering is follow-up.
