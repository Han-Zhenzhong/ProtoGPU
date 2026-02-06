## Plan: 06.01 3D Launch + Builtins 实现计划

基于 06 SIMT Core 的抽象语义（`exec_mask = active_mask & uop.guard`、step 可阻塞可重试）与 06.01 的 3D 枚举/内建映射规则，分阶段把“launch 维度建模 + builtin 读取 + lane-wise 执行”落到现有代码结构中。优先控制改动爆炸半径：先补齐 contracts 与 mapper 能表达 builtin，再把 Units/SIMT 的执行从标量推进到 lane-wise，最后接入 3D grid/block 的确定性枚举与 trace 观测字段。

### Steps
1. 定义 launch contracts 与诊断码，补齐基础类型与校验入口：[include/gpusim/contracts.h](include/gpusim/contracts.h) 的 `Dim3`/`LaunchConfig`/`Builtins`，以及 Runtime 侧的维度校验（落点参考 [docs/doc_dev/modules/06.01_launch_grid_block_3d.md](docs/doc_dev/modules/06.01_launch_grid_block_3d.md) 的 `runtime:E_LAUNCH_DIM` / `runtime:E_LAUNCH_OVERFLOW`）。
2. 打通“builtin 作为 operand kind”的表达能力：扩展 `OperandKind::Special` 与 `Operand.special`（contracts + JSON/schema + 打印/诊断），并让 PTX ISA mapper 能匹配 `(reg, special)` 这类签名：[src/instruction/ptx_isa.cpp](src/instruction/ptx_isa.cpp) 与相关 schema/registry（参考 [docs/doc_design/modules/06.01_launch_grid_block_3d.md](docs/doc_design/modules/06.01_launch_grid_block_3d.md) 选项 A）。
3. 先落地 lane-wise 执行“最小闭环”：扩展 [include/gpusim/units.h](include/gpusim/units.h) 的 warp 状态为 lane-wise regs/preds + `active_mask`，并在 [src/units/exec_core.cpp](src/units/exec_core.cpp) 引入 `read_operand_lane()`（Imm/Reg/Special），确保按 [docs/doc_design/modules/06_simt_core.md](docs/doc_design/modules/06_simt_core.md) 的 `exec_mask` 逐 lane 执行与写回。
4. 在 SIMT executor 中引入最小 builtin 元数据载体并计算 `%tid/%ctaid/...`：在 [src/simt/simt.cpp](src/simt/simt.cpp) 给每个 warp 填充 `LaunchConfig + ctaid + warpid + lane_base_thread_linear`，实现 `tid` 的 3D 反解与 partial warp `active_mask` 关闭规则（严格按 [docs/doc_design/modules/06.01_launch_grid_block_3d.md](docs/doc_design/modules/06.01_launch_grid_block_3d.md) 的 `thread_linear`/`laneid/warpid` 公式）。
5. 接通 3D grid/block 的确定性枚举与可观测：在 [src/simt/simt.cpp](src/simt/simt.cpp) 按 `ctaid.z→y→x` 的顺序枚举 CTA、CTA 内按 `warpid` 递增枚举 warp，并在 observability/trace 中至少补齐 `cta_id/warp_id` 字段（事件边界遵循 [docs/doc_design/modules/06_simt_core.md](docs/doc_design/modules/06_simt_core.md) 的 FETCH/EXEC/COMMIT），新增/更新 golden 用例覆盖满 warp、部分 warp、3D 反解与 builtin 写回验证。

### Further Considerations
1. Special 的解析落点：按当前流水线更推荐在 mapper 侧识别/携带 `special`，而非 frontend tokenization（避免把 operand kind 绑定到 parser 阶段）；若仍保留 frontend 识别，需明确与 mapper 的一致性与优先级。
2. trace mask 语义：`lane_mask` 建议保持 `active_mask`，把 `exec_mask` 单独记录（否则会改变既有观测解释）；同时明确 `warp_id` 的稳定组合策略。
3. 维度/溢出校验位置：建议统一在 Runtime/CLI 入口完成并携带 launch 位置信息，避免 SIMT/Units 内部重复分支与不一致错误码。
