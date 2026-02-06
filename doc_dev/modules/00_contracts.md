# 00 Contracts（实现落地：类型、序列化、诊断）

参照
- 抽象契约：`doc_design/modules/00_contracts.md`
- 架构主文档：`doc_design/arch_design.md` 第 3 章

落地目标
- 在代码中提供唯一权威的 Contracts 定义位置，避免在不同模块重复定义类型。
- 定义 JSON 序列化的稳定字段集合与错误/事件输出格式。

落地位置（代码）
- `src/common/` 下建立 contracts 子模块（文件名与语言无关，以目录结构为准）。
- 该子模块对外暴露：类型定义 + JSON 序列化/反序列化 + 校验辅助函数。

---

## 1. 类型落地清单

必须落地的核心类型
- IDs：`KernelId/CTAId/WarpId/ThreadId/SmId/StreamId/CmdId/EventId`。
- `LaneMask`：固定 width==warp_size。
- `Value`：lane-wise 承载，含 type tag。
- `AddrSpace`：global/shared/local/const/param。
- `InstRecord`：opcode/mods/pred/operands/dbg。
- `MicroOp`：kind/op/inputs/outputs/attrs/guard。
- `StepResult`、`MemResult`。
- `SourceLocation`、`Diagnostic`。
- `Event`。

统一命名与字段
- PC：`pc` 字段一律表示 `InstList index`。
- 指令索引：`inst_index` 与 `pc` 语义一致，二者在同一上下文下保持一致。

---

## 2. JSON 序列化规范

### 2.1 Event JSON（Trace 输出）

输出格式
- Trace 以 JSON lines 输出：每行一个 JSON 对象。

字段集合（固定）
```text
{
  "ts": number,
  "category": string,
  "action": string,
  "kernel_id": number|null,
  "cta_id": number|null,
  "warp_id": number|null,
  "thread_id": number|null,
  "sm_id": number|null,
  "stream_id": number|null,
  "cmd_id": number|null,
  "event_id": number|null,
  "pc": number|null,
  "lane_mask": string|null,
  "opcode_or_uop": string|null,
  "addr": number|null,
  "size": number|null,
  "extra": object|null
}
```

编码规则
- `lane_mask` 使用十六进制字符串表示（固定宽度，按 warp_size 对齐）。
- `addr` 使用无符号整数（可用 64-bit 范围）。
- `extra` 只允许 JSON object，并限制深度与体积（由实现校验）。

### 2.2 Diagnostic JSON（对外错误）
Diagnostic 允许被 CLI 直接输出为 JSON：
```text
{
  "module": string,
  "code": string,
  "message": string,
  "location": {"file": string, "line": number, "column": number} | null,
  "function": string|null,
  "inst_index": number|null
}
```

---

## 3. 配置结构（JSON）

配置拆分
- `HardwareSpec`：SM 数、warp_size、容量约束、严格检查开关。
- `ObsConfig`：类别开关、过滤、采样、断点条件。
- `RuntimeConfig`：tick 推进策略与并发推进开关。

约束
- 配置输入统一为 JSON。
- 所有配置字段必须有默认值与明确语义。

---

## 4. 错误码与模块名规范

模块名固定集合
- `common`, `observability`, `frontend`, `instruction`, `memory`, `units`, `simt`, `runtime`, `engines`, `cli`。

错误码命名规则
- `PTX_PARSE_*`：frontend parser
- `PTX_BIND_*`：frontend binder
- `DESC_*`：instruction registry/expander
- `MEM_*`：memory subsystem
- `EXEC_*`：simt/executor
- `RT_*`：runtime/deps

约束
- 任何对外错误必须采用上述模块名与错误码规则。
