# schemas/

> **TODO**: 当前 schema 好像没有在工程里用到。

- `inst_desc.schema.json`：指令描述 JSON 的 schema（字段约束、枚举、版本、互斥关系等）。
- `ptx_isa.schema.json`：PTX ISA 映射表的 schema（PTX opcode/type_mod/operand_kinds → ir_op）。

说明
- 目前加载 JSON 时主要依赖运行时解析的必填字段访问（缺字段会抛错），尚未把 JSON Schema 校验完整接入到加载流程中。
