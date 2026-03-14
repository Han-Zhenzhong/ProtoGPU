## Plan: 逐步模块化设计 ProtoGPU

范围说明
- 当前阶段只做抽象逻辑设计描述：模块职责、内部执行逻辑、输入/输出、流程与数据交互接口契约、状态与不变量。
- 后续再在 doc_dev 下输出“指导代码开发”的设计（目录/文件/类/函数划分、编码约束、测试落地）。

设计基准
- 所有设计以 PUML 图为准（尤其是 doc_design/arch_modules_block.diagram.puml 与 doc_design/sequence.diagram.puml）；如与文字描述冲突，以图为准。
- 指令描述文件格式固定为 JSON（Inst Descriptor Registry 只支持 JSON）。

基于现有架构文档与 UML 图，按依赖关系分步骤完成所有模块的设计：先“冻结跨模块契约”（IR/微操作/上下文/可观测性），再完成 Frontend/Instruction System/Memory/Units/SIMT/Runtime/Engines 的接口与内部流程设计，并把端到端 Streaming + SIMT + micro-op + No-cache memory + Observability 的完整流程一次性对齐落地。这样能减少接口反复变更，并保证每一步都有可验证的验收点。

落地点说明
- Step 0（契约层）的设计将补充进 doc_design/arch_design.md：以“核心数据结构与约定（第 3 章）”为主进行扩写，必要时新增独立小节，形成全模块共用的 Contracts 基准（并与 PUML 图的模块连线保持一致）。

### Steps
1. Step 0：冻结跨模块“契约层”（Core Types/Contracts）：InstRecord/MicroOp/LaneMask/Value/AddrSpace/各种 ID。
2. Step 1：Observability（必需）：ObsControl.emit() + TraceBuffer + Counters（统一事件面，用于验收与调试）。
3. Step 2：Frontend：Parser→Binder→ModuleBuilder，产出 Module/Function/InstList/label_index。
4. Step 3：Instruction System：InstDescRegistry(JSON) + MicroOpExpander，完整定义 descriptor 匹配、类型规则与 micro-op 展开规则。
5. Step 4：Memory Subsystem（No Cache）：AddrSpaceManager + Global/Shared/Local/Const/Param + Fence/Consistency。
6. Step 5：Base Units：ExecCore/ControlUnit/MemUnit（按 micro-op 契约执行）。
7. Step 6：SIMT Core：Context + Scheduler + Executor.step_warp()，定义完整的 SIMT 语义（mask/分歧合流/barrier/atomic/fence 交互）。
8. Step 7：Runtime + Streaming：Stream/Queue/Deps，落实 EVENT record/wait 语义。
9. Step 8：Engines：ComputeEngine + CopyEngine，把端到端串起来并支持 copy/compute overlap。

### 每一步“参照什么、设计什么、交付什么”

把更细的分步清单维护在：doc_plan/plan_design/module_design_sequence.md（对齐 arch_design.md 与 arch_modules_block.diagram.puml）。

### Further Considerations
1. 指令描述格式：固定为 JSON，并配套 JSON Schema（或等价的结构化约束）用于校验完整性与一致性。
2. 对齐图与目标：明确 No-cache 的内存子系统边界与一致性假设，保持文字描述与 PUML 图一致。
