# PTX 指令执行虚拟机（VM）架构设计文档（Streaming + 模块化 + micro-op 组合）

> 目标：实现一个可配置、模块化的 NVIDIA PTX 指令执行虚拟机。  
> 特点：  
> 1) **轻量前端**：不构建完整 AST，仅构建 `Module/Function + Instruction List + Metadata`  
> 2) **SIMT 语义正确**：warp mask、分歧与 reconvergence、barrier/atom/fence  
> 3) **Streaming 仿真**：支持多 stream 命令队列、事件依赖、copy/compute 并发  
> 4) **不仿真 cache 架构**：内存模型为 AddrSpace + Shared + Global（线性内存）  
> 5) **指令执行可组合**：按指令描述文件将指令展开为 micro-ops，由基础执行单元执行  
> 6) **观测能力（必需）**：TraceBuffer + Counters + ObsControl（过滤/采样/开关）

设计基准
- 架构与模块关系以 doc_design 下的 PUML 图为准（尤其是 arch_modules_block.diagram.puml 与 sequence.diagram.puml）；若文本与图存在冲突，以图为准。
- 指令描述格式固定为 JSON。

---

## 1. 范围与非目标

### 1.1 功能范围
- 支持 PTX 指令覆盖范围（由指令描述 JSON 定义，可扩展）：
  - 基本算术/逻辑/比较/类型转换
  - predication（`@p`）、分支（`bra` 等）
  - global/shared/local/const/param 地址空间读写
  - barrier、fence、atomic 的功能正确语义（不做时序精确）
- 支持 Kernel Launch：grid/block/thread 组织与内建寄存器（`tid`, `ctaid`, `ntid`, `nctaid` 等）的提供
- Streaming 仿真：CUDA streams 风格命令队列
  - COPY (H2D/D2H/D2D/Memset)
  - KERNEL launch
  - EVENT record/wait（依赖）

### 1.2 非目标（明确不做）
- 不做 cache（L1/L2）与 cache 命中/替换等
- 不做周期精确 pipeline/scoreboard/占用率精确还原（保留扩展点，不改变现有模块边界）
- 架构不绑定特定 PTX 版本；指令与版本覆盖范围由 Frontend 能力与指令描述 JSON 决定

---

## 2. 顶层架构概览

### 2.1 模块分层
1) **Frontend（轻量）**：PTX 文本 → Module/Function + InstList + Metadata  
2) **Instruction System**：InstDesc Registry + Micro-op Expander  
3) **Runtime + Streaming**：Stream/Queue/Dependencies + Compute/Copy Engines  
4) **SIMT Core**：SM/CTA/Warp/Thread 上下文 + Warp 调度 + Executor  
5) **Base Compute Units（精简）**：ExecCore + ControlUnit + MemUnit  
6) **Memory Subsystem（No Cache）**：AddrSpace + Shared + Global + Fence  
7) **Observability（必需）**：ObsControl + TraceBuffer + Counters

---

## 3. 核心数据结构与约定

### 3.1 基本类型与命名
- **PC**：以 `InstList` 索引为 PC（整数 index），或用字节偏移也可，但推荐 index 简化
- **LaneMask**：warp 内活跃线程掩码（如 32-bit）
- **Value**：支持多类型（u32/s32/u64/s64/f32 等），建议用 tagged union 或 `std::variant`
- **AddrSpace**：枚举 { global, shared, local, const, param }
- **ID 统一语义**：kernel/cta/warp/thread/stream/event/cmd 等 ID 均为强类型的整数标识；文档中以 `KernelId/CTAId/WarpId/ThreadId/StreamId/EventId/CmdId/SmId` 表达。
- **统一诊断定位**：所有可报告错误必须携带 `SourceLocation { file, line, column }` 与 `inst_index`（若可用），并明确来源模块（frontend/instruction/runtime/simt/units/memory/observability）。

### 3.2 Module / Function 表示
```text
Module {
  functions: map<string, Function>
  globals:   GlobalSymbols (global memory objects, extern)
  consts:    ConstSymbols
  metadata:  PTX version, target, address sizes, ...
}

Function {
  name: string
  kind: entry|func
  regs: reg declarations + type info
  params: param layout
  shared: shared symbols layout
  insts: vector<InstRecord>
  label_index: map<label, pc_index>
  debug_meta: optional
}
````

### 3.3 指令记录（InstRecord）

要求：**执行需要的一切都在 InstRecord 或 metadata 可查**。

```text
InstRecord {
  opcode: string
  mods:   {type_mod, rounding, saturate, space, ...}
  pred:   optional predicate guard (e.g. @p / @!p)
  operands: vector<Operand>  (reg, imm, addr, symbol)
  dbg:    optional (line, file)
}
```

Operand（概念形态）

```text
Operand {
  kind: REG | PRED | IMM | ADDR | SYMBOL
  type: optional (u32/s32/u64/s64/f32/...)
  value: reg_id | pred_id | imm_value | addr_expr | symbol_id
}
```

### 3.4 micro-op（MicroOp）

micro-op 是 Executor 与 Base Units 的“执行契约”。

```text
MicroOp {
  kind: Exec | Control | Mem
  op:   enum (ADD, MUL, CVT, SETP, BRA, LD, ST, ATOM, FENCE, BAR, ...)
  inputs:  operand refs (register ids / immediates / computed addr)
  outputs: destination refs (register ids / predicate ids)
  attrs:   type/width/space/semantics flags
  guard:   LaneMask (predication/mask execution)
}
```

### 3.5 跨模块 Contracts（Step 0）

本小节冻结跨模块的通用契约，供后续所有模块设计与实现引用。

**结果与阻塞**

```text
StepResult {
  progressed: bool
  warp_done: bool
  blocked_reason: NONE|BARRIER|MEM|DEPENDENCY|ERROR
}

MemResult {
  progressed: bool
  blocked: bool
  fault: optional (reason + location)
}
```

**事件契约**

```text
Event {
  ts: optional (logical tick)
  category: STREAM|COPY|FETCH|EXEC|CTRL|MEM|COMMIT
  action: string   // 例如 cmd_enq/copy_submit/copy_complete/kernel_submit/kernel_complete/uop
  kernel_id, cta_id, warp_id, thread_id: optional
  stream_id, cmd_id, event_id: optional
  pc: optional (InstList index)
  lane_mask: optional
  opcode_or_uop: optional
  addr, size: optional
  extra: optional (small JSON object)
}
```

**错误与诊断**

```text
Diagnostic {
  module: string
  code: string
  message: string
  location: optional SourceLocation
  function: optional string
  inst_index: optional int
}
```

---

## 4. 模块设计（处理 / 输入 / 输出 / 接口）

> 下面按“模块 → 职责 → 输入/输出 → 关键接口 → 与其它模块交互”描述。

---

## 4.1 Frontend（轻量）

### 4.1.1 PTX Lexer/Parser（streaming）

**职责**

* 逐 token 解析 PTX 文本
* 识别 directives（.version/.target/.address_size/.entry/.func/.reg/.param/.shared/.global 等）
* 识别指令行：opcode + modifiers + operands + predication

**输入**

* PTX 文本（string / stream）

**输出**

* token 流 / 临时指令记录流（给 Binder）

**接口**

```text
parse(ptx_text) -> ParsedStream   // 或者回调式 onDirective/onInst
```

**交互**

* 向 Binder 输出 directives 与 InstRecord（未绑定符号）

---

### 4.1.2 Directive/Metadata Binder

**职责**

* 建立符号表与元数据：

  * 寄存器声明（类型/数量）
  * 参数布局（param symbol → offset/size）
  * shared/global/const 符号布局
  * label 绑定与 `label_index` 构建
* 做基本一致性检查：

  * 操作数类型/空间合法性
  * 立即数范围、对齐声明（如有）

**输入**

* Parser 输出的 directives + raw InstRecords

**输出**

* bound InstRecords（符号、label 已可解析）
* Function/Module 元数据

**接口**

```text
bind(parsed_stream) -> Module
```

**交互**

* 与 Module Builder 交接已绑定结果

---

### 4.1.3 Module Builder

**职责**

* 生成最终 `Module/Function` 对象：

  * `Function.insts`（InstList）
  * `label_index`
  * `reg/param/shared/global` 元数据

**输入**

* Binder 输出的函数级数据

**输出**

* `Module`

**接口**

```text
build_module(bound_data) -> Module
```

---

## 4.2 Instruction System

### 4.2.1 Inst Descriptor Registry

**职责**

* 维护“PTX 指令语义与 micro-op 展开规则”
* 支持按 `(opcode + type_mod + modifiers + operand_kinds)` 查找 descriptor

**输入**

* descriptor 文件（JSON）

**输出**

* InstDesc 查询服务

**接口**

```text
load_desc(files) -> void
lookup(inst_record) -> InstDesc
```

**InstDesc 需要包含**

* canonical opcode
* operand pattern（dst/src/addr 等）
* 语义标志（rounding/sat/space/volatile/...)
* micro-op 模板（生成 micro-ops 的规则）

---

### 4.2.2 Micro-op Expander

**职责**

* 将 InstRecord + InstDesc 展开为 micro-ops（Exec/Control/Mem 三类）
* 负责：

  * 将 predication 转为“guard”附着在 micro-ops 上
  * 将复杂操作拆成多个 micro-op（例如：地址计算 + load + 类型扩展 + 写回）

**输入**

* InstRecord
* InstDesc

**输出**

* `vector<MicroOp>`

**接口**

```text
expand(inst_record, inst_desc) -> vector<MicroOp>
```

---

## 4.3 Runtime + Streaming

### 4.3.1 Kernel Runtime & ABI

**职责**

* 提供 host API：createStream、memcpyAsync、launchKernel、event record/wait、device sync
* 管理 kernel 参数打包与 `.param` 布局绑定
* 管理 module 加载与 entry 函数定位（entry PC）

**输入**

* Host API 调用
* Module（来自 Frontend）

**输出**

* Command 入队到 Stream Queue
* kernel/const/param 绑定状态

**接口（示意）**

```text
stream_t create_stream()
event_t  create_event()
memcpy_async(stream, dst, src, bytes, kind)
record_event(stream, event)
wait_event(stream, event)
launch_kernel(stream, module, entry, grid, block, params)
synchronize() / stream_synchronize(stream)
```

---

### 4.3.2 Stream Manager / Stream Context / Command Queue

**职责**

* 每个 stream 一个 FIFO command queue（保持 stream 内顺序）
* 支持命令：

  * COPY(H2D/D2H/D2D/Memset)
  * EVENT_RECORD
  * EVENT_WAIT
  * KERNEL_LAUNCH

**输入**

* Runtime 的 enqueue

**输出**

* ready 命令提交给 engines（Compute/Copy）

**接口**

```text
enqueue(stream, Command)
peek_ready(stream, dep_tracker) -> optional<Command>
pop(stream) -> Command
```

---

### 4.3.3 Dependency Tracker（events/waits）

**职责**

* 管理 event 的 signal 状态
* 判定 Command 是否 ready：

  * EVENT_WAIT 在 event signal 前不 ready
  * stream 内顺序保证前序未完成则后续不 ready
* 向 engines 提供同步原语：提交完成回调 → signal token

**输入**

* Command
* completion callbacks（copy done / kernel done）

**输出**

* ready/not-ready 判定
* event signal 状态更新

**接口**

```text
is_ready(stream, cmd) -> bool
on_complete(cmd_id)
record_event(E)
wait_event(E) // consumes or checks
```

---

### 4.3.4 Copy Engine（DMA）

**职责**

* 执行 H2D/D2H/D2D/Memset
* 可采用同步完成或延迟模型；语义需与 DependencyTracker 的完成信号一致

**输入**

* COPY Command

**输出**

* 对 Global Memory 的写/读
* completion token → DepTracker

**接口**

```text
submit_copy(cmd) -> cmd_id
tick() -> progress (optional)
```

与 Memory 交互：

* 写入 GMEM（线性内存）
* 若有 host memory 模拟，需 HostMem 作为源/目的

---

### 4.3.5 Compute Engine（kernel dispatcher）

**职责**

* 接收 KERNEL_LAUNCH，配置 grid/cta/thread 上下文
* 将 CTA 任务投递到 SM Model（可多 SM 并行）

**输入**

* Kernel Command

**输出**

* SM 中的 kernel work items
* completion token → DepTracker

**接口**

```text
submit_kernel(cmd) -> cmd_id
tick() -> progress (optional)
```

---

## 4.4 SIMT Core

### 4.4.1 SM Model / CTA Scheduler / Warp Scheduler

**职责**

* 资源抽象（不做周期精确也需容量约束）：

  * 每 SM 最大 CTA 数、最大 warp 数、寄存器/共享内存容量（可配置为严格检查或宽松约束）
* CTA Scheduler：

  * 将 CTA 分配到 SM，创建 CTA 上下文
* Warp Scheduler：

  * 选择 ready warp 交给 Executor 执行一步（step-based）

**接口**

```text
sm.enqueue_kernel_work(kernel_work)
cta_scheduler.assign()
warp_scheduler.pick_ready_warp() -> warp_id
```

---

### 4.4.2 Warp/Thread Context

**WarpContext（必须）**

* PC（inst index）
* active mask（lane mask）
* reconvergence stack（用于分歧合流）
* barrier 状态（block 内到达计数/等待 mask）
* 异常/退出状态

**ThreadContext**

* Registers（按 lane 存储）
* Predicates（按 lane 存储）
* Local memory base（若实现 local）
* builtin regs（tid/ctaid…可按需即时计算或缓存）

---

### 4.4.3 Executor（fetch/decode/dispatch）

**职责**

* 取指（从 InstList 取 InstRecord）
* descriptor lookup + expand -> micro-ops
* 执行 micro-ops：按 kind 分发到 Core/Ctrl/MemU
* 统一处理 predication（guard lane mask）
* commit：更新 PC、写回完成、处理 warp 完成退出

**输入**

* WarpScheduler 提供的 warp_id
* Function InstList（Module/Function）

**输出**

* 更新 Warp/Thread 状态
* 触发 Mem 子系统访问
* （Streaming）kernel 完成信号（通过 Compute Engine）

**接口**

```text
step_warp(sm_state, warp_id) -> StepResult
```

StepResult（建议包含）：

* progressed?（是否执行了）
* warp_done?（是否结束）
* blocked_reason?（barrier/mem 等）

---

## 4.5 Base Compute Units（精简版）

### 4.5.1 ExecCore（Reg/Pred IO + ALU/FP/CVT/Compare/Select）

**职责**

* 执行 Exec 类 micro-op：

  * integer/fp 算术
  * compare / setp
  * cvt / pack/unpack
  * selp / predicated select
* 内建寄存器读取（如 `%tid.x`）可在此或 Executor 统一提供

**输入**

* Exec micro-op
* lane mask
* ThreadContext（寄存器/谓词）

**输出**

* 寄存器写回、谓词更新

**接口**

```text
run_exec_uop(uop, lane_mask, thread_ctx) -> void
```

---

### 4.5.2 ControlUnit（Branch/PC/Reconvergence）

**职责**

* 执行 Control 类 micro-op：

  * bra / call/ret
* 维护 reconvergence stack：

  * 分歧：push (reconverge_pc, fallthrough_mask, taken_mask, ...)
  * 合流：pop 并恢复 mask/PC
* 更新 WarpContext：PC、mask

**输入**

* Control micro-op
* lane mask / predicate outcomes
* WarpContext（PC/mask/stack）
* label_index（用于 target PC）

**输出**

* WarpContext 更新（new PC/mask/stack）

**接口**

```text
run_ctrl_uop(uop, warp_ctx, label_index) -> void
```

---

### 4.5.3 MemUnit（LD/ST/ATOM/FENCE/BARRIER）

**职责**

* 执行 Mem 类 micro-op：

  * ld/st：生成地址、访问 AddrSpaceManager
  * atom：原子读改写
  * fence：可见性/排序（功能正确实现）
  * barrier：block 内同步（bar.sync）
* 将访存结果写回寄存器

**输入**

* Mem micro-op
* lane mask
* Thread/Warp/CTA 上下文（用于 shared 基址、barrier 状态）
* Memory subsystem handle

**输出**

* 寄存器写回（ld/atom old）
* barrier 状态更新 / block/unblock
* fence 生效

**接口**

```text
run_mem_uop(uop, lane_mask, ctx, memsys) -> MemResult
```

---

## 4.6 Memory Subsystem（No Cache）

### 4.6.1 AddrSpace Manager

**职责**

* 将 PTX 地址空间请求映射到具体内存对象：

  * global → GlobalMemory
  * shared → SharedMemory（按 CTA）
  * local → per-thread local memory
  * const/param → read-only buffers
* 做基础检查：

  * 对齐/越界（可配置：strict 或 permissive）
  * 类型宽度与字节数一致性

**接口**

```text
read(space, addr, size, lane_mask) -> vector<Value>
write(space, addr, size, values, lane_mask) -> void
atomic(op, space, addr, val, lane_mask) -> vector<Value>
fence(kind, scope) -> void
barrier_sync(cta_id, barrier_id, lane_mask) -> block/unblock
```

### 4.6.2 Shared Memory

* CTA 私有 byte-array
* 不仿真 bank 冲突，仅关注语义正确

### 4.6.3 Global Memory

* 线性 byte-array（或 sparse map）
* 支持 host<->device 复制（Copy Engine）

### 4.6.4 Fence/Consistency

* 最低保证：同一线程/同一 CTA 的顺序可见性按 PTX fence 语义保守实现
* 若不做跨 SM/多 kernel 并发一致性，需在文档中明确假设

---

## 4.7 Observability（必需）

### 4.7.1 ObsControl

**职责**

* 提供统一 `emit(event)` API（模块只依赖这一点）
* 事件过滤/采样/开关：

  * enable categories（fetch/exec/mem/ctrl/stream/copy）
  * sampling rate
  * break conditions（例如：PC/warp_id/addr 匹配触发暂停）

**接口**

```text
emit(Event e) -> void
configure(ObsConfig cfg) -> void
should_record(e) -> bool
```

### 4.7.2 TraceBuffer（ring buffer / log）

**职责**

* 记录必要事件，供离线分析/调试
* 采用 ring buffer 避免无限增长
* 支持 flush 导出

**接口**

```text
record(Event e)
flush() -> TraceDump
```

### 4.7.3 Counters（基础统计）

**职责**

* 计数器：

  * inst_fetch_count, uop_exec_count, mem_uop_count
  * branch_count, divergence_count
  * barrier_count, atom_count
  * copy_bytes, kernel_launch_count
* 可按 kernel/SM/warp 粒度

**接口**

```text
inc(counter_id, amount=1)
snapshot() -> Stats
reset()
```

### 4.7.4 Event（建议字段）

```text
Event {
  ts: optional (logical tick)
  category: STREAM|COPY|FETCH|EXEC|CTRL|MEM|COMMIT
  kernel_id, cta_id, warp_id
  pc
  lane_mask
  opcode/uop_kind
  addr, size (mem)
  extra: small payload
}
```

---

## 5. 执行流程（端到端）

### 5.1 启动与加载

1. Runtime 接收 `launchKernel`（可能来自某 stream）
2. Frontend 解析 PTX：Parser → Binder → Module Builder
3. Runtime 取得 entry Function 与 entry PC（通常为 0）
4. Command 入队到 Stream Queue

### 5.2 Streaming 调度

1. DependencyTracker 扫描各 stream 队首 command，判断 ready
2. ready 的 COPY → Copy Engine；ready 的 KERNEL → Compute Engine
3. COPY/KERNEL 完成后回调 DepTracker，signal token / event

### 5.3 Kernel 执行（Compute Engine + SIMT）

1. Compute Engine 建立 grid/CTA/warp/thread 上下文
2. CTA Scheduler 分配 CTA 到 SM
3. Warp Scheduler 选取 ready warp
4. Executor：

   * fetch InstRecord at PC
   * lookup InstDesc
   * expand → microOps
   * dispatch:

     * Exec uops → ExecCore
     * Ctrl uops → ControlUnit（更新 PC/mask/stack）
     * Mem uops → MemUnit（访问内存/同步/原子/栅栏）
   * commit
5. 所有 CTA 完成 → kernel complete → DepTracker signal

---

## 6. 配置点（可配置硬件/系统行为）

### 6.1 HardwareSpec（示意）

```json
{
  "sm_count": 4,
  "warp_size": 32,
  "max_cta_per_sm": 8,
  "max_warps_per_sm": 64,
  "regs_per_sm": 65536,
  "shared_bytes_per_sm": 65536,
  "strict_memory_checks": true,
  "local_memory_enabled": false,
  "streaming": {
    "enable_copy_engine": true,
    "enable_compute_engine": true,
    "allow_copy_compute_overlap": true
  }
}
```

### 6.2 Observability 配置

```json
{
  "obs": {
    "enable": true,
    "categories": ["STREAM", "COPY", "FETCH", "MEM", "CTRL"],
    "sampling": 1.0,
    "trace_buffer_size": 100000,
    "break": {
      "enabled": false,
      "pc_equals": 1234
    }
  }
}
```

---

## 7. 模块接口汇总（便于实现对齐）

### 7.1 Frontend

* `Module parse_and_build(ptx_text)`

### 7.2 Instruction System

* `InstDesc lookup(InstRecord)`
* `vector<MicroOp> expand(InstRecord, InstDesc)`

### 7.3 Streaming Runtime

* `enqueue(stream, Command)`
* `is_ready(stream, Command)`
* `submit_copy(Command)`
* `submit_kernel(Command)`

### 7.4 SIMT

* `enqueue_kernel_work(kernel_work)`
* `warp_id pick_ready_warp()`
* `StepResult step_warp(sm_state, warp_id)`

### 7.5 Base Units

* `ExecCore.run_exec_uop(uop, lane_mask, thread_ctx)`
* `ControlUnit.run_ctrl_uop(uop, warp_ctx, label_index)`
* `MemUnit.run_mem_uop(uop, lane_mask, ctx, memsys)`

### 7.6 Memory

* `read/write/atomic/fence/barrier_sync`

### 7.7 Observability

* `ObsControl.emit(Event)`
* `TraceBuffer.record/flush`
* `Counters.inc/snapshot/reset`

---

## 8. 扩展与兼容方向（保持架构不改的前提下）

1. 指令覆盖扩展：增加更多指令类别、类型、地址空间语义（由 JSON descriptor 驱动）
2. local memory + call/ret：补齐栈帧/ABI 等必要语义并保持与 micro-op 契约一致
3. 一致性模型：明确跨 SM 可见性与并发 kernel 语义边界并扩展 Fence/Atomic 规则
4. 时序模型：在 WarpScheduler/MemUnit/Engines 上挂接延迟与队列（不改变功能语义）
5. PTX 级 streaming：将 cp.async/pipeline 等能力表示为 MemUnit 的额外 micro-op 语义

---

## 9. 验收场景（建议覆盖）

* Streaming：多 stream 命令队列、EVENT record/wait 依赖、COPY 与 KERNEL 的提交与完成信号
* SIMT：predication、分歧/合流（reconvergence）、bar.sync（block 内同步）
* Memory：global/shared/local/const/param 读写、atomic、fence（功能正确语义）
* Observability：TraceBuffer 与 Counters 的事件与统计一致性、过滤/采样/断点触发行为

---

## 10. 备注：关键设计约束（务必落实到代码约定）

* **PC 统一为 InstList index**，label 预绑定到 index，分支不再做字符串解析
* **micro-op 是唯一执行契约**：Executor 不直接做“特殊语义”，都落到 uop
* **Observability 单入口**：各模块只依赖 `ObsControl.emit()`，方便全局开关/优化
* **No cache**：所有 ld/st/atom 直接到 AddrSpaceManager → Shared/Global

## 11. 可扩展与优化方向（不破坏现有架构）

本章节描述**在不推翻当前架构前提下**可插拔、按配置启用的优化方向。每一项都对应现有模块的**扩展点**，避免“重构式升级”。

---

### 11.1 前端与指令表示层优化

#### 11.1.1 预规范化与指令折叠
**动机**  
减少执行期的 descriptor lookup 与 micro-op 展开成本。

**方向**
- 在 Frontend/Binder 阶段进行指令规范化：
  - 将语义等价的指令变体（如 `.u32/.s32` 在部分场景）折叠为 canonical form
  - 预处理 predication（将 `@p inst` 转换为显式 guard 元数据）
- 对常见指令（如 `mov`, `add`, `mul`）预展开 micro-op 模板并缓存

**扩展点**
- `InstRecord` 增加 `canonical_opcode`
- `MicroOp` 增加“pre-expanded”标志

---

#### 11.1.2 轻量 CFG 与静态 reconvergence 提示
**动机**  
加速 ControlUnit 中的分歧/合流处理，减少运行期栈操作。

**方向**
- 在 Binder 阶段通过配置启用轻量 CFG：
  - 仅识别 `bra`、fall-through、label
  - 标记静态合流点（post-dominator 近似）
- 将 reconvergence hint 写入 InstRecord metadata

**扩展点**
- ControlUnit 优先使用 hint；缺失时退回动态 reconvergence 栈

---

### 11.2 micro-op 与执行路径优化

#### 11.2.1 micro-op 融合（uop fusion）
**动机**  
减少 Executor→BaseUnit 的 dispatch 次数。

**方向**
- 将常见组合融合为单个复合 uop：
  - `addr_calc + ld`
  - `setp + bra`
  - `cvt + alu`
- 在 Micro-op Expander 中识别并生成 fused uop

**扩展点**
- `MicroOp.kind` 扩展为 `FUSED_EXEC` / `FUSED_MEM`
- ExecCore/MemUnit 内部支持 fused 执行路径

---

#### 11.2.2 Warp 内向量化执行
**动机**  
提高解释执行效率（尤其在 host CPU 上）。

**方向**
- 将 lane-wise 标量循环替换为：
  - SIMD（SSE/AVX）批处理
  - 位掩码批量操作（mask algebra）
- ExecCore 对 arithmetic/cmp/cvt 提供 vectorized backend

**扩展点**
- ExecCore 内部多实现（scalar / vectorized）
- 运行时按 host 能力选择

---

### 11.3 SIMT 调度与执行优化

#### 11.3.1 多 warp 并行推进（batch stepping）
**动机**  
降低调度与函数调用开销。

**方向**
- WarpScheduler 一次返回 N 个 ready warp
- Executor 以 batch 方式推进多个 warp 的一步或一个基本块

**扩展点**
- `pick_ready_warps(N)` 新接口
- Executor 增加 batch step 模式

---

#### 11.3.2 基于阻塞原因的调度优化
**动机**  
避免反复尝试不可前进的 warp。

**方向**
- WarpContext 记录阻塞原因：
  - barrier wait
  - pending memory / atomic
- WarpScheduler 按原因分类队列

**扩展点**
- `WarpContext.block_reason`
- Scheduler 策略插件

---

### 11.4 Memory Subsystem 优化（仍然 No-Cache）

#### 11.4.1 批量内存访问合并
**动机**  
减少 AddrSpaceManager 调用次数。

**方向**
- 对同一 warp 的 ld/st：
  - 合并连续地址访问
  - 合并同空间、同宽度操作
- MemUnit 生成 batched memory requests

**扩展点**
- `MemUnit.run_mem_uop` 返回 batched request
- AddrSpaceManager 支持 vector read/write

---

#### 11.4.2 更精细的 fence / atomic scope
**动机**  
避免过度保守的全局同步。

**方向**
- 按 PTX scope（cta/cluster/gpu/system）区分可见性
- 在单 kernel / 单 SM 场景下弱化 fence 影响

**扩展点**
- Fence 元数据
- AddrSpaceManager 内部可见性策略表

---

### 11.5 Streaming 与并发优化

#### 11.5.1 Copy / Compute 多实例引擎
**动机**  
更接近真实 GPU 的并发行为。

**方向**
- 支持多个 Copy Engine 实例
- Compute Engine 支持多个 kernel 并行驻留（资源允许）

**扩展点**
- Engine pool（而非单实例）
- DependencyTracker 支持多完成源

---

#### 11.5.2 Stream 优先级与调度策略
**动机**  
模拟 CUDA stream priority。

**方向**
- StreamContext 增加 priority 字段
- 调度 ready commands 时考虑优先级

**扩展点**
- StreamScheduler（在 Runtime 层）
- 可插拔调度策略

---

### 11.6 Observability 的低开销与增强模式

#### 11.6.1 观测零开销 fast-path
**动机**  
在关闭观测时，执行路径完全不受影响。

**方向**
- ObsControl.emit() 在 disable 时编译/运行期内联为 no-op
- Counters 可按编译期宏完全裁剪

**扩展点**
- ObsConfig.freeze() → 固定行为，允许 aggressive inlining

---

#### 11.6.2 条件触发式深度观测
**动机**  
只在“异常/感兴趣点”才付出代价。

**方向**
- 条件触发：
  - PC == X
  - warp_id == Y
  - addr ∈ range
- 触发后：
  - 打开 TraceBuffer
  - 提升采样率
  - 暂停执行供调试

**扩展点**
- ObsControl 条件表达式
- Executor 检查触发点

---

### 11.7 时序与性能模型（插件化扩展）

#### 11.7.1 轻量时序模型
**动机**  
提供相对性能趋势，而非周期精确。

**方向**
- 给 micro-op 绑定抽象 latency
- WarpScheduler 使用“可用时间戳”推进

**扩展点**
- `MicroOp.latency`
- Scheduler 时间轴插件

---

#### 11.7.2 周期精确模型（长期）
**动机**  
架构研究与性能预测。

**方向**
- Pipeline/scoreboard/bank 冲突建模
- cache 模型作为完全独立插件引入

**扩展点**
- 与现有架构并存，不污染基础路径

---

### 11.8 语言与工程层面的优化

#### 11.8.1 多语言后端绑定
**方向**
- 核心 VM 用 C++/Rust
- Python/Lua 作为控制与观测脚本层

---

#### 11.8.2 快照与回放（deterministic replay）
**动机**
- 精确复现 bug / 分歧行为。

**方向**
- Warp/Memory/Stream 状态快照
- TraceBuffer 驱动 replay

---

### 11.9 演进原则（强烈建议遵守）

- **任何优化都不得改变 micro-op 语义**
- **不开启优化 = 行为与当前版本完全一致**
- **所有优化必须可通过配置启用/关闭**
- **优先在模块内部扩展，不跨层“偷逻辑”**

> 如果这些原则被破坏，维护成本会指数级上升。

---