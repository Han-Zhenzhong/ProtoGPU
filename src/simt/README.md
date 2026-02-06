# src/simt/

SIMT core：SM/CTA/Warp/Thread 上下文 + 调度 + 执行编排。

包含模块
- SMModel、CTAScheduler、WarpScheduler
- WarpContext / ThreadContext / CTAContext
- Executor（warp 级 fetch/desc/expand/dispatch/commit 的编排层）

对外契约
- 执行语义以 micro-op 契约为准；与 units/memory/instruction 交互按 PUML 连线。
