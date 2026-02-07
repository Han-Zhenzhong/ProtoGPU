## Plan: 06.02 并行 SMWorker Dev Plan

把已完成的抽象设计与实现落地文档（06.02）转成一份可执行的“开发任务拆解 + 里程碑 + 验收点”的 dev plan 文档，落在 docs/doc_plan/plan_dev 下，并在总 dev plan 索引里挂链接，便于后续按阶段推进代码实现（GlobalScheduler、SMWorkerPool、SIMT 重构、Memory/Obs 并发安全与确定性回归模式）。

### Steps
1. 新增 dev plan 文件 `docs/doc_plan/plan_dev/plan-smParallelExecution.prompt.md`，在开头引用 `docs/doc_design/modules/06.02_sm_parallel_execution.md` 与 `docs/doc_dev/modules/06.02_sm_parallel_execution.md`。
2. 明确“现状与迁移入口”，点名当前直连路径：Runtime 调用 `gpusim::SimtExecutor::run`（`src/runtime/runtime.cpp` 与 `src/simt/simt.cpp`），以及 Engines 仍是占位（`src/runtime/engines.cpp`）。
3. 拆解里程碑 M0/M1：先做 `sm_count=1` 的结构迁移（配置扩展在 `include/gpusim/simt.h`，解析在 `src/runtime/runtime.cpp`），再把 SIMT 执行重构为“可被 worker 调用的 CTA 粒度推进”（仍落在 `src/simt/simt.cpp` 周边）。
4. 拆解里程碑 M2：新增 GlobalScheduler + SMWorkerPool 的文件落点与依赖方向（计划中指明建议新增的 runtime/simt 侧文件，并写清 CTA pinned 到单 worker 的不变量），并写清与现有 Runtime/Workload 路径的集成点（`src/runtime/workload.cpp`）。
5. 列出并行必需改造与验收点：Memory 线程安全（`include/gpusim/memory.h`、`src/memory/memory.cpp`）、统一时间戳/`sm_id` 事件字段（`include/gpusim/observability.h`、`src/observability/observability.cpp`）、以及 deterministic 回归策略（并行开关/强制 `sm_count=1` 的规则）。

### Further Considerations
1. deterministic 策略：lock-step（更复杂，但 trace 更稳定）。
2. 时间戳归属：Runtime 持有全局 logical clock。
3. 同步更新块图：把 GlobalScheduler 显式画进 `docs/doc_design/arch_modules_block.diagram.puml` 以减少歧义。
