# 01 PTX64 Frontend & Mapping Dev Plan（Parser/Binder/Mapper）

目标：冻结 PTX 6.4 bring-up 子集的“解析→绑定→映射（PTX→IR）”链路，使新增/修改 form 时能稳定命中 matcher，并且失败时输出可分层诊断（miss / mismatch / ambiguous / parse fail）。

单一真源
- 前端与映射实现说明：[docs/doc_dev/ptx6.4_sm70/01_ptx64_frontend_and_mapping.md](../../../doc_dev/ptx6.4_sm70/01_ptx64_frontend_and_mapping.md)
- Tier‑0 forms 与约束（M1–M4）：[docs/doc_spec/ptx64_baseline.md](../../../doc_spec/ptx64_baseline.md)

---

## 1) 冻结 matcher（bring-up 期）

- 冻结匹配键：`ptx_opcode + type_mod + operand_kinds`
- bring-up 默认：不让 `space/flags` 进入匹配（除非完成“规格+schema+回归”闭环）。

---

## 2) 端到端链路与触点

- Parser（tokenization + kernel 成像）：`src/frontend/parser.cpp`
- Binder（选择 entry kernel）：`src/frontend/binder.cpp`
- Mapper/Registry（PTX→IR）：`src/instruction/ptx_isa.cpp`

---

## 3) Parser 关键语义（必须锁定）

- `.reg .f32` 计入 u32 bank（`reg_u32_count`）
- param 布局：自然对齐（u32=4，u64=8）
- predication：`@%pN` / `@!%pN` tokenization 到 `InstRecord.pred`
- M4：label→PC 两遍处理，并在 kernel 结束时把 `bra label` 重写为 `bra imm(pc)`（仅对单操作数 `bra` 生效）

---

## 4) 映射诊断分层（失败必须可归因）

- `DESC_NOT_FOUND`：无匹配 entry
- `OPERAND_FORM_MISMATCH`：候选存在，但 operand 形态/解析不匹配
- `DESC_AMBIGUOUS`：多个候选等价可命中
- `OPERAND_PARSE_FAIL`：具体 operand 解析失败（包含原因信息）

---

## 5) 新增/修改一个 PTX form 的推荐步骤

1. 先写 fixture（锁定 PTX 写法）。
2. 让 Parser/Binder 能通过并产生预期 tokens。
3. 补 `assets/ptx_isa/*.json` entry，让 mapper 命中且无歧义。
4. 把 fixture 纳入 Tier‑0（或解释例外）。

验收
- 默认 Tier‑0 gate：`gpu-sim-tiny-gpt2-mincov-tests`
