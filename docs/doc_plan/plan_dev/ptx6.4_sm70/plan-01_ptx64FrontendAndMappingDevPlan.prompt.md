## Plan: PTX64 Frontend & Mapping Dev Plan Prompt

把 [docs/doc_dev/ptx6.4_sm70/01_ptx64_frontend_and_mapping.md](../../../doc_dev/ptx6.4_sm70/01_ptx64_frontend_and_mapping.md) 的“解析/绑定/映射链路 + 冻结 matcher + 诊断分层”，落成一份可执行的 dev-plan prompt，并顺手修复现有 plan prompts 的相对链接，使 markdown-link-check 不再报错。

### Steps 5
1. 起草计划文档：新增 [docs/doc_plan/plan_dev/ptx6.4_sm70/01_ptx64_frontend_and_mapping_dev_plan.md](01_ptx64_frontend_and_mapping_dev_plan.md)，沿用“目标/单一真源/步骤/验收”的结构。
2. 固化“端到端链路与触点”：在计划里明确 Parser→Binder→Registry→Mapper 的改动地图，指向 [src/frontend/parser.cpp](../../../../src/frontend/parser.cpp)、[src/frontend/binder.cpp](../../../../src/frontend/binder.cpp)、[src/instruction/ptx_isa.cpp](../../../../src/instruction/ptx_isa.cpp) 及核心 `PtxIsaMapper::map_kernel`/`map_one`、`PtxParser::parse_kernel`、`bind_kernel_by_name`。
3. 写清“冻结匹配键 + fail-fast 诊断分层”：把 matcher 冻结为 `ptx_opcode + type_mod + operand_kinds`，并在 prompt 列出预期诊断码（`DESC_NOT_FOUND`/`OPERAND_FORM_MISMATCH`/`DESC_AMBIGUOUS`/`OPERAND_PARSE_FAIL`）与各自处理路径。
4. 计划化 Parser 关键语义：在计划明确寄存器计数（`.reg .f32` 计入 u32 bank）、参数自然对齐、谓词 `@%p/@!%p`、以及 M4 label→PC 两遍扫描 + `bra` 单操作数重写（对齐实现点在 [src/frontend/parser.cpp](../../../../src/frontend/parser.cpp)）。
5. 修复现有 plan prompts 的相对链接：把 [docs/doc_plan/plan_dev/ptx6.4_sm70/plan-00_tier0GateDevPlan.prompt.md](./plan-00_tier0GateDevPlan.prompt.md) 和 [docs/doc_plan/plan_dev/ptx6.4_sm70/plan-00.01_failFastMemoryOobDevPlan.prompt.md](./plan-00.01_failFastMemoryOobDevPlan.prompt.md) 中的 `docs/...` 链接替换为 `../../../...` / `../../../../...` 风格，保证从当前目录解析正确。

### Further Considerations 3
1. `%laneid/%warpid` special token：实现似乎要求带点号（如 `%tid.x`），文档若写 `%laneid` 需确认是更新实现还是修正文档。
2. fixture 规范：避免把“看起来像数字”的 token 当 symbol（实现会拒绝），计划中最好明确写 fixture 命名/写法约束。
3. 命名约定：`plan-01_*.prompt.md` 是否要严格对齐 doc 编号（`01_`）还是用更描述性的前缀（两者选一保持一致）。
