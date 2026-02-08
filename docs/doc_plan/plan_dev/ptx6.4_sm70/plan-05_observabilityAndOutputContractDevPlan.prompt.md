## Plan: Observability & Output Contract Dev Plan

把 [docs/doc_dev/ptx6.4_sm70/05_observability_and_output_contract.md](../../../doc_dev/ptx6.4_sm70/05_observability_and_output_contract.md) 的 Diagnostic/trace/stats 真实形态与“契约化/版本化差距”落成一份可执行 dev plan：在不破坏 Tier‑0 与现有脚本的前提下，冻结最小对外交付字段，并用最小破坏策略补齐 `format_version/schema/profile/deterministic` 等元信息。

### Steps 6
1. 冻结对外 API 面：以 [include/gpusim/runtime.h](../../../../include/gpusim/runtime.h) 的 `RunOutputs` 为契约，明确 `outputs.sim.completed` 与 `outputs.sim.diag` 的语义与稳定性；Diagnostic 结构以 [include/gpusim/contracts.h](../../../../include/gpusim/contracts.h) 的 `Diagnostic{module,code,message,location?,function?,inst_index?}` 为准。
2. 固化 Diagnostic 的“可定位性”规则：列出各层常见 `module/code` 的归属（SIMT vs units vs runtime），并规定“SIMT 已知 PC 时必须填 `inst_index`/`function`”；对 unit 返回的诊断，计划里明确是否允许 SIMT 侧补齐上下文（不改 unit API 也能提升定位能力）。
3. 锁定 trace 事件模型与最小事件集：以 [include/gpusim/contracts.h](../../../../include/gpusim/contracts.h) 的 `Event` 字段为基线，冻结 Tier‑0 最小事件集（`RUN_START/FETCH/UOP/COMMIT`）及关键字段（`ts/cat/action/pc/lane_mask/opcode_or_uop/extra`），并把 emit 落点写清在 [src/simt/simt.cpp](../../../../src/simt/simt.cpp) 与序列化在 [src/observability/observability.cpp](../../../../src/observability/observability.cpp)。
4. 引入版本化元信息（最小破坏路径）：按文档推荐优先走“trace JSONL 第一行 header”方案（保持 JSONL 不变），并为 stats 在 [src/observability/observability.cpp](../../../../src/observability/observability.cpp) 的 `stats_to_json()` 顶层补齐 `format_version/schema/profile/deterministic`（加字段不删字段，保证兼容）。
5. 明确 determinism 与并行下的输出承诺：在计划中冻结“`deterministic=true` 禁并行 worker、trace 不交错”的语义（落点 [src/simt/simt.cpp](../../../../src/simt/simt.cpp)），并对 `parallel=true` 的非稳定事件顺序给出明确免责声明（`ts` 单调不等于事件顺序稳定）。
6. CLI 输出路径与脚本兼容性清单：以 [src/apps/cli/main.cpp](../../../../src/apps/cli/main.cpp) 的 `--trace/--stats` 写出路径为准，计划中列出“哪些字段/动作名是脚本依赖的（如 RUN_START）不可改”，并明确新增字段的迁移策略（保留旧字段、只做 additive）。

### Further Considerations 3
1. JSON 数值精度风险：当前 `u64` 可能会被写成 `double`（尤其 addr/ids/counters）；要不要在契约里规定“大整数用字符串字段”或新增 `*_str` 并逐步迁移？
2. `RUN_START.extra` 是 JSON 字符串（双层 JSON）；是否要在 v1 明确这是“stringified JSON”，避免下游误解？
3. 缺少输出格式单测：现有测试基本不校验 trace/stats JSON 结构；是否补一个轻量 contract test（只断言 header/关键字段存在，不做全量 golden）以防回归？
