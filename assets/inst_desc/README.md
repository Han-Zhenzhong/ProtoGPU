# assets/inst_desc/

指令描述 JSON 数据集。

用途
- Inst Descriptor Registry 的输入
- 可用于功能/回归测试覆盖（与 tests/fixtures/inst_desc 区分：这里偏默认数据集）

说明（descriptor-driven decode 之后的推荐分层）
- 若要支持“用户选择 PTX8 指令集，只关心 PTX op → 通用 IR op，不关心 uops 组合”，建议将描述数据拆成两层：
	- PTX ISA 映射：`ptx_opcode/type_mod/operand_kinds -> ir_op`
	- IR 语义库：`ir_op (+operand_kinds) -> uops[]`
- 本目录可继续承载默认数据集；具体分层文件名与目录约定以 dev doc 为准。

约束
- JSON 必须通过 `schemas/inst_desc.schema.json` 校验。
