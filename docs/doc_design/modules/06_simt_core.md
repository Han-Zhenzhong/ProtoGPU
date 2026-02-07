# 06 SIMT Core（Contexts + Schedulers + Executor）

说明
- 本文只描述抽象逻辑：上下文模型、ready 判定、Executor 单步语义。
- 设计基准：与 [doc_design/arch_modules_block.diagram.puml](../arch_modules_block.diagram.puml) 与 [doc_design/sequence.diagram.puml](../sequence.diagram.puml) 对齐；若文本与图冲突，以图为准。
- Kernel Launch（3D grid/block + builtins）详见：[doc_design/modules/06.01_launch_grid_block_3d.md](06.01_launch_grid_block_3d.md)
- 多 SM 并行执行（每 SM 一个软件线程）详见：[doc_design/modules/06.02_sm_parallel_execution.md](06.02_sm_parallel_execution.md)

---

## 职责（Responsibilities）
- 提供 SIMT 语义正确的执行核心：warp mask、分歧合流、barrier/atom/fence 的交互。
- 将 kernel 的 CTA/warp/thread 映射为可调度的上下文，并以 step-based 方式推进。
- 对外暴露：`warp_scheduler.pick_ready_warp()` 与 `executor.step_warp()`。

---

## 输入/输出（Inputs/Outputs）
- 输入：
  - 来自 ComputeEngine 的 kernel/CTA work items。
  - 来自 Runtime 的 Function/InstList 与参数绑定。
- 输出：
  - Warp/Thread 状态更新。
  - 对 Units/Memory 的访问副作用。
  - `StepResult` 与观测事件。

---

## 上下文模型（Contexts）

### ThreadContext
```text
ThreadContext {
  regs: lane-wise register file
  preds: lane-wise predicate file
  local: optional local memory base/handle
  builtins: computed tid/ctaid/ntid/nctaid...
}
```

### WarpContext
```text
WarpContext {
  pc: PC
  active_mask: LaneMask
  reconv_stack: stack<ReconvEntry>
  barrier_state: per-barrier arrival/wait info
  done: bool
}
```

### CTAContext / SMModel
CTAContext 至少持有：shared memory handle、barrier 表、warp 列表。

---

## 调度与 ready 判定（Schedulers）

### CTA Scheduler
职责：将 CTA 分配给 SM，并初始化 CTAContext。

### Warp Scheduler
```text
pick_ready_warp() -> optional<WarpId>
```

ready 判定（基线）
- warp 未 done。
- 未被 barrier 阻塞。
- 未被 memory/engine 队列限制阻塞（若引入）。

策略接口
- 允许替换策略（round-robin、oldest-first、fair-share），但不改变 contracts。

---

## Executor.step_warp（核心执行语义）

```text
step_warp(sm_state, warp_id) -> StepResult
  fetch: inst = InstList[pc]
  emit:  FETCH(fetch)
  desc:  inst_desc = DescriptorRegistry.lookup(inst)
  uops:  uops = MicroOpExpander.expand(inst, inst_desc)
  for uop in uops:
    dispatch by kind -> ExecCore / ControlUnit / MemUnit
  commit: update pc/mask/done
  emit:  COMMIT(commit)
```

关键语义
- `exec_mask = warp.active_mask & uop.guard`。
- ControlUnit 可更新 PC 与 mask；该更新对后续 uop 的执行点具有明确规则：
  - 基线：一条 inst 展开的 uops 视为一个“不可拆分序列”，ControlUnit 的 PC 更新在 commit 生效。
- MemUnit 可能阻塞：若 uop 阻塞则 step 返回 `blocked_reason`，并保证可重试。

---

## 可观测点（Obs events + counters）
- `FETCH`：fetch（pc/opcode）
- `EXEC/CTRL/MEM`：各 uop 执行（含 op/type/space）
- `COMMIT`：commit（pc->new_pc, warp_done, mask changes）
- Counters：fetch/commit/uop_exec/uop_ctrl/uop_mem

---

## 不变量与边界条件（Invariants / Error handling）
- PC 永远指向 InstList 合法范围或 warp_done。
- Executor 不绕过 Instruction System 与 Units。
- 任何阻塞必须通过 StepResult 明确报告，且可复现。

---

## 验收用例（Smoke tests / Golden traces）
- 单 kernel/单 CTA/单 warp：在不依赖 streaming 的情况下可跑到结束。
- predication 与 divergence：事件序列可复盘 mask 变化与 reconv 栈行为。
- barrier：warp 在 barrier 未满足时阻塞并在满足后继续。
