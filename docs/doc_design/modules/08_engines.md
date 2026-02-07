# 08 Engines（ComputeEngine + CopyEngine）

说明
- 本文只描述抽象逻辑：提交/推进/tick、完成信号与与 SIMT/Mem 的交互。
- 设计基准：与 [doc_design/arch_modules_block.diagram.puml](../arch_modules_block.diagram.puml) 与 [doc_design/sequence.diagram.puml](../sequence.diagram.puml) 对齐；若文本与图冲突，以图为准。
- 多 SM 并行执行（每 SM 一个软件线程）详见：[doc_design/modules/06.02_sm_parallel_execution.md](06.02_sm_parallel_execution.md)

---

## 职责（Responsibilities）
- `CopyEngine`：执行 COPY 命令（H2D/D2H/D2D/Memset），对 GlobalMemory 产生副作用，并在完成时 signal DependencyTracker。
- `ComputeEngine`：执行 KERNEL 命令，构建 kernel/CTA/warp/thread 上下文，将工作投递到 SMModel，并在 kernel 完成时 signal DependencyTracker。
- 定义统一推进语义：`tick()` 为系统的逻辑步推进，并与 `Event.ts` 一致。

---

## 输入/输出（Inputs/Outputs）
- 输入：来自 CommandQueue 的 ready 命令（COPY/KERNEL）。
- 输出：
  - GlobalMemory 读写（CopyEngine）。
  - SMModel 上下文与执行推进（ComputeEngine）。
  - completion token → DependencyTracker。
  - 观测事件：submit/complete 与 tick 时间戳。

---

## tick 模型（逻辑推进）

定义
- `tick()` 推进系统一个逻辑时间步：可能提交若干 ready 命令、推进若干 warp step、推进若干 copy 进度。
- `Event.ts` 取自 tick 计数或等价的单调时间戳。

约束
- tick 不改变 contracts：只是驱动“何时推进”。

---

## CopyEngine

接口（概念）
```text
submit_copy(cmd: Command) -> CmdId
tick() -> void
```

执行语义
- H2D：从 HostMemory（抽象）读，写入 GlobalMemory。
- D2H：从 GlobalMemory 读，写入 HostMemory。
- D2D：GlobalMemory 内拷贝。
- Memset：GlobalMemory 指定范围填充。

完成信号
- 完成后调用 `DependencyTracker.on_complete(cmd_id)`。

观测点
- `COPY`：copy_submit / copy_complete（携带 bytes、范围、kind）。

---

## ComputeEngine

接口（概念）
```text
submit_kernel(cmd: Command) -> CmdId
tick() -> void
```

执行语义
- submit_kernel：
  - 解析 KernelPayload：entry、grid/block、params。
  - 建立 `KernelContext`：kernel_id、param/const 绑定、CTA 列表。
  - 将 CTA work items 投递到 `SMModel.enqueue_kernel_work()`。

- tick：
  - 选择一个或多个 SM 推进
  - 在每个 SM 内由 WarpScheduler 选择 ready warp
  - 调用 `Executor.step_warp()` 推进
  - 当所有 CTA/warp 完成时，kernel 完成并 signal

完成信号
- kernel 完成后调用 `DependencyTracker.on_complete(cmd_id)`，并产生 `kernel_complete` 事件。

观测点
- `STREAM`/`EXEC`：kernel_submit/kernel_complete（携带 kernel_id/entry）。

---

## 不变量与边界条件（Invariants / Error handling）
- ComputeEngine 不绕过 SIMT Core 与 Units。
- CopyEngine 只作用于 GlobalMemory（No-cache 基线）。
- completion 只通过 DependencyTracker 影响后续命令。

---

## 验收用例（Smoke tests / Golden traces）
- memcpyAsync + launchKernel + event record/wait：端到端组合可复盘，kernel 提交/完成顺序与依赖一致。
- copy/compute overlap：不同 stream 的 COPY 与 KERNEL 可在 tick 中交错推进，并在 Trace 中体现。
