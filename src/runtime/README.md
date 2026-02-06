# src/runtime/

Streaming runtime：Host 操作 → Command → Stream Queue → Dependencies → Engines。

包含模块
- Kernel Runtime & ABI
- StreamManager / StreamContext / CommandQueue
- DependencyTracker（events/waits）

对外契约
- 事件与完成信号语义需与 `docs/doc_design/sequence.diagram.puml` 对齐。
