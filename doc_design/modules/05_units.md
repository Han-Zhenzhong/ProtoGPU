# 05 Base Compute Units（ExecCore + ControlUnit + MemUnit）

说明
- 本文只描述抽象逻辑：Units 对 micro-op 的执行语义与状态更新。
- 设计基准：与 [doc_design/arch_modules_block.diagram.puml](../arch_modules_block.diagram.puml) 对齐；若文本与图冲突，以图为准。

---

## 职责（Responsibilities）
- `ExecCore`：执行 Exec 类 micro-ops（ALU/FP/CVT/SETP/SELP）。
- `ControlUnit`：执行 Control 类 micro-ops（BRA/call/ret 的控制语义），维护 reconvergence 栈并更新 PC/mask。
- `MemUnit`：执行 Mem 类 micro-ops（LD/ST/ATOM/FENCE/BAR），通过 Memory Subsystem 完成访问与同步。

---

## 输入/输出（Inputs/Outputs）
- 输入：`MicroOp` + `LaneMask` + 上下文（Thread/Warp/CTA）+ 依赖模块句柄（Memory/ObsControl）。
- 输出：寄存器/谓词写回、PC/mask/stack 更新、内存副作用、阻塞或 fault。

---

## 内部执行流程（Internal Flow）

### ExecCore
```text
run_exec_uop(uop, exec_mask, thread_ctx)
  -> read inputs (regs/preds/imm)
  -> compute per lane
  -> write outputs (regs/preds)
```

关键语义
- 读写均按 lane；inactive lanes 不产生副作用。
- 类型规则由 `uop.attrs.type/width` 驱动，保证与 Expander 一致。

### ControlUnit
```text
run_ctrl_uop(uop, exec_mask, warp_ctx, label_index)
  -> compute branch condition per lane
  -> update divergence/reconvergence stack
  -> update warp_ctx.pc and warp_ctx.active_mask
```

Reconvergence 栈（概念）
```text
ReconvEntry { reconv_pc, mask_before, taken_mask, fallthrough_mask }
stack: vector<ReconvEntry>
```

基线规则
- 分歧：taken 与 fallthrough 均非空时 push entry，并选择一个路径执行。
- 合流：当当前路径结束且 stack 顶部条件满足时 pop，并恢复 mask/pc。

### MemUnit
```text
run_mem_uop(uop, exec_mask, ctx, memsys)
  -> compute lane-wise addr
  -> invoke memsys read/write/atomic/fence/barrier_sync
  -> writeback (ld/atom)
  -> return MemResult
```

阻塞语义
- barrier：在 CTA 内未满足到达条件时阻塞，并通过 StepResult.blocked_reason=BARRIER 对上层报告。
- memory：若引入延迟模型或队列容量限制，MemUnit 以 blocked_reason=MEM 报告。

---

## 与其它模块的数据/流程交互（Interfaces + Events）

接口（概念）
```text
ExecCore.run_exec_uop(uop, mask, thread_ctx) -> void
ControlUnit.run_ctrl_uop(uop, mask, warp_ctx, label_index) -> void
MemUnit.run_mem_uop(uop, mask, ctx, memsys) -> MemResult
```

观测点
- `EXEC`：exec_uop（含 op/type/lanes）
- `CTRL`：ctrl_uop（含 target_pc/stack_depth/divergence）
- `MEM`：mem_uop（含 space/addr/size/fault）

---

## 不变量与边界条件（Invariants / Error handling）
- Units 不做 descriptor lookup 与展开；只执行 micro-ops。
- ControlUnit 的 PC 更新必须以 InstList index 表达。
- MemUnit 只通过 Memory Subsystem API 访问内存。

---

## 验收用例（Smoke tests / Golden traces）
- ExecCore：算术/比较/类型转换在 lane-wise 语义下确定性写回。
- ControlUnit：分歧/合流路径可复盘，reconv 栈深度与事件匹配。
- MemUnit：ld/st/atom/fence/barrier 的事件与状态更新一致。
