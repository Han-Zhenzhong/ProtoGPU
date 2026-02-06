# Dev Docs（实现级设计）

说明
- 本目录承载“指导代码开发”的实现级设计：目录与模块边界、接口落地、数据结构落地、配置与观测落地、错误处理与诊断规范。
- 设计基准：所有模块边界与依赖方向以 PUML 为准（尤其是 `doc_design/arch_modules_block.diagram.puml` 与 `doc_design/sequence.diagram.puml`）；若与文字冲突，以图为准。
- 指令描述文件格式固定为 JSON。

与抽象设计的关系
- 抽象逻辑设计在 `doc_design/`：模块职责、语义与流程契约。
- 实现级设计在 `doc_dev/`：落地的类型定义位置、API 形态、目录与文件组织、错误与诊断对外形态、配置与 trace 输出格式。

代码目录对齐
- `src/common/`：Contracts（核心类型、诊断、事件、配置与序列化）。
- `src/observability/`：ObsControl、TraceBuffer、Counters。
- `src/instruction/`：DescriptorRegistry（JSON+schema 校验）与 MicroOpExpander。
- `src/frontend/`：Parser/Binder/ModuleBuilder。
- `src/memory/`：AddrSpaceManager 与各地址空间实现。
- `src/units/`：ExecCore/ControlUnit/MemUnit。
- `src/simt/`：Contexts/Schedulers/Executor。
- `src/runtime/`：Runtime/Streams/Queues/DependencyTracker。
- `src/apps/cli/`：端到端入口（输入 PTX/descriptor/config，输出 trace/stats）。

实现顺序（依赖顺序）
1) Contracts
2) Observability
3) Instruction System
4) Frontend
5) Memory
6) Units
7) SIMT Core
8) Runtime + Streaming
9) Engines
10) Kernel I/O + ABI（跨 Runtime/Engines/Memory）

配置与输入输出约定
- 运行配置：JSON 文件（路径由 CLI 参数提供）。
- 指令描述：JSON 文件（路径由 CLI 参数提供）。
- 指令描述 schema：放在 `schemas/`（详见 03 Instruction System dev doc）。
- Trace：JSON lines（每行一个 Event JSON 对象）。
- Stats：JSON（Counters snapshot）。

错误与诊断约定
- 对外错误统一为 `Diagnostic`（module/code/message/location/inst_index）。
- 任何错误必须可定位到：源文件位置（file/line/column）或 inst_index（至少其一）。

文档索引（按模块）
- `doc_dev/modules/00_contracts.md`
- `doc_dev/modules/01_observability.md`
- `doc_dev/modules/02_frontend.md`
- `doc_dev/modules/02.01_frontend_desc_driven_decode.md`
- `doc_dev/modules/03_instruction_system.md`
- `doc_dev/modules/04_memory.md`
- `doc_dev/modules/05_units.md`
- `doc_dev/modules/06_simt_core.md`
- `doc_dev/modules/06.01_launch_grid_block_3d.md`
- `doc_dev/modules/07_runtime_streaming.md`
- `doc_dev/modules/07.01_stream_input_workload_spec.md`
- `doc_dev/modules/08_engines.md`
- `doc_dev/modules/09_kernel_io_and_abi.md`
