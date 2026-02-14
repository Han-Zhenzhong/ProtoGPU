# assets/inst_desc/

> Chinese version: [README.zh-CN.md](README.zh-CN.md)

Instruction descriptor JSON dataset.

## Usage

- Input to the Inst Descriptor Registry
- Can be used for functional/regression coverage (distinct from `tests/fixtures/inst_desc`: this folder is intended to be the default dataset)

## Notes (recommended layering after descriptor-driven decode)

- If you want to support “user selects PTX8 ISA and only cares about PTX op → generic IR op, not about µop composition”, it’s recommended to split the description data into two layers:
  - PTX ISA mapping: `ptx_opcode/type_mod/operand_kinds -> ir_op`
  - IR semantics library: `ir_op (+operand_kinds) -> uops[]`
- This directory can continue to host the default dataset; exact filenames and directory conventions should follow the dev docs.

## Constraints

- JSON must validate against `schemas/inst_desc.schema.json`.
