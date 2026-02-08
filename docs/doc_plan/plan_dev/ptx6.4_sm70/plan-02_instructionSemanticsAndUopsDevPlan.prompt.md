## Plan: Instruction Semantics & Micro-ops Dev Plan

把 [docs/doc_dev/ptx6.4_sm70/02_instruction_semantics_and_uops.md](../../../doc_dev/ptx6.4_sm70/02_instruction_semantics_and_uops.md) 的 “IR→uops（inst_desc）契约 + expand + SIMT wiring + fail-fast 分流点”，整理成可执行的 dev plan：每加一个新语义，都能按清单同步四处（inst_desc / contracts / expander / units），并确保错误在正确的阶段爆出（加载期 vs 运行期）。

### Steps 6
1. 冻结 IR→uops 执行契约：以 [include/gpusim/contracts.h](../../../../include/gpusim/contracts.h) 的 `MicroOp`/`MicroOpOp`/`Operand` 为唯一输入，明确 `exec_mask = warp.active & uop.guard` 为空时的 no-op 语义落在各 unit（Exec/Mem/Ctrl）。
2. 规范 inst_desc 资产与 schema：围绕 [assets/inst_desc/](../../../../assets/inst_desc/) 与 [schemas/inst_desc.schema.json](../../../../schemas/inst_desc.schema.json) 明确必填字段、`type_mod` wildcard（空串）策略、以及 bring-up 期“严格/非严格”（未知字段是否允许）的决策点。
3. 落实加载期 fail-fast：以 [src/instruction/descriptor_registry.cpp](../../../../src/instruction/descriptor_registry.cpp) 的 `DescriptorRegistry::load_json_text()`、`parse_uop_kind()`、`parse_uop_op()` 为入口，确保未知 kind/op、缺字段在加载期直接失败，并把错误信息格式写进计划（便于定位到具体 desc/uop）。
4. 锁定 lookup 匹配行为：以 [src/instruction/descriptor_registry.cpp](../../../../src/instruction/descriptor_registry.cpp) 的 `DescriptorRegistry::lookup(const InstRecord&)` 为中心，写清匹配键是 `opcode + (type_mod optional) + operand_kinds` 且顺序严格；同时把“是否要加 ambiguous 检测（>1 命中）”作为可选增强，并要求纳入 Tier‑0 期望。
5. 明确 expand 的语义与边界：以 [src/instruction/expander.cpp](../../../../src/instruction/expander.cpp) 的 expand 逻辑为准，计划里固定 `attrs.type/space/flags` 的来源与默认行为，以及 `UopTemplate.in/out` 索引越界目前是静默忽略（是否要升级为 fail-fast/诊断作为可选增强）。
6. 固化 SIMT wiring 与运行期 fail-fast 分流：以 [src/simt/simt.cpp](../../../../src/simt/simt.cpp) 为入口，计划中逐条列出并链接到代码点：`E_DESC_MISS`、`E_PRED_OOB`、`E_DIVERGENCE_UNSUPPORTED`、`E_NEXT_PC_CONFLICT`；并标注执行侧在 [src/units/exec_core.cpp](../../../../src/units/exec_core.cpp)、[src/units/mem_unit.cpp](../../../../src/units/mem_unit.cpp)、[src/units/control_unit.cpp](../../../../src/units/control_unit.cpp) 的 `E_UOP_ARITY`/`E_UOP_UNSUPPORTED` 应如何被触发与归因。

### Further Considerations 3
1. “未知字段报错”现状：当前 loader 会忽略未知 JSON key，且 schema 也未禁止 `additionalProperties`；要不要把“严格 schema”纳入 Tier‑0 gate？
2. `parse_space()`/`parse_value_type()` 的默认值会掩盖资产错误；是否在 bring-up 后期升级为诊断或加载期失败？
3. 测试覆盖缺口：现有测试更偏 end-to-end（tiny GPT‑2 mincov）；是否需要补少量 targeted fixtures/assertions 覆盖 `E_DESC_MISS`/`E_UOP_UNSUPPORTED`/`E_NEXT_PC_CONFLICT` 等分流点？
