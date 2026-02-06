# 07 Runtime + Streaming（实现落地：Commands/Queues/DependencyTracker）

参照
- 抽象设计：`doc_design/modules/07_runtime_streaming.md`
- Contracts：`doc_dev/modules/00_contracts.md`
- WorkloadSpec（stream 输入）：`doc_dev/modules/07.01_stream_input_workload_spec.md`

落地目标
- stream FIFO 严格；event record/wait 的依赖判定可复盘。
- Engines 的 completion 只通过 DependencyTracker 影响后续命令。

落地位置（代码）
- `src/runtime/`
  - runtime_api
  - stream_manager
  - command_queue
  - dependency_tracker

---

## 1. Command 落地结构

Command
- cmd_id/stream_id/kind/payload。

序列化
- Command 不要求持久化；但对外输出（trace）必须包含 cmd_id/kind。

---

## 2. StreamQueue

API
```text
enqueue(cmd)
peek() -> cmd
pop() -> cmd
```

约束
- 队首未完成前，后续命令不得提交。

---

## 3. DependencyTracker

状态
- `event_state[event_id] = signaled + by_cmd_id + ts`。

API
```text
is_ready(stream_id, cmd) -> bool
on_complete(cmd_id) -> void
record_event(event_id, cmd_id, ts) -> void
wait_event_ready(event_id) -> bool
```

ready 规则
- EVENT_WAIT：event_id signaled 才 ready。
- 其它命令：满足 stream FIFO 且其依赖已满足。

观测
- `STREAM`：cmd_enq/cmd_ready/cmd_submit/cmd_complete/event_record/event_wait_done。
