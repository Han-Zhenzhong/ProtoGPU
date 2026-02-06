# 07 Runtime + Streaming（Streams / Queues / DependencyTracker）

说明
- 本文只描述抽象逻辑：命令模型、队列语义、依赖与完成信号。
- 设计基准：与 [doc_design/arch_modules_block.diagram.puml](../arch_modules_block.diagram.puml) 与 [doc_design/sequence.diagram.puml](../sequence.diagram.puml) 对齐；若文本与图冲突，以图为准。

---

## 职责（Responsibilities）
- 将 Host 侧操作表达为 Command 并入队到指定 Stream。
- 维护 stream 内顺序（FIFO）与跨 stream 依赖（event record/wait）。
- 通过 DependencyTracker 判定命令 ready，并向 Engines 提交。

---

## 输入/输出（Inputs/Outputs）

### 输入
- Host API：createStream/memcpyAsync/launchKernel/recordEvent/waitEvent/sync。
- Frontend 输出的 `Module/Function`（用于 kernel launch）。

### 输出
- 对 CopyEngine/ComputeEngine 的 `submit_*` 调用。
- `EventId` 的 record/wait 语义与完成信号。
- 观测事件：cmd_enq/submit/complete/ready/wait_done。

---

## Command 模型（Conceptual Command）
```text
Command {
  cmd_id: CmdId
  stream_id: StreamId
  kind: COPY | KERNEL | EVENT_RECORD | EVENT_WAIT
  payload: oneof
}

CopyPayload { kind: H2D|D2H|D2D|MEMSET, dst, src, bytes }
KernelPayload { module, entry, grid_dim, block_dim, params, shared_bytes? }
EventRecordPayload { event_id }
EventWaitPayload { event_id }
```

---

## 07.1 Kernel I/O + ABI（指引）

本模块只负责“命令入队、依赖、ready 判定与提交”。

与 PTX 执行相关的三类关键数据：
- 数据输入/输出（H2D/D2H/D2D/Memset）
- kernel `.param` 参数打包与绑定
- 结果输出（显式 D2H + 控制结果与同步点）

统一放在独立设计文档中：
- [doc_design/modules/09_kernel_io_and_abi.md](09_kernel_io_and_abi.md)

---

## 内部执行流程（Internal Flow）

### StreamQueue（每 stream FIFO）
```text
enqueue(cmd)
peek() -> cmd
pop() -> cmd
```

约束
- stream 内严格顺序：队首未完成前，后续命令不可提交。

### DependencyTracker
状态
- `event_state[event_id] = signaled? + signal_ts? + by_cmd?`

ready 判定（基线）
- `EVENT_WAIT(E)`：当且仅当 `event_state[E].signaled==true` 才 ready。
- `EVENT_RECORD(E)`：执行后将 `event_state[E]` 标记 signaled。
- `COPY/KERNEL`：满足 stream 内顺序与其前置依赖后 ready。

提交与完成信号
- Engines 在完成时回调 `DependencyTracker.on_complete(cmd_id)`。

---

## 接口契约（Interfaces）

Runtime Host API（概念）
```text
create_stream() -> StreamId
create_event() -> EventId
memcpy_async(stream, ...) -> CmdId
record_event(stream, event) -> CmdId
wait_event(stream, event) -> CmdId
launch_kernel(stream, ...) -> CmdId
```

Scheduler loop（概念）
```text
tick():
  for each stream:
    cmd = queue.peek()
    if dep.is_ready(cmd):
      submit to engine
```

观测点
- `STREAM`：cmd_enq, cmd_ready, cmd_submit, cmd_complete, event_record, event_wait_done。

---

## 不变量与边界条件（Invariants / Error handling）
- stream 内 FIFO 不被破坏。
- `EventId` 的 record/wait 语义可复盘：wait 必须在 record 完成后放行。
- Engines 的 completion 必须只通过 DependencyTracker 影响后续命令。

---

## 验收用例（Smoke tests / Golden traces）
- 两个 stream：stream0 COPY → recordEvent(E)；stream1 waitEvent(E) → kernel。kernel 不得在 E signaled 前提交。
- Trace 可复盘 ready/not-ready 与完成链路。
- 端到端 I/O（见 09 模块）：H2D → launchKernel(params) → D2H → host 校验输出；若失败需可从 diag+trace 定位。
