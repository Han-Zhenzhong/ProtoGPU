# 06 对外 API 与资产打包（PTX6.4 + sm70 冻结口径）

本设计对应 plan 的 Step 5：收敛成可被外部工程稳定依赖的 API，并明确资产加载方式。

相关文档与入口
- 规划：../../doc_plan/plan_design/plan-realPTXSetAndGPUHWSim.prompt.md
- 头文件入口：../../../include/gpusim/runtime.h（workload 能力通过 `Runtime::run_workload(path)` 暴露；repo 当前不存在 `include/gpusim/workload.h`）
- ABI/Kernel IO：../modules/09_kernel_io_and_abi.md

## 1. 对外 API 目标

冻结目标
- 外部调用方可以：
  - 提供 PTX 文本（或预解析 module）
  - 提供 `ptx_isa` 与 `inst_desc`（文件路径或 in-memory blob）
  - 配置 profile（sm70 基线 + 可选并行/确定性开关）
  - 执行并得到：completed/diag + 可选 trace/stats

## 2. 资产加载：file 与 in-memory 两种模式

冻结要求
- 不能强制外部工程依赖 repo 内部路径结构；必须支持 "in-memory assets"：
  - 调用方把 JSON 字符串（或 bytes）传入 registry loader。

建议的接口形态（概念）
- `Runtime::load_assets(PtxIsaSource, InstDescSource)`
  - Source 可以是 `FilePath` 或 `InMemoryJson`。

## 3. profile 与配置输入

冻结字段（sm70 baseline）
- warp_size（默认 32，允许 1..32）
- memory.model（仅 `no_cache_addrspace`）
- parallel/deterministic（见 ../../doc_spec/sm70_profile.md 附录 C）

## 4. 错误处理口径

冻结原则
- 任何“非目标能力”被触发（divergence/未知 form/越界访问等）必须返回诊断，而不是返回部分结果。

对外返回建议（概念）
- `SimOutput { completed: bool, diag?: Diagnostic, trace?: ..., stats?: ... }`

## 5. 与 Tier-0 的关系

- 对外 API 的最小可用验收 = Tier-0（tiny GPT-2 M1–M4）可以通过该 API 端到端运行。
- API 的任何变更必须保持 Tier-0 回归通过（或以 major 版本变更并同步升级文档）。
