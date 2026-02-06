# 模块分步设计顺序（对齐 arch_design + 模块图）

说明
- 本文只做“抽象逻辑设计描述”：模块职责、内部执行逻辑、输入/输出、数据与流程交互、接口契约、状态与不变量。
- 本文不做“指导代码开发的设计”：不规定目录结构/类名/文件拆分/具体语言实现细节；这些请后续在 doc_dev 下单独落地。
- 文中的“接口”以概念契约形式给出，用于对齐模块边界与调用关系，而非要求某种具体函数签名。
- 设计基准：所有设计以 PUML 图为准（尤其是 doc_design/arch_modules_block.diagram.puml 与 doc_design/sequence.diagram.puml）；若文本描述与图有冲突，以图为准。

本文把 [doc_design/arch_design.md](../doc_design/arch_design.md) 与 [doc_design/arch_modules_block.diagram.puml](../doc_design/arch_modules_block.diagram.puml) 的模块拆分，落成“先设计什么、参照什么、要产出什么接口/数据结构/流程”的执行清单。

设计原则（与 arch_design.md 保持一致）
- PC 统一为 `InstList` index；label 在 Binder 阶段绑定到 index。
- micro-op 是 Executor 与 Base Units 的唯一执行契约（Executor 不写特殊语义）。
- Observability 只依赖单入口 `ObsControl.emit()`，其余模块不直接依赖 TraceBuffer/Counters。
- No-cache：所有 ld/st/atom 都经由 AddrSpaceManager 路由到 Shared/Global/Local/Const/Param。

---

## Step 0：冻结跨模块“契约层”（先做，否则后面会反复改接口）

设计模块
-（跨模块）Core Types/Contracts：`InstRecord`、`MicroOp`、`LaneMask`、`Value`、`AddrSpace`、各种 ID（kernel/cta/warp/thread/stream/event/cmd）。

参照
- arch_design.md：第 3 章（核心数据结构与约定）、第 4.7（Event 字段建议）、第 10（关键约束）。
- arch_modules_block.diagram.puml：模块间连线（尤其是 Exp→Exec、MemU→ASM、RT/Engines→Obs）。

要设计什么（交付物）
- 数据结构：
  - `InstRecord`：opcode/mods/pred/operands/dbg 等字段集合，保证“执行 + 展开 + 诊断”所需信息都可直接获取。
  - `MicroOp`：`kind`(Exec/Control/Mem) + `op` + 输入输出引用 + attrs（type/space/flags）+ `guard`（用于 predication/掩码执行）。
  - `Value`：类型集合（至少 u32/s32/u64/s64/f32）与按 lane 存储方式。
  - `LaneMask`：位宽（建议跟随 `warp_size`，默认 32）。
- 错误与诊断：Parser/Binder/Executor 的错误码与可定位信息（文件/行/指令索引/函数名/模块名）。
- 统一命名：PC/index、label_index、operand kinds（reg/imm/addr/symbol）。

与其它模块交互接口（概念契约，先定清楚“输入/输出/语义”）
- Descriptor 查表：给定 `InstRecord`，能确定其语义描述 `InstDesc`。
- micro-op 展开：给定 `InstRecord + InstDesc`，输出有序的 `MicroOp[]`，并携带 predication guard 与必要 attrs。
- Warp 单步执行：给定 `warp_id` 与其上下文，推进一个执行步并返回 `StepResult`（进展/阻塞原因/是否完成）。
- 统一观测：任意模块在关键点可向 `ObsControl` 发出 `Event`，且不依赖具体存储实现。

验收点
- 任意模块文档/代码只要引用这些类型，都不需要“猜字段”。

---

## Step 1：Observability（必需，优先设计便于后续调试与验收）

设计模块
- ObsControl
- TraceBuffer
- Counters

参照
- arch_design.md：4.7 节（ObsControl/TraceBuffer/Counters）、Event 建议字段。
- arch_modules_block.diagram.puml：所有 `..> OCtrl : emit(...)` 的连接点与事件种类。

要设计什么
- 事件分类与事件集合（按图中的：STREAM/COPY/FETCH/EXEC/CTRL/MEM/COMMIT + KERNEL_SUBMIT/COMPLETE）。
- `ObsControl`：enable/filter/sample/break-conditions 的配置结构与默认行为。
- TraceBuffer：ring buffer 行为（满了覆盖/丢弃策略）、flush 导出格式（先定义为 JSON lines 或自定义结构）。
- Counters：计数器 ID 列表与粒度（全局/按 kernel/按 SM）。

接口
- ObsControl：接收 Event（emit），并按配置决定记录/计数/触发条件。
- TraceBuffer：记录 Event 序列并支持导出（flush）。
- Counters：按事件或执行点累加并支持快照与清零。

验收点
- 后续每个模块的“关键动作点”都能统一发 event（不用直接依赖日志系统）。

---

## Step 2：Frontend（Parser → Binder → ModuleBuilder）

设计模块
- PTX Lexer/Parser（streaming）
- Directive/Metadata Binder
- Module Builder

参照
- arch_design.md：4.1（Frontend 全部小节）、3.2/3.3（Module/Function/InstRecord）。
- arch_modules_block.diagram.puml：Parser→Binder→ModB 的数据流、ModB→RT 的交付物。

要设计什么
- Parser：
  - 解析范围：支持架构文档中使用到的 directives（.version/.target/.address_size/.entry/.func/.reg/.param/.shared/.global/.const 等）与指令行（含 predication 与 modifiers）。
  - streaming 输出模型：回调 `onDirective/onInst` 或迭代器 `ParsedStream`。
- Binder：
  - 符号表：reg/param/shared/global/const/label。
  - `label_index`：label → PC(index) 的构建规则。
  - operand 绑定：symbol/label 引用解析为内部引用（避免执行期字符串查找）。
  - 基础校验：类型/空间/对齐/立即数范围（至少能报清楚错误位置）。
- ModuleBuilder：
  - 产出 `Module { functions, globals, consts, metadata }` 与 `Function { insts, label_index, layouts }`。

接口（建议固定）
- Parser：从 PTX 文本/流中产出 directive 与 raw instruction records（可用回调或迭代器形式表达）。
- Binder：将 raw records 绑定为可执行/可展开的记录，并产出 module/function 的布局与索引信息。
- ModuleBuilder：汇总并输出最终 Module/Function 表示。

验收点
- 给定一个 PTX 文件，能生成 `Module/Function/InstList/label_index`，且错误定位到行号/指令。

---

## Step 3：Instruction System（DescRegistry + MicroOpExpander）

设计模块
- Inst Descriptor Registry（描述文件格式 + lookup）
- Micro-op Expander（inst → uops）

参照
- arch_design.md：4.2（Desc/Expander）+ 3.4（MicroOp）。
- arch_modules_block.diagram.puml：Exec→Desc→Exp→Exec 的闭环。

要设计什么
- 描述文件格式：JSON（固定，不提供其它格式）。
  - key：opcode/type_mod/mods/operand_kinds
  - value：语义 flags + uop 模板（含输入输出映射）
  - schema：定义 JSON Schema（或等价的结构化约束）以便校验 descriptor 文件的完整性与一致性
- lookup 规则：
  - canonical opcode 与 mods 匹配优先级
  - 类型推断/强制规则（完整定义：类型 mod、operand types、立即数/地址类型、扩展/截断规则）
- expand 规则：
  - predication guard（把 `@p` 转成 micro-op guard）
  - 复杂指令拆分：addr_calc + ld/st，类型扩展等

接口
- DescriptorRegistry：加载并提供 InstDesc 查询服务（按 opcode/mods/type/operand-kinds 匹配）。
- MicroOpExpander：将 inst 规范化后展开为 MicroOp 序列（含 guard/attrs）。

验收点
- 对架构设计中纳入范围的所有指令类别（算术/逻辑/比较/转换/分支/访存/atomic/fence/barrier）都能通过描述文件查表并稳定展开为 uops。

---

## Step 4：Memory Subsystem（No Cache 基线）

设计模块
- AddrSpaceManager
- GlobalMemory
- SharedMemory
- Fence/Consistency（可先并入 ASM 但接口要明确）

参照
- arch_design.md：4.6（memory 全节）+ 1.2（非目标：不做 cache）。
- arch_modules_block.diagram.puml：MemU→ASM→(SHM/GMEM)，FENCE..>ASM。

要设计什么
- 地址空间路由：global/shared/local/const/param 的实现策略与寻址/布局规则。
- 读写语义：lane-wise 地址与 lane mask 的处理（对 inactive lanes 的行为）。
- 边界/对齐检查策略：strict vs permissive（由配置控制）。
- shared：按 CTA 私有 byte-array 的生命周期与索引（cta_id 映射）。
- global：线性 byte-array vs sparse（先线性即可）。
- fence：保守正确的排序/可见性假设（明确写在文档里）。

接口
- 统一内存访问契约：read/write/atomic/fence/barrier_sync 五类操作，明确每类操作的参数、lane mask 语义、错误处理与可见性规则。

验收点
- 能被 MemUnit 以统一 API 访问，不暴露底层存储细节。

---

## Step 5：Base Compute Units（ExecCore/ControlUnit/MemUnit）

设计模块
- ExecCore（ALU/FP/CVT/SETP/SELP）
- ControlUnit（BRA + reconvergence stack）
- MemUnit（LD/ST/ATOM/FENCE/BARRIER）

参照
- arch_design.md：4.5（units）+ 4.4.2（Warp/Thread context）+ 4.4.3（Executor）。
- arch_modules_block.diagram.puml：Exec→Core/Ctrl/MemU 的 dispatch。

要设计什么
- ExecCore：
  - 输入输出引用的寄存器/谓词读写规则（按 lane）。
  - opcode 覆盖与类型规则（由 MicroOp.op 与 attrs 驱动），保证与 InstDesc/Expander 的 contract 完整闭环。
- ControlUnit：
  - PC 更新规则（InstList index）。
  - reconvergence stack 数据结构与 push/pop 条件（覆盖分歧/合流的一般情形，并定义异常/不可识别 CFG 的处理策略）。
- MemUnit：
  - 访存地址计算与对齐/越界处理。
  - barrier：block 内同步状态机（到达/等待/释放）。
  - atom/fence：先定义“功能正确但保守”的行为与范围。

接口
- ExecCore：执行 Exec 类 micro-op，对寄存器/谓词产生确定性写回。
- ControlUnit：执行 Control 类 micro-op，更新 PC/mask/重汇合栈等控制状态。
- MemUnit：执行 Mem 类 micro-op，通过 Memory Subsystem 完成访问与同步，并返回访存/阻塞结果。

验收点
- 单条 micro-op 能独立执行且可被 ObsControl 观测。

---

## Step 6：SIMT Core（Context + Scheduler + Executor）

设计模块
- WarpContext / ThreadContext / CTAContext
- SMModel + CTAScheduler + WarpScheduler
- Executor（fetch/desc/expand/dispatch/commit）

参照
- arch_design.md：4.4（SIMT core）+ 第 5 章（端到端流程）。
- arch_modules_block.diagram.puml：SM→CtaSch→WarpSch→WarpCtx→ThrCtx→Exec；WarpSch→Exec(pick)。

要设计什么
- Context：
  - WarpContext：PC/mask/reconv stack/barrier 状态/退出标志。
  - ThreadContext：regs/preds/local（定义 local 地址空间的布局与生命周期）。
- Scheduler：
  - ready 判定（被 barrier/mem 阻塞则不可 pick）。
  - 策略接口（后续可插拔）。
- Executor.step_warp：
  - fetch：InstList[pc]
  - lookup/expand：DescRegistry + Expander
  - dispatch：Core/Ctrl/MemU
  - commit：pc 更新、warp done 检测、obs emit 点

接口
- WarpScheduler：从可运行 warp 集合中选择一个 ready warp（需定义 ready 判定）。
- Executor：推进被选中 warp 的执行并产生 StepResult，同时发出可观测事件。

验收点
- 不依赖 streaming runtime 的情况下，能“单 kernel/单 CTA/单 warp”跑到结束（用于验证 Executor/Units/Memory 的完整闭环）。

---

## Step 7：Runtime + Streaming（Stream/Queue/Deps）

设计模块
- Kernel Runtime & ABI
- StreamManager / StreamContext / CommandQueue
- DependencyTracker（events/waits）

参照
- arch_design.md：4.3（runtime+streaming）+ 第 5.2（streaming 调度）。
- arch_modules_block.diagram.puml：RT→SMgr→SCtx→CQ↔Dep，CQ→CEng/XEng。

要设计什么
- ABI：module load/entry lookup、param packing（`.param` layout）。
- Command 模型：COPY/KERNEL/EVENT_RECORD/EVENT_WAIT 的字段。
- DepTracker：ready 判定规则、completion token/回调契约。

接口
- Runtime：将 Host 侧操作表达为 Command 并入队到指定 Stream。
- Stream/Queue：保证 stream 内顺序，提供“取队首 + 判定是否可提交”的操作。
- DependencyTracker：维护 event/wait 的依赖与完成信号，使 Command 在满足依赖后可被提交到引擎。

验收点
- 同 stream 串行、跨 stream 依赖（event wait）能正确阻塞/放行。

---

## Step 8：Engines（ComputeEngine + CopyEngine）并把端到端串起来

设计模块
- CopyEngine
- ComputeEngine

参照
- arch_design.md：4.3.4/4.3.5（copy/compute engine）+ 第 5 章（端到端流程）。
- arch_modules_block.diagram.puml：CQ→CEng/XEng，CEng→SM，XEng→GMEM。

要设计什么
- CopyEngine：对 GMEM 的 DMA 读写（H2D/D2H/D2D/Memset）以及 completion 回调。
- ComputeEngine：
  - kernel submit → 建 grid/cta/warp/thread 上下文
  - 将 CTA 投递到 SMModel，并在完成时 signal DepTracker
- tick 模型定义：明确 `tick()` 的语义（逻辑步/延迟模型/进度推进），并与 Obs 的 `ts` 字段保持一致。

验收点
- 支持：memcpyAsync + launchKernel + event record/wait 的完整组合，且 trace 能复盘。

---

## 推荐把每一步的输出落到 doc_design/modules/

建议文件命名（可按需调整）：
- 00_contracts.md（Step 0）
- 01_observability.md（Step 1）
- 02_frontend.md（Step 2）
- 03_instruction_system.md（Step 3）
- 04_memory.md（Step 4）
- 05_units.md（Step 5）
- 06_simt_core.md（Step 6）
- 07_runtime_streaming.md（Step 7）
- 08_engines.md（Step 8）

每个模块文档的固定小节模板
- 职责（Responsibilities）
- 输入/输出（Inputs/Outputs）
- 内部执行流程（Internal Flow / State Machine）
- 与其它模块的数据/流程交互（Interfaces + Events）
- 不变量与边界条件（Invariants / Error handling）
- 可观测点（Obs events + counters）
- 验收用例（Smoke tests / Golden traces）
