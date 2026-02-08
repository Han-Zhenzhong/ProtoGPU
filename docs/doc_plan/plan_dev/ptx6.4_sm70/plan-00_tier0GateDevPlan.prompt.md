## Plan: Tier‑0 Gate Dev Plan（PTX6.4 + sm70）

把 [docs/doc_dev/ptx6.4_sm70/00_tier0_gate_workflow.md](../../../doc_dev/ptx6.4_sm70/00_tier0_gate_workflow.md) 的“闸门规则 + fail-fast 清单 + 改动点地图”，落成一份可执行的开发计划文档，放到 [docs/doc_plan/plan_dev/ptx6.4_sm70/](./) 下，并与 [docs/doc_spec/ptx64_baseline.md](../../../doc_spec/ptx64_baseline.md) 的 M1–M4 冻结接口保持单一真源。目标是：每次新增/修改一个可执行 PTX form，都能形成“四件套”闭环并保持 Tier‑0 回归稳定通过。

### Steps 5
1. 选择计划落点与命名：新建 [docs/doc_plan/plan_dev/ptx6.4_sm70/00_tier0_gate_dev_plan.md](00_tier0_gate_dev_plan.md)，标题对齐 “00” Tier‑0 工作流编号。
2. 写清 “单一真源 + 冻结接口”：在文档开头链接 [docs/doc_spec/ptx64_baseline.md](../../../doc_spec/ptx64_baseline.md) 的 M1–M4 forms 与约束，并声明 matcher 冻结为 `ptx_opcode + type_mod + operand_kinds`。
3. 固化交付定义：把“四件套”写成强制门槛（`assets/ptx_isa`、`assets/inst_desc`、`tests/fixtures/ptx`、Tier‑0 回归），并给出“不能并入 Tier‑0 时的例外说明要求”。
4. 固化 fail-fast gate：逐条搬运 workflow 的检查清单到计划里，并标注常见落点（mapper/descriptor/exec/mem/control/simt）用于 code review 与回溯定位。
5. 固化回归入口：在计划中把 `gpu-sim-tiny-gpt2-mincov-tests` 写成唯一 merge gate，并附上 `ctest --test-dir build -C Release -V -R "^gpu-sim-tiny-gpt2-mincov-tests$"` 命令与“失败时按改动点地图回溯”的规则。

### Further Considerations 2
1. 文件命名偏好：按 “00_” 编号对齐（推荐）还是直接用 `dev_plan.md`（更通用）？
2. 例外策略：新增 form 若暂时不进 Tier‑0，是否要求同时新增独立 CTest + 文档解释原因（建议要求）。
