# 05 Dev：Observability 与输出契约（实现落点）

本文对应设计文档 [docs/doc_design/ptx6.4_sm70/05_observability_and_output_contract.md](../../doc_design/ptx6.4_sm70/05_observability_and_output_contract.md)。它描述当前实现中 **Diagnostic / trace / stats** 的真实字段、生成路径与写出位置，并明确与“对外交付契约化/版本化”的差距（方便后续按 plan Step 4 收敛）。

---

## 1) Diagnostic：对外 API 的一部分（当前形态与来源）

数据结构：`include/gpusim/contracts.h`

`struct Diagnostic { module, code, message, location?, function?, inst_index? }`

对外返回路径：
- `Runtime::*` 返回 `RunOutputs`（`include/gpusim/runtime.h`），其中：
  - `RunOutputs.sim.completed`（bool）
  - `RunOutputs.sim.diag`（`std::optional<Diagnostic>`）

典型 diag 产生点（示例）：
- `src/simt/simt.cpp`：`E_MEMORY_MODEL`、`E_DESC_MISS`、`E_PRED_OOB`、`E_DIVERGENCE_UNSUPPORTED`、`E_NEXT_PC_CONFLICT`...
- `src/units/*`：uop 形态/不支持/对齐/OOB 等（module 常见 `units.exec/units.mem/units.ctrl`）
- `src/runtime/workload.cpp`：workload schema/ref/args 错误（module=`runtime`）

当前“可定位性”现状：
- SIMT 层的大部分诊断会填 `function=kernel.name`，并在很多路径上填 `inst_index=warp.pc`。
- Unit 层诊断通常不带 `inst_index`（因为 unit 不知道 PC），也不带 lane id。
- `location`（file/line/column）依赖前端是否填充 `InstRecord.dbg`；目前很多诊断仍是 `nullopt`。

CLI 行为（可观察对外交付的一部分）：
- `src/apps/cli/main.cpp` 会把 `outputs.sim.diag` 打印到 stderr（module:code message），并在存在时打印 `location`/`inst_index`。

---

## 2) trace：事件模型、采集与 JSONL 序列化

### 2.1 Event 数据结构

定义：`include/gpusim/contracts.h`

`struct Event` 关键字段：
- `ts`（u64）
- `category`（Stream/Copy/Fetch/Exec/Ctrl/Mem/Commit）
- `action`（字符串，如 `RUN_START`/`FETCH`/`UOP`/`COMMIT`）
- 上下文字段（optional）：`sm_id/cta_id/warp_id/thread_id/stream_id/cmd_id/event_id/kernel_id`
- 指令相关（optional）：`pc`、`lane_mask`、`opcode_or_uop`
- 内存相关（optional）：`addr`、`size`
- 扩展字段（optional）：`extra_json`

### 2.2 采集：ObsControl + ring buffer

实现：
- `include/gpusim/observability.h` / `src/observability/observability.cpp`

行为：
- `ObsControl.emit(e)`：在 `obs.enabled==true` 时写入 `TraceBuffer` ring（容量 `ObsConfig.trace_capacity`）。
- `ObsControl.counter(key)`：在 `obs.enabled==true` 时累加 counters。
- `TraceBuffer.snapshot()`：返回按时间顺序的事件数组（满环时从 `head_` 起算）。

### 2.3 事件产生点（当前 Tier‑0 覆盖的最小集）

主要集中在 `src/simt/simt.cpp`：
- `RUN_START`（category=Stream）：一次性写入 sim 配置摘要到 `extra_json`（包含 profile/components/sm_count/parallel/deterministic）。
- `FETCH`（category=Fetch）：每条 inst fetch 时发出（带 `pc/lane_mask/opcode`）。
- `UOP`（category=Exec/Ctrl/Mem）：每条 uop 执行前发出（带 `pc/lane_mask/opcode_or_uop`）。
- `COMMIT`（category=Commit）：inst 级提交点发出。

计数器常见 key：
- `inst.fetch` / `inst.commit`
- `uop.exec.*` / `uop.mem.*` / `uop.ctrl.*`

### 2.4 写出格式：JSONL（每行一个事件）

序列化实现：`src/observability/observability.cpp` 的 `event_to_json_line()`。

当前 JSONL 每行对象字段（固定/可选）：
- 固定：`ts`, `cat`, `action`
- 可选：`kernel_id/cta_id/warp_id/thread_id/sm_id/stream_id/cmd_id/event_id/pc/lane_mask/opcode_or_uop/addr/size/extra`

写出位置：`src/apps/cli/main.cpp`
- `--trace <path>`：遍历 `rt.obs().trace_snapshot()`，对每个事件写一行 `event_to_json_line(e)`。

---

## 3) stats：counters JSON（当前形态与写出）

计数器采集：`ObsControl.counter()` → `Counters::inc()`。

序列化实现：`src/observability/observability.cpp` 的 `stats_to_json()`：

```json
{ "counters": { "key": 123, "another": 456 } }
```

写出位置：`src/apps/cli/main.cpp`
- `--stats <path>`：`stats_to_json(rt.obs().counters_snapshot())`。

---

## 4) 与 determinism 的关系（可复现边界）

实现位置：`src/simt/simt.cpp`

- `deterministic=true` 会强制禁用并行 worker（即便 `parallel=true`），因此 trace 事件不会跨 SM 线程交错。
- `deterministic=false` 且 `parallel=true` 且 `sm_count>1` 时启用并行，trace 的事件顺序允许交错差异。

补充：
- 并行模式下 `ts` 来自共享原子自增（仍是全局唯一递增），但“哪个事件先 emit”在多线程下不承诺稳定。

---

## 5) 与设计契约的差距（当前实现未冻结/未版本化的部分）

设计建议输出包含 `format_version/schema/profile/deterministic` 等顶层字段；当前实现：

- trace：是 JSONL 事件流，没有 header/顶层元数据对象。
  - 近似替代：`RUN_START.extra` 携带了配置摘要，但这不是版本化的 schema。
- stats：只有 `{counters:{...}}`，没有 `format_version/schema/profile`。

如果要按 design 收敛（推荐的最小改动路径）：
- stats：在 `stats_to_json()` 顶层补充 `format_version/schema/profile/deterministic`。
- trace：
  - 方案 A：在 JSONL 第一行写一个 header 对象（`action="TRACE_HEADER"` 或独立对象）。
  - 方案 B：改为输出一个 JSON 对象 `{meta, events:[...]}`（破坏性更大，需要 bump major）。

---

## 6) 验收入口（Tier‑0）

输出契约的“功能侧”验收仍以 Tier‑0 为主：

```bash
ctest --test-dir build -C Release -V -R "^gpu-sim-tiny-gpt2-mincov-tests$"
```
