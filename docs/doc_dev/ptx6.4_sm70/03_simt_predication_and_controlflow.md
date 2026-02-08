# 03 Dev：SIMT predication 与最小控制流（实现落点）

本文对应设计文档 [docs/doc_design/ptx6.4_sm70/03_simt_predication_and_controlflow.md](../../doc_design/ptx6.4_sm70/03_simt_predication_and_controlflow.md)。目标是把 sm70 profile 附录 A 的控制流基线（M3/M4）落到当前实现：**SIMT 编排层如何做 predication guard、如何强制 uniform-only、以及 next_pc 的提交点**。

单一真源（规格）
- [docs/doc_spec/sm70_profile.md](../../doc_spec/sm70_profile.md)

---

## 1) SIMT 编排层的真实职责

SIMT 的核心编排实现在 `src/simt/simt.cpp`（类 `SimtExecutor`，接口见 `include/gpusim/simt.h`）。单个 warp 的循环大致是：

1. `FETCH`：按 `warp.pc` 取 `KernelImage.insts[pc]`
2. `DescriptorRegistry::lookup(inst)`：查 IR→uops descriptor（miss 则 fail-fast）
3. `Expander::expand(inst, desc, warp_size)`：展开为 `MicroOp[]`
4. **predication 注入**：把 `InstRecord.pred` 转成 lane mask，交叉进每个 `uop.guard`
5. Dispatch：把每条 uop 送到 ExecCore / MemUnit / ControlUnit
6. Collect：聚合 `StepResult.next_pc`（冲突则 fail-fast）
7. `COMMIT`：在 inst 级提交 `next_pc`（或默认 `pc+1`）

注意：predication 不是在 expander 内做的，而是在 SIMT 编排层对展开后的 `uops[]` 做统一处理。

---

## 2) Warp 与 lane（warp_size / partial warp / active_mask）

实现位置：`src/simt/simt.cpp`

- `warp_size`：`SimConfig.warp_size` 或 launch override；必须在 `[1,32]`，否则 `E_WARP_SIZE`。
- partial warp：每个 warp 初始化时设置 `warp.active = lane_mask_prefix(warp_size, lanes_on)`，其中 `lanes_on` 由 block 线程数的“尾部不足”计算得出。

执行侧约定：所有 unit 都用

`exec_mask = warp.active & uop.guard`

作为“本条 uop 允许生效的 lane 集合”。

---

## 3) predication（M3）：InstRecord.pred → MicroOp.guard

数据流：

- Parser 把 `@%pN` / `@!%pN` tokenization 成 `InstRecord.pred`（`PredGuard{pred_id,is_negated}`）。
- SIMT 在 expand 后注入 guard：`src/simt/simt.cpp` 中构造 `pred_mask` 并对每个 uop：
  - `u.guard = lane_mask_and(u.guard, pred_mask)`

关键实现细节：

- predicate 存储：`warp.p` 是按 `[pred_id][lane]` 的扁平数组（每 CTA 初始化为 `8 * warp_size`）。
- 越界保护：`pred_id < 0` 或 `pred_id * warp_size + warp_size > warp.p.size()` 会 fail-fast：
  - `Diagnostic{ module="simt", code="E_PRED_OOB" }`

语义要点（与设计一致）：

- predication 是 lane-wise：每个 lane 用自己的 predicate 值决定是否开启。
- predication 与 active_mask 叠加：只对 `warp.active` 中的 lane 计算与生效。

---

## 4) uniform-only 分支（M4）：divergence 检测

设计口径：同一 warp 内 active lanes 对分支条件必须一致，否则 fail-fast。

当前实现位置：`src/simt/simt.cpp`

执行策略：在 dispatch control uop 之前对 `BRA/RET` 做一致性检查：

- 计算 `m = warp.active & uop.guard`
- 若 `m` 非空且 `m != warp.active`：视为 divergence → fail-fast
  - `Diagnostic{ module="simt", code="E_DIVERGENCE_UNSUPPORTED" }`

这实现了两点：

- 允许 **predicated-off branch**：`m` 为空时等价 no-op（不触发 divergence）。
- 禁止“只有部分 lane 跳转”的情况（包括 predicate 导致的部分跳转）。

注意：当前 divergence 检测是在 SIMT 编排层做的；`ControlUnit` 自身只负责把 `BRA` 变成 `StepResult.next_pc`。

---

## 5) PC 更新：next_pc 通道 + inst 级提交点

实现位置：`src/simt/simt.cpp` + `src/units/control_unit.cpp`

### 5.1 ControlUnit：只产生 next_pc，不直接写 PC

`ControlUnit::step()`：
- `BRA`：要求 `inputs.size()==1` 且 `inputs[0]` 为 `imm(pc)`，否则 `units.ctrl/E_UOP_ARITY` 或 `units.ctrl/E_OPERAND_KIND`。
- `BRA` 返回 `StepResult.next_pc`。
- `RET`：设置 `warp.done=true` 并返回 `warp_done=true`。

### 5.2 SIMT：聚合 next_pc，并在 commit 提交

SIMT 对同一条 inst 的所有 uops：
- 收集 `sr.next_pc` 到局部变量 `next_pc`。
- 若多条 uop 都设置了 `next_pc`：必须一致，否则 fail-fast：
  - `Diagnostic{ module="simt", code="E_NEXT_PC_CONFLICT" }`

commit 阶段（仅当 `warp.done==false`）：
- `warp.pc = next_pc`（若存在）
- 否则 `warp.pc = old_pc + 1`
- 同时发出 `EventCategory::Commit` 的 `COMMIT` 事件。

语义要点：
- PC 写入是 inst 级的：保证“先执行完本条 inst 的所有 uops，再提交 PC”。
- `RET` 会让 `warp.done=true`，因此不会进入 commit 更新路径。

---

## 6) 诊断与观测（你应该在 trace/diag 里看到什么）

常见诊断（模块/代码）：

- `simt/E_PC_OOB`：PC 越界
- `simt/E_DESC_MISS`：descriptor lookup miss
- `simt/E_PRED_OOB`：predicate id 越界
- `simt/E_DIVERGENCE_UNSUPPORTED`：uniform-only 违反
- `simt/E_NEXT_PC_CONFLICT`：同一 inst 多 control uop 的 next_pc 冲突
- `units.ctrl/E_UOP_*`：ControlUnit 的 uop 形态错误

观测事件（`ObsControl.emit`）：

- `FETCH`：包含 `pc`、`warp.active`、`inst.opcode`
- `UOP`：按 kind 分类到 `Exec/Ctrl/Mem`（当前实现里 lane_mask 记录的是 `warp.active`，而不是 `exec_mask = active & guard`）
- `COMMIT`：提交点（`old_pc`、lane_mask、opcode）

---

## 7) 验收入口（Tier‑0）

SIMT predication + uniform-only 控制流是 Tier‑0 bring-up 的硬约束，默认验收用：

```bash
ctest --test-dir build -C Release -V -R "^gpu-sim-tiny-gpt2-mincov-tests$"
```
