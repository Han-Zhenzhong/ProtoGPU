# 05 Observability：诊断/trace/stats 输出契约（面向对外交付）

本设计聚焦 plan 中的 Step 4：输出格式契约化与版本化，并与 sm70 profile 的 determinism 边界对齐。

相关模块设计：../modules/01_observability.md、../modules/00_contracts.md

## 1. 诊断（Diagnostic）是对外 API 的一部分

冻结要求
- 所有 fail-fast 必须返回可定位 Diagnostic：
  - `module`：frontend/mapper/instruction/simt/units/memory/runtime/observability
  - `code`：稳定的错误码（例如 `E_DIVERGENCE_UNSUPPORTED`, `E_MEMORY_MODEL`）
  - `message`：人类可读
  - `location`：尽量包含 file/line/column
  - `inst_index/pc`：若可用必须包含

建议
- 对外 API 返回结构应同时包含：`completed` 与 `diag`（可选），避免把错误仅埋在 stderr。

## 2. trace/stats 的版本字段（建议冻结）

对外交付时，应把输出格式做契约化：

- trace：
  - 顶层字段：`format_version`, `schema`, `profile`（例如 "ptx6.4+sm70"）, `deterministic`，以及事件数组。
  - 事件字段最小集：`ts(optional)`, `category`, `action`, `pc(optional)`, `lane_mask(optional)`, `uop(optional)`。

- stats：
  - 顶层字段：`format_version`, `schema`, `profile`, `counters{...}`。

版本化原则
- 只增不改：新增字段不破坏旧解析器；破坏性变更必须 bump major。

## 3. 与 determinism 的一致性

- deterministic=true：建议 trace 的事件顺序尽量稳定（至少同一输入不应随机波动）。
- deterministic=false：允许跨 SM 的事件交错变化；但应确保事件本身携带足够上下文（sm_id/cta_id/warp_id）。

## 4. golden 测试与观测输出

- 对可稳定复现的输出（deterministic=true）：建议引入 golden trace/stats（与 tests/golden 约定对齐）。
- Tier-0（tiny GPT-2 M1–M4）：优先保证 functional 结果与 diag 口径稳定；trace/stats 作为可选增强。
