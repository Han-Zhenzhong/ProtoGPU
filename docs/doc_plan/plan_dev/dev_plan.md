# Dev Plan（对齐 Overall Plan + Design Docs）

说明
- 本文是开发阶段的任务拆解与验收点清单，对齐 [doc_plan/overall_plan.md](../overall_plan.md)。
- 设计基准以 PUML 为准（尤其是 doc_design/arch_modules_block.diagram.puml 与 doc_design/sequence.diagram.puml）；若与文字冲突，以图为准。
- 指令描述文件格式固定为 JSON。

范围
- 覆盖 Phase 2（开发实现设计，产出在 doc_dev/）与 Phase 3（代码实现，产出在 src/）。
- 不包含 Phase 4–6 的测试计划拆解（放在 doc_plan/plan_test/）；但本计划在每步包含“联动验收入口”，保证实现可被验证。

---

## 0. 全局落地约束（实现前先定）

0.1 仓库内的契约来源
- Contracts：以 [doc_design/modules/00_contracts.md](../../doc_design/modules/00_contracts.md) 与 [doc_design/arch_design.md](../../doc_design/arch_design.md) 第 3 章为准。
- 事件面：以 [doc_design/modules/01_observability.md](../../doc_design/modules/01_observability.md) 为准。
- No-cache 内存语义：以 [doc_design/modules/04_memory.md](../../doc_design/modules/04_memory.md) 为准。

0.2 统一配置输入
- 运行配置与硬件规格配置以 JSON 表达（示例在 arch_design 第 6 章已给出 JSON）。

0.3 统一错误与诊断
- 对外可见错误统一为 `Diagnostic`（module/code/message/location/inst_index）。

验收点
- doc_dev/ 中存在“实现契约与接口落地约定”的总入口文档（见 1.1）。

---

## 1. Phase 2：开发实现设计（doc_dev/ 产出）

目标
- 将抽象逻辑设计落为可直接指导编码的实现级设计：文件/模块边界、类与数据结构、错误处理、配置、日志与观测落地方式。

### 1.1 建立 doc_dev/ 的实现设计目录与主入口
任务
- 创建 doc_dev/ 的主入口文档：`doc_dev/README.md`，包含：
  - 模块落地顺序与依赖
  - 配置文件约定（JSON）
  - 观测输出约定（trace/stats）
  - 代码组织与模块边界（对齐 src/ 目录与 PUML package）

验收点
- doc_dev/README.md 能指向每个模块的实现设计文档（1.2–1.10）。

### 1.2 Contracts 落地设计（对齐 00_contracts）
参照
- [doc_design/modules/00_contracts.md](../../doc_design/modules/00_contracts.md)

任务（doc_dev）
- 定义最终落地的数据结构与序列化策略：Event/TraceDump/Stats/Config。
- 明确 lane-wise Value 的存储布局与寄存器文件表示。

验收点
- Contracts 在代码中有单一权威定义位置，且不会在不同模块重复定义。

### 1.3 Observability 落地设计（对齐 01_observability）
参照
- [doc_design/modules/01_observability.md](../../doc_design/modules/01_observability.md)

任务（doc_dev）
- 定义 TraceBuffer 导出格式（JSON lines 的字段/顺序/稳定性约束）。
- 定义 Counters 的 scope 聚合规则（global/kernel/sm/warp 的落地方式）。

验收点
- 任意模块只调用 ObsControl，不直接引用 TraceBuffer/Counters。

### 1.4 Frontend 落地设计（对齐 02_frontend）
参照
- [doc_design/modules/02_frontend.md](../../doc_design/modules/02_frontend.md)

任务（doc_dev）
- 明确 Parser 的 streaming API 形态（iterator / callback）与错误恢复策略。
- 明确 Binder 的符号表结构与 label_index 构建规则的实现细节。

验收点
- Module/Function 的落地结构满足 Runtime 与 Executor 的取指与定位需求。

### 1.5 Instruction System 落地设计（对齐 03_instruction_system）
参照
- [doc_design/modules/03_instruction_system.md](../../doc_design/modules/03_instruction_system.md)

任务（doc_dev）
- 定义 JSON descriptor 的 schema 文件位置与校验机制（加载失败的 Diagnostic 规范）。
- 定义 lookup 索引结构与匹配优先级的实现策略。

验收点
- lookup deterministic，expand 输出序列稳定。

### 1.6 Memory 落地设计（对齐 04_memory）
参照
- [doc_design/modules/04_memory.md](../../doc_design/modules/04_memory.md)

任务（doc_dev）
- 明确地址表达与 lane-wise addr 表示方式。
- 明确 strict_memory_checks 的行为矩阵（对齐/越界/只读空间写入等）。

验收点
- MemUnit 只通过 ASM API 访问内存。

### 1.7 Units 落地设计（对齐 05_units）
参照
- [doc_design/modules/05_units.md](../../doc_design/modules/05_units.md)

任务（doc_dev）
- 明确 ExecCore/ControlUnit/MemUnit 的输入输出落地接口与状态读写路径。
- 明确 reconvergence 栈的落地数据结构与控制流规则。

验收点
- ControlUnit 的 PC 更新只使用 InstList index。

### 1.8 SIMT Core 落地设计（对齐 06_simt_core）
参照
- [doc_design/modules/06_simt_core.md](../../doc_design/modules/06_simt_core.md)

任务（doc_dev）
- 明确 Executor 的“单步”边界与可重试的阻塞语义。
- 明确 WarpScheduler 的 ready 判定实现细节。

验收点
- 单 kernel/单 CTA/单 warp 的执行可在不依赖 streaming 的情况下完成。

### 1.9 Runtime + Streaming 落地设计（对齐 07_runtime_streaming）
参照
- [doc_design/modules/07_runtime_streaming.md](../../doc_design/modules/07_runtime_streaming.md)

任务（doc_dev）
- 明确 Command 的落地结构与序列化（如需）。
- 明确 DependencyTracker 的 event_state 模型与完成信号接口。

验收点
- stream FIFO + event wait 的 ready 判定可复盘。

### 1.10 Engines 落地设计（对齐 08_engines）
参照
- [doc_design/modules/08_engines.md](../../doc_design/modules/08_engines.md)

任务（doc_dev）
- 明确 tick 的主循环组织方式：谁驱动 tick，tick 内推进顺序。
- 明确 CopyEngine 与 ComputeEngine 完成信号对 DepTracker 的唯一通道。

验收点
- memcpyAsync + launchKernel + event record/wait 的端到端组合可由 tick 推进并在 Trace 中复盘。

---

## 2. Phase 3：代码实现（src/ 产出）

实现顺序（依赖顺序）
1) common/contracts（类型与契约）
2) observability（事件面）
3) instruction（descriptor + expander）
4) frontend（parser/binder/module）
5) memory（ASM + spaces）
6) units（exec/ctrl/mem）
7) simt（contexts/schedulers/executor）
8) runtime（streams/commands/deps）
9) engines（copy/compute + tick）

说明
- 顺序与 [doc_plan/plan_design/module_design_sequence.md](../plan_design/module_design_sequence.md) 的 Step0–8 对齐。

### 2.1 common/contracts
参照
- [doc_design/modules/00_contracts.md](../../doc_design/modules/00_contracts.md)

任务（src）
- 在 `src/common/` 建立 contracts 的权威定义与 JSON 相关结构（Event/Config/TraceDump/Stats）。

验收点
- 任意模块能引用 contracts，不引入循环依赖。

### 2.2 observability
参照
- [doc_design/modules/01_observability.md](../../doc_design/modules/01_observability.md)

任务（src）
- ObsControl：配置、过滤、采样、emit。
- TraceBuffer：ring buffer + flush（JSON lines）。
- Counters：inc/snapshot/reset。

验收点
- 端到端任一路径可通过 emit 产生稳定事件输出。

### 2.3 instruction
参照
- [doc_design/modules/03_instruction_system.md](../../doc_design/modules/03_instruction_system.md)

任务（src）
- DescriptorRegistry：JSON schema 校验 + lookup。
- MicroOpExpander：expand(inst, desc) 并产生 guard/attrs。

验收点
- 对未知/歧义 descriptor 产生 Diagnostic 与观测事件。

### 2.4 frontend
参照
- [doc_design/modules/02_frontend.md](../../doc_design/modules/02_frontend.md)

任务（src）
- Parser：streaming 解析 + dbg 定位。
- Binder：符号表 + label_index + operand 绑定。
- ModuleBuilder：生成 Module/Function。

验收点
- 给定 PTX，输出 Module/Function/InstList/label_index。

### 2.5 memory
参照
- [doc_design/modules/04_memory.md](../../doc_design/modules/04_memory.md)

任务（src）
- AddrSpaceManager：路由 + 检查。
- Global/Shared/Local/Const/Param：空间实现。
- fence/barrier_sync：保守正确语义。

验收点
- MemUnit 可通过统一 API 完成 ld/st/atom/fence/barrier。

### 2.6 units
参照
- [doc_design/modules/05_units.md](../../doc_design/modules/05_units.md)

任务（src）
- ExecCore：Exec uop 执行与写回。
- ControlUnit：BRA + reconv stack + PC/mask 更新。
- MemUnit：地址计算 + memsys 调用 + 写回 + 阻塞。

验收点
- 每类 uop 均产生对应观测事件。

### 2.7 simt
参照
- [doc_design/modules/06_simt_core.md](../../doc_design/modules/06_simt_core.md)

任务（src）
- Contexts：Thread/Warp/CTA。
- Schedulers：CTA/Warp。
- Executor：fetch/lookup/expand/dispatch/commit。

联动子计划（Kernel Launch 3D + builtins）
- [doc_plan/plan_dev/plan-launchGridBlock3d.prompt.md](plan-launchGridBlock3d.prompt.md)

验收点
- 单 warp 可在 step_warp 循环中执行到结束。

### 2.8 runtime
参照
- [doc_design/modules/07_runtime_streaming.md](../../doc_design/modules/07_runtime_streaming.md)

任务（src）
- Runtime host API：命令入队。
- StreamManager/Queue：FIFO。
- DependencyTracker：event_state + ready 判定 + completion。

联动子计划（WorkloadSpec：stream 输入）
- [doc_plan/plan_dev/plan-workload_spec_stream_input.md](plan-workload_spec_stream_input.md)

验收点
- event record/wait 依赖可复盘。

### 2.9 engines
参照
- [doc_design/modules/08_engines.md](../../doc_design/modules/08_engines.md)

任务（src）
- CopyEngine：对 GMEM 的 copy/memset。
- ComputeEngine：kernel submit/tick/complete。
- tick 主循环：推进 streams + engines + simt。

验收点
- COPY 与 KERNEL 可在不同 stream 下交错推进，Trace 可复盘。

---

## 3. 联动验收（实现阶段的最低必要入口）

目标
- 每完成一个模块，立即具备可验证入口，避免后置集成。

建议入口
- CLI（`src/apps/cli/`）：
  - 输入：PTX + descriptor JSON + config JSON
  - 输出：trace（JSON lines）+ stats（JSON）

验收点
- 任一失败可从 Diagnostic 与 Trace 定位到：模块名 + inst_index/pc + 源位置。
