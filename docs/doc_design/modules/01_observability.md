# 01 Observability（ObsControl + TraceBuffer + Counters）

说明
- 本文只描述抽象逻辑：事件面、配置、记录与统计语义。
- 设计基准：与 [doc_design/arch_modules_block.diagram.puml](../arch_modules_block.diagram.puml) 与 [doc_design/sequence.diagram.puml](../sequence.diagram.puml) 对齐；若文本与图冲突，以图为准。

---

## 职责（Responsibilities）
- 提供统一依赖面：所有模块只依赖 `ObsControl.emit(Event)`。
- 以配置驱动的过滤/采样/断点条件，控制 Trace 与 Counters 的开销与覆盖范围。
- 将系统行为落为可复盘的事件序列（TraceBuffer）与可对比的统计（Counters）。

---

## 输入/输出（Inputs/Outputs）
- 输入：各模块产生的 `Event`（见 00 Contracts）。
- 输出：
  - TraceBuffer：可导出的事件序列（建议 JSON lines）。
  - Counters：可快照的统计值（可按 scope 聚合）。

---

## 内部执行流程（Internal Flow）

### 事件处理管线
```text
emit(Event e)
  -> normalize (补齐缺省字段、规范 action)
  -> filter (category/id/pc/addr)
  -> sample (rate)
  -> record? (TraceBuffer)
  -> count?  (Counters)
  -> break?  (break conditions)
```

### 配置模型
```text
ObsConfig {
  enabled_categories: set<Category>
  enabled_actions: optional set<string>
  filter: {
    kernel_id?, cta_id?, warp_id?, stream_id?, cmd_id?, event_id?
    pc_range?: [lo, hi]
    addr_range?: [lo, hi]
  }
  sampling: { rate: float in (0,1] }
  break_conditions: optional list<Condition>
}
```

Condition（概念）
- `pc == X` / `warp_id == Y` / `addr in [A,B]` / `action == "commit"` 等。

---

## 接口契约（Interfaces + Events）

### ObsControl
```text
configure(ObsConfig) -> void
emit(Event) -> void
```

语义
- `emit` 必须是无副作用且可关闭的：关闭时不产生 Trace/Counters 更新。
- `emit` 不得反向依赖业务模块（避免环依赖）。

### TraceBuffer
```text
record(Event) -> void
flush() -> TraceDump
clear() -> void
```

TraceDump（建议）
- 导出格式：JSON lines（每行一个 Event JSON）。
- flush 语义：返回当前缓冲区的顺序事件并清空（或按配置保留）。

Ring buffer 行为
- 满时策略由配置决定：覆盖最旧 / 丢弃最新，并产生 `trace_overflow` 事件或 counter。

### Counters
```text
inc(counter_id, amount=1, scope={kernel_id?, sm_id?}) -> void
snapshot(scope_selector?) -> Stats
reset(scope_selector?) -> void
```

计数器集合（建议基线集合）
- Streaming：`cmd_enq`, `copy_submit`, `copy_complete`, `kernel_submit`, `kernel_complete`, `event_record`, `event_wait`。
- Executor：`fetch`, `commit`, `uop_exec`, `uop_ctrl`, `uop_mem`。
- Memory/Sync：`ld`, `st`, `atom`, `fence`, `barrier`。

---

## 不变量与边界条件（Invariants / Error handling）
- 单入口：除 `ObsControl.emit()` 外，不允许业务模块直接依赖 TraceBuffer/Counters。
- 事件顺序：同一调用路径内产生的事件在 TraceBuffer 中保持调用顺序。
- 事件字段：满足 00 Contracts 的 Event 字段约束；缺省字段必须有明确规则。

---

## 可观测点（Obs events + counters）

事件分类（与 PUML 对齐）
- `STREAM`：cmd_enq / ready / wait_done
- `COPY`：copy_submit / copy_complete
- `FETCH`：fetch
- `EXEC`：exec_uop
- `CTRL`：ctrl_uop
- `MEM`：mem_uop（含 ld/st/atom/fence/barrier 的细分 action）
- `COMMIT`：commit

建议在 `extra` 中携带
- Streaming：bytes、copy kind、kernel entry 名称
- Mem：space、addr、size、fault
- Ctrl：target_pc、reconv stack depth

---

## 验收用例（Smoke tests / Golden traces）
- 事件面连通：运行端到端流程时，Trace 中同时出现 STREAM/COPY/KERNEL/FETCH/EXEC/MEM/COMMIT。
- 过滤生效：配置 `warp_id==0` 后 Trace 只记录该 warp 的事件。
- 采样生效：sampling rate 降低时，Counters 仍可完整统计，Trace 仅保留采样结果。
