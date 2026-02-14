# PTX 6.4 + sm70（功能级）冻结设计（Tier-0: tiny GPT-2 M1–M4）

本目录给出“可对外交付的冻结基线”在 **设计层** 的落地：
- PTX 输入口径：PTX 6.4 冻结子集（以 opcode/type_mod/operand_kinds 为匹配键）
- 硬件口径：sm70 profile（功能级；不追求 cycle-accurate）
- 质量闸门：tiny GPT-2 bring-up M1–M4 作为 Tier-0（必须持续回归通过）

它不是对 repo 全部模块设计文档的替代，而是把“对外承诺”收敛成一套可验证、可扩展且不漂移的冻结接口与边界。

## 入口与依赖

- 规划来源：
  - 完整仿真与对外交付路线：../../doc_plan/plan_design/plan-realPTXSetAndGPUHWSim.prompt.md
- 规格基线（必须一致）：
  - PTX 6.4 baseline：../../doc_spec/ptx64_baseline.md
  - sm70 profile：../../doc_spec/sm70_profile.md

- 模块化设计文档（本目录在这些文档之上“收敛冻结口径”）：
  - Frontend：../modules/02_frontend.md
  - Instruction System：../modules/03_instruction_system.md
  - SIMT Core：../modules/06_simt_core.md
  - Units：../modules/05_units.md
  - Memory：../modules/04_memory.md
  - Observability：../modules/01_observability.md
  - Contracts：../modules/00_contracts.md

## 本目录文档

- 00_scope_and_quality_gate.md：范围、Tier-0 质量闸门、验收命令
- 01_ptx64_frontend_and_mapping.md：PTX6.4 tokenization 子集、映射键冻结、label→pc、%f 与 0f 立即数
- 02_instruction_semantics_and_uops.md：IR op 约定、inst_desc/expander 契约、fail-fast
- 03_simt_predication_and_controlflow.md：predication guard、uniform-only 分支、next_pc 提交
- 04_memory_no_cache_addrspace.md：no_cache_addrspace 可观察边界与错误策略
- 05_observability_and_output_contract.md：diag/trace/stats 的格式契约与版本化建议
- 06_public_api_and_assets_packaging.md：对外 API 形态与“in-memory assets”加载策略

## 设计原则（冻结）

- 匹配键冻结：`ptx_opcode + type_mod + operand_kinds`；bring-up 阶段 `space/flags` 不参与匹配。
- fail-fast：unknown form / unknown uop / divergence / 越界访存等必须返回可定位 Diagnostic，禁止 silent fallback。
- 最小可回归：每新增一个 PTX form，必须同时补齐：
  1) `assets/ptx_isa/*.json` entry
  2) `assets/inst_desc/*.json` 语义
  3) fixture PTX
  4) CTest（或纳入现有 Tier-0）
