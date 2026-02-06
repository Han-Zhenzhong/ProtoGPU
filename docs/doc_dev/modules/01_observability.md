# 01 Observability（实现落地：ObsControl/TraceBuffer/Counters）

参照
- 抽象设计：`doc_design/modules/01_observability.md`
- 事件契约：`doc_dev/modules/00_contracts.md`（Event JSON）

落地目标
- 所有模块只依赖 `ObsControl.emit(Event)`。
- TraceBuffer 输出 JSON lines；Counters 输出 JSON stats。

落地位置（代码）
- `src/observability/`
  - `ObsControl`
  - `TraceBuffer`
  - `Counters`

---

## 1. API 落地（对外）

ObsControl
```text
configure(obs_config_json) -> void
emit(Event) -> void
```

TraceBuffer
```text
record(Event) -> void
flush_json_lines() -> string
clear() -> void
```

Counters
```text
inc(counter_id, amount=1, scope={kernel_id?, sm_id?}) -> void
snapshot_json(scope_selector?) -> object
reset(scope_selector?) -> void
```

约束
- `ObsControl.emit()` 允许被高频调用；当 obs 关闭时不产生 Trace/Counters 更新。
- TraceBuffer 满时策略由配置字段控制并在 Stats 中可见（例如 overflow_count）。

---

## 2. Trace 输出稳定性

输出规则
- Trace 的每行必须是单个 Event JSON 对象。
- 字段集合固定，未设置字段输出 `null`。

落地约束
- `category/action` 必须来自受控集合（由代码常量定义），禁止自由拼接导致统计与过滤失效。

---

## 3. Counters scope 聚合

scope
- global
- per kernel
- per SM

规则
- 相同 counter_id 在不同 scope 下分别累加。
- snapshot 的输出 JSON 结构固定（keys 不随运行变化）。

---

## 4. Break 条件

ObsConfig.break
- 断点条件表达落地为结构化 JSON（字段+比较符+值），由 ObsControl 解释执行。
- 断点触发后的行为落地为状态：
  - 标记 `break_triggered=true`
  - CLI 退出码与 Diagnostic 输出明确
