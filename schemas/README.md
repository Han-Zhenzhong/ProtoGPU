# schemas/

> Chinese version: [README.zh-CN.md](README.zh-CN.md)

> **TODO**: These schemas may not currently be wired into the build/runtime.

- `inst_desc.schema.json`: schema for instruction descriptor JSON (field constraints, enums, versions, mutual exclusivity, etc.)
- `ptx_isa.schema.json`: schema for the PTX ISA mapping table (PTX `opcode/type_mod/operand_kinds` → `ir_op`)

## Notes

- Today, JSON loading primarily relies on runtime parsing with required-field access (missing fields throw at runtime). Full JSON Schema validation has not yet been fully integrated into the loading pipeline.
