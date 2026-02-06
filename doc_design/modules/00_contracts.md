# 00 Contracts（跨模块契约层）

说明
- 本文只描述抽象逻辑契约：类型、语义、输入输出与不变量。
- 设计基准：与 [doc_design/arch_modules_block.diagram.puml](../arch_modules_block.diagram.puml) 与 [doc_design/sequence.diagram.puml](../sequence.diagram.puml) 对齐；若文本与图冲突，以图为准。

本模块冻结跨模块共用的 Contracts，覆盖：Frontend → Instruction System → SIMT/Units → Memory → Runtime/Engines → Observability。

---

## 职责（Responsibilities）
- 定义跨模块共享的数据结构：`InstRecord`、`MicroOp`、`Value`、`LaneMask`、`AddrSpace`、各种 `Id`。
- 定义跨模块共享的结果/阻塞/错误/事件契约：`StepResult`、`MemResult`、`Diagnostic`、`Event`。
- 固化命名与语义约定：PC 以 `InstList index` 表示；label 在 Binder 阶段绑定到 index；Observability 单入口 `ObsControl.emit(Event)`；指令描述文件为 JSON。

---

## 输入/输出（Inputs/Outputs）
- 输入：无（作为全局契约被引用）。
- 输出：所有模块文档与实现引用的统一契约定义。

---

## 核心类型（Core Types）

### IDs
所有 ID 均为强类型整数标识（概念上不可混用）。

```text
KernelId, CTAId, WarpId, ThreadId, SmId
StreamId, CmdId, EventId
FuncId (optional)
```

语义约束
- `ThreadId` 表示 warp 内 lane 的逻辑 thread（与 lane index 对齐）。
- `WarpId` 在 CTA 范围内唯一；`CTAId` 在 kernel 范围内唯一。
- `CmdId` 在 Stream 范围内唯一；`EventId` 在 Runtime 范围内唯一。

### PC 与 InstList
```text
PC := int  // InstList index
InstList := vector<InstRecord>
label_index := map<label, PC>
```

约束
- 任何控制流跳转必须以 `PC`（InstList index）表达，不使用执行期字符串 label 查找。

### LaneMask
```text
LaneMask {
  width: int  // == warp_size
  bits:  bitset(width)
}
```

语义约束
- `bits[i]=0` 的 lane 在该步不产生读写副作用（寄存器/谓词/内存）。
- guard 与 active mask 组合后形成实际执行 mask：`exec_mask = warp_active_mask & uop.guard`。

### Value
Value 为 lane-wise 数据载体，至少覆盖：`u32/s32/u64/s64/f32`。

```text
Value {
  type: enum
  lanes: vector<Scalar>  // lanes.size == warp_size
}
```

语义约束
- 对 inactive lanes 的 `lanes[i]` 可保持旧值或置为未定义，但不得被读取并产生可见副作用。

### AddrSpace
```text
AddrSpace := global | shared | local | const | param
```

语义约束
- No-cache：所有 `LD/ST/ATOM` 访问最终都经由 `AddrSpaceManager` 路由到具体空间实现。

---

## 指令与 micro-op 契约

### InstRecord
InstRecord 是“可执行 + 可展开 + 可诊断”的基础承载单元（语义完整性要求由本契约保证）。

```text
InstRecord {
  opcode: string
  mods: { type_mod, rounding, saturate, space, flags... }
  pred: optional { pred_id, is_negated }
  operands: vector<Operand>
  dbg: optional { file, line, column }
}

Operand { kind: REG|PRED|IMM|ADDR|SYMBOL, type?, value }
```

约束
- Binder 阶段完成 symbol/label 的绑定与规范化，避免执行期字符串查找。
- `mods.space` 与 operand kinds 组合必须足以让 DescriptorRegistry 唯一匹配 `InstDesc`。

### InstDesc（来自 DescriptorRegistry）
InstDesc 为“指令语义与展开模板”的只读描述。

```text
InstDesc {
  key: { opcode, type_mod, mods, operand_kinds }
  flags: { has_side_effect, is_branch, is_memory, ... }
  uop_templates: vector<UopTemplate>
}
```

约束
- 指令描述文件格式固定为 JSON，并由 schema（JSON Schema 或等价约束）校验。

### MicroOp
MicroOp 是 Executor 与 Base Units 的唯一执行契约。

```text
MicroOp {
  kind: Exec | Control | Mem
  op: enum
  inputs: vector<OperandRef>
  outputs: vector<OperandRef>
  attrs: { type, width, space, semantics_flags... }
  guard: LaneMask
}
```

约束
- Executor 不直接实现“特殊语义”，一切语义拆解为 micro-ops 并由 Units 执行。
- `guard` 必须显式表达 predication 与掩码执行，避免隐式分支。

---

## 结果、阻塞与错误

### StepResult
Warp 单步执行的统一结果。

```text
StepResult {
  progressed: bool
  warp_done: bool
  blocked_reason: NONE|BARRIER|MEM|DEPENDENCY|ERROR
  diag: optional Diagnostic
}
```

### MemResult
MemUnit 与 Memory Subsystem 的交互结果。

```text
MemResult {
  progressed: bool
  blocked: bool
  fault: optional Diagnostic
}
```

### Diagnostic
所有错误与诊断统一结构。

```text
SourceLocation { file, line, column }
Diagnostic {
  module: string
  code: string
  message: string
  location: optional SourceLocation
  function: optional string
  inst_index: optional int
}
```

约束
- 所有错误必须可定位到：源文件位置或 inst_index（二者至少一个）。

---

## 观测事件契约（Event）

Observability 通过 `ObsControl.emit(Event)` 形成单一依赖面。

```text
Event {
  ts: optional logical tick
  category: STREAM|COPY|FETCH|EXEC|CTRL|MEM|COMMIT
  action: string
  ids: { kernel_id?, cta_id?, warp_id?, thread_id?, sm_id?, stream_id?, cmd_id?, event_id? }
  pc: optional PC
  lane_mask: optional LaneMask
  opcode_or_uop: optional string
  addr: optional u64
  size: optional int
  extra: optional JSON object
}
```

约束
- `extra` 只承载小体量结构化信息（便于 TraceBuffer 记录与后续分析）。
- `ts` 与系统 `tick()` 语义一致（见 08 Engines）。

---

## 不变量与边界条件（Invariants / Error handling）
- PC 永远指向 InstList 的合法范围或“warp_done”。
- `LaneMask.width == warp_size`；跨模块传递 lane mask 不改变宽度。
- 任意模块对外报告错误均使用 `Diagnostic`。
- 任意模块上报观测均通过 `ObsControl.emit(Event)`。

---

## 可观测点（Obs events + counters）
本模块定义事件字段标准，不直接产生事件；事件产生点在各模块文档中列出。

---

## 验收用例（Smoke tests / Golden traces）
- 文档引用校验：任何模块文档引用 Contracts 时字段含义明确，无需推断。
- 事件字段一致性：同一类事件在不同模块产生时字段齐全且含义一致。
