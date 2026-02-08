# 03 SIMT：predication 与最小控制流（sm70 profile 附录 A 对齐）

本设计将 ../../doc_spec/sm70_profile.md 的附录 A（Control-flow baseline）落实为可实现、可回归的 SIMT 执行契约。

相关模块设计：../modules/06_simt_core.md

## 1. Warp 与 lane 口径（冻结）

- warp_size 基线为 32（profile Summary）。
- 允许 partial warp：`active_mask` 控制 lane 是否参与。

## 2. predication（M3）

冻结语义
- predication guard 是 lane-wise 的：每个 lane 根据其 predicate 值决定是否执行。
- guard 注入位置：SIMT 层将 InstRecord.pred（@%p / @!%p）转为 lane mask，传给 expander/uops。

执行规则
- 所有 Units 必须以 `exec_mask` 为准：
  - 写寄存器/写谓词/写内存都只能对 `exec_mask` 的 lanes 生效。

## 3. uniform-only 分支（M4）

冻结约束（强制）
- 仅支持 uniform control-flow：同一 warp 内所有 active lanes 对分支条件必须一致。
- 允许 predicated branch 形态（`@%p bra` / `@!%p bra`），但仍必须 uniform-only。

Divergence 检测
- 在执行 `bra`（或其 control uop）前，对 active lanes 计算其是否将跳转：
  - 若出现 `any_true && any_false`，则 divergence → fail-fast（例如 `E_DIVERGENCE_UNSUPPORTED`）。

## 4. PC 更新：next_pc 通道（附录 A.2）

冻结语义
- ControlUnit 对 `bra` 的效果必须通过单步结果传递（例如 `StepResult.next_pc`）。
- 提交点：本次 inst 的所有 uops 执行完后，SIMT commit 阶段一次性提交 `next_pc`。

一致性要求
- 若同一 inst 展开多个 control uop 并都提出 `next_pc`：
  - 它们必须一致；否则诊断失败（避免隐式“最后写 wins”）。

## 5. ret（M4）

冻结语义
- `ret` 结束当前 warp（或当前 kernel 的线程组），并推动 runtime 完成判定。

## 6. 观测与诊断

- divergence/invalid pc/unsupported control-flow 必须产生：
  - `Diagnostic{module:"simt"/"control_unit", code, message, location, inst_index}`
- 建议 trace 中包含：
  - `FETCH(pc, ptx/ir opcode)`
  - `CTRL(bra, target_pc, uniform=true/false)`
  - `COMMIT(pc->next_pc)`
