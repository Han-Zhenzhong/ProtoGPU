## Plan: Public API & Assets Packaging Dev Plan

把 [docs/doc_dev/ptx6.4_sm70/06_public_api_and_assets_packaging.md](../../../doc_dev/ptx6.4_sm70/06_public_api_and_assets_packaging.md) 的“对外稳定 API 面 + 资产加载方式 + in-memory 缺口”落成可执行 dev plan：保持现有 file-path API 不破坏，同时把底层已存在的 `load_json_text()` / PTX text tokenization 收敛成 Runtime 的新 overload，并用 Tier‑0 + 小型 in-memory 用例锁回归。

### Steps 6
1. 起草计划文档：新增 [docs/doc_plan/plan_dev/ptx6.4_sm70/06_public_api_and_assets_packaging_dev_plan.md](06_public_api_and_assets_packaging_dev_plan.md)，并在开头冻结 public headers 与稳定入口：以 [include/gpusim/runtime.h](../../../../include/gpusim/runtime.h) 的 `Runtime`/`RunOutputs` 为主入口，明确 [include/gpusim/contracts.h](../../../../include/gpusim/contracts.h) 与 [include/gpusim/observability.h](../../../../include/gpusim/observability.h) 是对外交付契约的一部分（Diagnostic/Event/trace/stats 输出格式）。
2. 盘点并锁定当前“最稳定路径”：记录 Runtime 现有 file-path 模式（PTX path + assets path 明确传入）的行为边界，强调不做 repo 内 assets 自动发现（CLI 默认值不算 public contract），实现落点在 [src/runtime/runtime.cpp](../../../../src/runtime/runtime.cpp)。
3. 抽取 Runtime 内部 helper（不改行为）：在 [src/runtime/runtime.cpp](../../../../src/runtime/runtime.cpp) 抽出一个内部 helper，输入 `KernelTokens + PtxIsaRegistry + DescriptorRegistry (+ 可选 args/param blob)`，复用现有 `PtxIsaMapper::map_kernel()`、`DescriptorRegistry::lookup()`、`SimtExecutor::run()`，并让现有 `run_ptx_kernel*_launch` 系列统一走该 helper（目标是“先重构、后加新 API”）。
4. 增加 in-memory Runtime overload（增量不破坏）：基于底层已具备的
   - PTX text： [include/gpusim/frontend.h](../../../../include/gpusim/frontend.h) `parse_ptx_text_tokens()`
   - JSON text loaders： [include/gpusim/ptx_isa.h](../../../../include/gpusim/ptx_isa.h) `PtxIsaRegistry::load_json_text()` 与 [include/gpusim/instruction_desc.h](../../../../include/gpusim/instruction_desc.h) `DescriptorRegistry::load_json_text()`
   设计并新增 `Runtime::run_ptx_kernel_text_launch(...)`（或 `PtxIsaSource/InstDescSource` 变体类型），确保 ownership 随 call 生命周期走，不引入隐式全局缓存。
5. 收敛 workload 的对外说明与扩展点：明确 repo 内不存在 `include/gpusim/workload.h`，workload 入口是 [include/gpusim/runtime.h](../../../../include/gpusim/runtime.h) `Runtime::run_workload()` + 实现在 [src/runtime/workload.cpp](../../../../src/runtime/workload.cpp)；把“是否提供 `run_workload_text(json_text)` overload”列为下一步可选增强（与 in-memory 方向一致）。
6. 把回归闸门写进计划：保持现有 Tier‑0（`gpu-sim-tiny-gpt2-mincov-tests`）不变；新增一个最小 in-memory 用例（最小 PTX + 最小 JSON assets text）作为“新 overload 不被未来弄坏”的护栏，并明确它不应依赖 repo 路径结构。

### Further Considerations 3
1. API 形态选择：直接新增 `run_ptx_kernel_text_launch`（简单）vs 引入 `Source` 变体类型（扩展性强）；二选一保持一致。
2. 资产缓存策略：bring-up 阶段建议不做隐式缓存，避免“资产漂移”；若要性能优化，建议让 caller 显式持有 registry。
3. 兼容性风险点：`*_with_args_*` 的 param layout/blob 初始化必须保持一致，避免 helper 重构后出现“无 args 调用复用旧 param”的隐性状态问题。
