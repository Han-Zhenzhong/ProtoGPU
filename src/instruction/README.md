# src/instruction/

指令系统：InstRecord → InstDesc(JSON) → MicroOp[]。

包含模块
- Inst Descriptor Registry（JSON）
- Micro-op Expander

对外契约
- descriptor 格式以 `schemas/inst_desc.schema.json` 为准。
- lookup/expand 的行为与 `doc_design/arch_modules_block.diagram.puml` 中连线一致。
