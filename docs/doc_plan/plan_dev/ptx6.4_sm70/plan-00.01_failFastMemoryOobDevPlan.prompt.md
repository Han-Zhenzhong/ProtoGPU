## Plan: Fail-fast Memory OOB Dev Plan

把 [docs/doc_dev/ptx6.4_sm70/00.01_fail_fast_memory_oob.md](../../../doc_dev/ptx6.4_sm70/00.01_fail_fast_memory_oob.md) 的“global OOB/未分配必须报错”冻结口径，落成一份可执行的 dev plan：明确内存模型与执行层的双重防线、诊断码期望、以及回归夹具与断言的稳定写法，确保未来不会回归到“静默读 0/静默写成功”。

### Steps 5
1. 建立计划落点：新增 [docs/doc_plan/plan_dev/ptx6.4_sm70/00.01_fail_fast_memory_oob_dev_plan.md](00.01_fail_fast_memory_oob_dev_plan.md)。
2. 冻结可观测结果：在计划中写死“未分配/越界 global 访问 → `completed=false` 且必须返回 `Diagnostic`”，并将主要诊断码锚定为 `E_GLOBAL_MISS`（对齐检查仍可产生 `E_GLOBAL_ALIGN`）。
3. 明确内存模型防线：约束 [include/gpusim/memory.h](../../../../include/gpusim/memory.h) 与 [src/memory/memory.cpp](../../../../src/memory/memory.cpp) 的 `AddrSpaceManager::{alloc_global,read_global,write_global}` 必须用 allocation ranges 拒绝任何越界区间（read 返回 `std::nullopt` / write 返回 `false`）。
4. 明确执行层转译：约束 [src/units/mem_unit.cpp](../../../../src/units/mem_unit.cpp) 在 `ld.global.*` / `st.global.*` 路径中把对齐失败映射到 `E_GLOBAL_ALIGN`，把 read/write miss 映射到 `E_GLOBAL_MISS`，并立即 fail-fast 返回 Diagnostic。
5. 固化回归资产：保持 [tests/fixtures/ptx/mem_oob_st_global_f32.ptx](../../../../tests/fixtures/ptx/mem_oob_st_global_f32.ptx) 的写法使用 `[%rd+imm]` 以避免依赖未纳入 bring-up 的 `add.u64 (reg,reg,imm)`；同时在 [tests/tiny_gpt2_minimal_coverage_tests.cpp](../../../../tests/tiny_gpt2_minimal_coverage_tests.cpp)（`test_oob_st_global_must_fail_fast()`）断言 `!completed && diag.has_value()`，并检查诊断码（优先 `E_GLOBAL_MISS`）。

### Further Considerations 2
1. 让 OOB fixture 保持“对齐但越界”，避免被 `E_GLOBAL_ALIGN` 抢先触发而掩盖 OOB 行为。
2. 明确双层防御是强制的：即使未来新增 memcpy/engine 路径，也不能绕过 allocation-range 检查与诊断转译。
