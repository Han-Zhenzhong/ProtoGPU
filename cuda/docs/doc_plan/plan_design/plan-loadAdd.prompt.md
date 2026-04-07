## Plan: Add load_add PTX + clang demo

Add a custom PTX opcode load_add.global.u32 with semantics dst = dst + mem[addr], implemented without a new fused micro-op by extending descriptor/expander to support one temporary value channel between uops. Deliver end-to-end proof via a new .cu demo that emits load_add in PTX (inline asm), runs through the CUDA runtime shim, and validates output in CTest-compatible integration flow.

**Steps**
1. Phase 1: Instruction contract and asset wiring
2. Define v1 instruction contract in docs/spec notes: opcode load_add, required modifiers global.u32, operand form (reg, addr), deterministic lane semantics dst = old_dst + load32(addr), alignment/error behavior inherited from existing LD path. This phase blocks all later work.
3. Add PTX ISA mapping entry to assets/ptx_isa/demo_ptx64.json: ptx_opcode=load_add, type_mod=u32, operand_kinds=[reg,addr], ir_op=load_add. Parallel: add schema/doc note if operand form text examples are documented elsewhere.
4. Add descriptor entry in assets/inst_desc/demo_desc.json for opcode=load_add type_mod=u32 with a 2-uop sequence (MEM LD -> EXEC ADD) that references one temporary slot. This depends on Phase 2 descriptor/expander extension.
5. Phase 2: Descriptor/expander temporary channel (no new uop op)
6. Extend descriptor template model to express temporary operand references for uops (for example temp input/output index arrays or reserved negative operand indices), while keeping backward compatibility for existing descriptors. This is the main architectural change.
7. Update descriptor parsing/validation in src/instruction/descriptor_registry.cpp and include/gpusim/instruction_desc.h so strict-key mode still behaves correctly and gives context-rich errors on malformed temp references.
8. Update expansion in src/instruction/expander.cpp to materialize temp placeholders into MicroOp operands so runtime can carry value from LD output into ADD input while preserving original dst register source.
9. Update runtime operand behavior in execution units only as needed for temp operand kind handling (likely contracts model extension in include/gpusim/contracts.h plus read/write handling paths in src/units/mem_unit.cpp and src/units/exec_core.cpp). Keep existing op enums unchanged.
10. Phase 3: Frontend/runtime integration for new opcode
11. Ensure parser/mapper accepts load_add.global.u32 syntax and maps to IR opcode load_add: add mapping-focused tests in tests/ptx_isa_mapper_tests.cpp for success and operand-form mismatch diagnostics.
12. Ensure descriptor lookup and SIMT execution run load_add expansion correctly in single-lane and multi-lane contexts: add/extend SIMT or memory tests (tests/simt_predication_controlflow_tests.cpp and/or tests/memory_no_cache_addrspace_tests.cpp) to verify dst accumulation with initialized register + memory value and predication masking behavior.
13. Verify observability naming remains coherent for emitted uops/events (UOP traces should show LD then ADD) and no control-flow regressions in SIMT loop at src/simt/simt.cpp.
14. Phase 4: clang demo + integration automation
15. Add new CUDA demo source in cuda/demo (for example load_add_demo.cu) using inline asm that emits load_add.global.u32 so generated PTX includes the custom opcode expected by simulator path.
16. Add matching PTX artifact strategy: either checked-in PTX file under cuda/demo or generated PTX during script execution (preferred for reproducibility parity with existing streaming_demo.cu script).
17. Add Linux integration script in scripts (pattern from run_cuda_shim_e2e_streaming_demo_cu.sh) to: compile demo .cu with clang, emit text PTX, run host binary with shim via LD_LIBRARY_PATH + GPUSIM_CUDART_SHIM_PTX_OVERRIDE, assert stdout contains OK, and provide skip behavior for missing toolchain.
18. Register a new CTest target in CMakeLists.txt under UNIX-only integration section, with PASS_REGULAR_EXPRESSION=OK and SKIP_RETURN_CODE aligned with existing shim integration tests.
19. Phase 5: Documentation and acceptance checks
20. Update demo docs in cuda/demo/README.md with build/run commands and expected PTX snippet showing load_add.global.u32.
21. Update top-level or docs references if needed (README.md/docs) so contributors understand this is a simulator-specific PTX extension, not standard PTX atomics.
22. Run focused test set and integration validation; capture pass criteria and known limitations.

**Relevant files**
- /home/hanzz/ProtoGPU/assets/ptx_isa/demo_ptx64.json — add load_add PTX-to-IR mapping entry.
- /home/hanzz/ProtoGPU/assets/inst_desc/demo_desc.json — add load_add descriptor sequence using temp channel.
- /home/hanzz/ProtoGPU/include/gpusim/contracts.h — extend operand representation if temp operand kind is introduced.
- /home/hanzz/ProtoGPU/include/gpusim/instruction_desc.h — extend UopTemplate model for temp references.
- /home/hanzz/ProtoGPU/src/instruction/descriptor_registry.cpp — parse/validate temp fields with strict-key compatibility.
- /home/hanzz/ProtoGPU/src/instruction/expander.cpp — materialize temp operands through uop expansion.
- /home/hanzz/ProtoGPU/src/units/mem_unit.cpp — produce temp output for LD phase when descriptor targets temp.
- /home/hanzz/ProtoGPU/src/units/exec_core.cpp — consume temp input during ADD phase.
- /home/hanzz/ProtoGPU/src/instruction/ptx_isa.cpp — verify mapping path for new opcode and diagnostics.
- /home/hanzz/ProtoGPU/tests/ptx_isa_mapper_tests.cpp — add mapper acceptance/negative tests for load_add.
- /home/hanzz/ProtoGPU/tests/inst_desc_registry_tests.cpp — add parser/strict-key tests for new temp descriptor fields.
- /home/hanzz/ProtoGPU/tests/memory_no_cache_addrspace_tests.cpp — add memory semantics tests for load_add behavior.
- /home/hanzz/ProtoGPU/tests/simt_predication_controlflow_tests.cpp — optional predication/warp behavior regression guard.
- /home/hanzz/ProtoGPU/cuda/demo/load_add_demo.cu — new .cu demo with inline asm load_add.
- /home/hanzz/ProtoGPU/scripts/run_cuda_shim_load_add_demo_cu.sh — new end-to-end script for clang PTX generation + shim run.
- /home/hanzz/ProtoGPU/CMakeLists.txt — add integration test registration.
- /home/hanzz/ProtoGPU/cuda/demo/README.md — add usage docs and constraints.

**Verification**
1. Unit parse/descriptor checks: run gpu-sim-ptx-isa-mapper-tests and gpu-sim-inst-desc-tests to validate new opcode mapping and descriptor temp parsing.
2. Runtime behavior checks: run gpu-sim-memory-tests and gpu-sim-simt-tests to ensure no regression and load_add semantics hold under predication/warp contexts.
3. Integration check: run ctest target for new shim load_add demo on Linux; assert OK output and valid skip semantics when CUDA toolchain is absent.
4. Manual PTX inspection: verify generated PTX from load_add_demo.cu contains load_add.global.u32 in kernel body.
5. Optional contract check: run existing shim override tests to ensure multi-PTX override behavior unchanged.

**Decisions**
- Confirmed scope: custom load_add opcode only (not atom.add alias in v1).
- Confirmed execution model: descriptor expansion path, no new fused MicroOpOp in v1.
- Confirmed demo requirement: full end-to-end execution under shim with success assertion.
- Confirmed v1 narrowing: support only load_add.global.u32.
- Confirmed implementation strategy: add implicit temporary channel in descriptor/expander to make LD+ADD expansion semantically correct.
- Excluded from v1: shared/local/const variants, non-u32 types, atomics ordering semantics, and hardware-accurate latency modeling.

**Further Considerations**
1. Temp encoding choice: explicit temp_in/temp_out arrays are clearer and safer than overloading negative operand indices; recommended for maintainability and strict schema validation.
2. Demo artifact policy: generating PTX in script avoids stale checked-in PTX drift; recommended to mirror existing streaming demo flow.
3. Future extension path: if more fused memory-arithmetic ops are planned, generalize temp channel to support multiple temporary slots early to avoid repeated format churn.
