## Plan: cvt Development Execution

Implement `cvt` in a narrow, testable slice driven by clang-generated PTX used by the split-source warp-reduce demo.

### Steps
1. Freeze v1 surface: support only `cvt.rn.f32.u32` (`reg <- reg`).
2. Wire frontend assets:
- Add PTX ISA entry in `assets/ptx_isa/demo_ptx64.json`.
- Add descriptor entry in `assets/inst_desc/demo_desc.json`.
 - Use `type_mod: u32` for `cvt.rn.f32.u32` because current parser keeps the trailing type token.
3. Wire contracts/parser glue:
- Ensure `MicroOpOp::Cvt` exists and `CVT` token maps in descriptor parser.
- Keep `%fN` parsing for float registers in frontend parser.
4. Implement execution:
- Add `ExecCore` branch for `MicroOpOp::Cvt` with lane-masked conversion and writeback.
- Add stable observability counter.
5. Add tests:
- Descriptor lookup test for `CVT`.
- Builtins/exec tests for successful `u32->f32` and unsupported type error.
6. Validate end-to-end:
- Run `scripts/run_cuda_shim_warp_reduce_add_demo_cu.sh`.
- Fix next missing form if surfaced, without broadening scope unnecessarily.

### Done Criteria
1. No `DESC_NOT_FOUND` on `cvt` during demo script run.
2. New tests pass.
3. Existing warp_reduce_add flow remains green.
