## Plan: SIMT Predication & Minimal Control-Flow

把 [docs/doc_dev/ptx6.4_sm70/03_simt_predication_and_controlflow.md](../../../doc_dev/ptx6.4_sm70/03_simt_predication_and_controlflow.md) 的 “SIMT 编排职责、M3 predication guard、M4 uniform-only、next_pc 提交点、以及观测/诊断” 落成一份可执行 dev plan：明确每个 fail-fast 的归属层（SIMT vs units），并把 Tier‑0 gate（tiny GPT‑2 minimal coverage）作为默认验收入口。

### Steps 6
1. 冻结 SIMT 编排契约与数据结构：确认 `InstRecord.pred`/lane mask/`StepResult.next_pc`/`warp.active` 的语义与边界，落点在 [include/gpusim/contracts.h](../../../../include/gpusim/contracts.h) 与 [include/gpusim/simt.h](../../../../include/gpusim/simt.h)。
2. 固化 SIMT 主循环阶段与安全检查：以 [src/simt/simt.cpp](../../../../src/simt/simt.cpp) 的 `FETCH → lookup → expand → inject pred → dispatch → collect → COMMIT` 为准，计划中列出 `E_WARP_SIZE`、`E_PC_OOB`、partial warp `active` 初始化等检查点。
3. 计划化 M3 predication 注入：明确 predication **在 expand 之后**由 SIMT 统一注入（对每个 uop 做 `u.guard &= pred_mask`），并将 `pred_id` 越界 fail-fast 固化为 `simt/E_PRED_OOB`（实现点在 [src/simt/simt.cpp](../../../../src/simt/simt.cpp)）。
4. 计划化 ControlUnit 的最小控制流语义：在 [src/units/control_unit.cpp](../../../../src/units/control_unit.cpp) 明确 `BRA/RET` 的 uop 形态约束（arity + operand kind），`BRA` 只产生 `next_pc` 不直接写 PC，`RET` 设置 `warp.done=true`，并保留 predicated-off/no-op 语义。
5. 固化 M4 uniform-only（divergence 检测归属 SIMT）：在 [src/simt/simt.cpp](../../../../src/simt/simt.cpp) 把 divergence 规则写成可核对的检查（`m = active & guard`，若 `m` 非空且 `m != active` 则 `simt/E_DIVERGENCE_UNSUPPORTED`），并强调 ControlUnit 不负责 divergence 判定。
6. 固化 next_pc 聚合与 inst 级提交点 + 观测：在 [src/simt/simt.cpp](../../../../src/simt/simt.cpp) 明确 `E_NEXT_PC_CONFLICT`（同一 inst 多条 uop 设置 next_pc 冲突）、commit 时 `pc = next_pc or pc+1`、以及 `FETCH/UOP/COMMIT` 事件的期望字段与来源（事件定义见 [include/gpusim/observability.h](../../../../include/gpusim/observability.h)、实现见 [src/observability/observability.cpp](../../../../src/observability/observability.cpp)）。

### Further Considerations 3
1. 诊断覆盖缺口：当前测试未明显覆盖 `E_PRED_OOB`/`E_DIVERGENCE_UNSUPPORTED`/`E_NEXT_PC_CONFLICT`/`E_PC_OOB`；是否补少量 targeted tests（不替代 Tier‑0）？
2. Trace lane_mask 语义：当前 `UOP` 事件里记录的是 `warp.active` 还是 `exec_mask`（`active & guard`）？计划里需统一口径避免误读。
3. 诊断分层一致性：`pred` 越界在 SIMT 注入处有 `simt/E_PRED_OOB`，而写 predicate（如 SETP）可能在 exec unit 也会报 `units.exec/E_PRED_OOB`；要不要在计划里明确“谁负责报什么”。
