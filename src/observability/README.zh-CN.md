# src/observability/

观测与可复盘能力。

包含模块
- ObsControl：统一 emit 入口，支持过滤/采样/断点
- TraceBuffer：事件存储与导出
- Counters：统计计数与快照

对外契约
- 其它模块只依赖 ObsControl 的 emit 契约，不直接依赖 TraceBuffer/Counters。
