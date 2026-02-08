# 05 Observability & Output Contract Dev Plan

目标：冻结 Diagnostic/trace/stats 的最小对外交付契约，并在不破坏 Tier‑0 与现有脚本的前提下补齐版本化元信息（`format_version/schema/profile/deterministic`），让下游工具可稳定消费。

单一真源
- 实现现状与差距：[docs/doc_dev/ptx6.4_sm70/05_observability_and_output_contract.md](../../../doc_dev/ptx6.4_sm70/05_observability_and_output_contract.md)

---

## 1) Diagnostic（对外 API 的一部分）

- 返回面：`include/gpusim/runtime.h` 的 `RunOutputs.sim.diag`
- 结构：`include/gpusim/contracts.h` 的 `Diagnostic`

冻结要求
- `module`/`code` 必须稳定；message 可扩展但不可破坏解析
- SIMT 已知 PC 时应填 `inst_index`
- CLI 输出格式保持可读稳定（stderr 打印）

---

## 2) trace（JSONL）

- 采集：`include/gpusim/observability.h` / `src/observability/observability.cpp`
- emit：主要在 `src/simt/simt.cpp`（RUN_START/FETCH/UOP/COMMIT）
- 写出：`src/apps/cli/main.cpp` `--trace <path>`

冻结最小事件集
- `RUN_START`（包含配置摘要）
- `FETCH` / `UOP` / `COMMIT`

版本化策略（推荐最小破坏）
- 保持 JSONL 不变，新增第一行 header 对象（不删除 RUN_START）

---

## 3) stats（counters JSON）

- 写出：`src/apps/cli/main.cpp` `--stats <path>`
- 序列化：`src/observability/observability.cpp` `stats_to_json()`

版本化策略（additive）
- 在顶层补齐 `format_version/schema/profile/deterministic`（保留 `counters` 不变）

---

## 4) determinism 与并行输出承诺

- `deterministic=true`：禁并行 worker，trace 不跨线程交错
- `parallel=true` 且非 deterministic：事件顺序不承诺稳定（即便 ts 单调）

落点
- `src/simt/simt.cpp`

---

## 5) 验收

- 默认 merge gate：`gpu-sim-tiny-gpt2-mincov-tests`
- 建议新增轻量 contract test：只断言 header/关键字段存在（避免 full golden）
