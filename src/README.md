# src/

功能代码根目录。

目录约定
- `common/`：跨模块契约层（IDs、Value、LaneMask、InstRecord、MicroOp、Errors、Config 等）。
- `frontend/`：PTX 前端（Parser/Binder/ModuleBuilder）。
- `instruction/`：指令系统（InstDescRegistry：JSON；MicroOpExpander）。
- `runtime/`：Streaming runtime（streams/queues/dependencies）。
- `simt/`：SIMT core（SM/CTA/Warp/Thread context + schedulers + executor orchestration）。
- `units/`：基础执行单元（ExecCore/ControlUnit/MemUnit）。
- `memory/`：No-cache 内存子系统（AddrSpaceManager + Global/Shared/Local/Const/Param + Fence/Consistency）。
- `observability/`：观测系统（ObsControl/TraceBuffer/Counters）。
- `apps/`：可执行入口与集成（CLI 等）。
