# Dev: PTX 6.4 + sm70（冻结基线 / Tier-0）

本目录是 [docs/doc_design/ptx6.4_sm70/00_scope_and_quality_gate.md](../../doc_design/ptx6.4_sm70/00_scope_and_quality_gate.md) 的实现级落地手册：告诉你改哪里、怎么验收、怎么避免把“冻结口径”做漂。

## 关联（单一真源）

- 规格：
  - [docs/doc_spec/ptx64_baseline.md](../../doc_spec/ptx64_baseline.md)
  - [docs/doc_spec/sm70_profile.md](../../doc_spec/sm70_profile.md)

- 冻结设计（对外承诺）：
  - [docs/doc_design/ptx6.4_sm70/README.md](../../doc_design/ptx6.4_sm70/README.md)
  - [docs/doc_design/ptx6.4_sm70/00_scope_and_quality_gate.md](../../doc_design/ptx6.4_sm70/00_scope_and_quality_gate.md)

- 已有实现级模块文档（更细的代码落点）：
  - [docs/doc_dev/README.md](../README.md)
  - `docs/doc_dev/modules/*`

## 本目录文档

- 00_tier0_gate_workflow.md：Tier‑0 闸门（M1–M4）如何在资产/代码/回归里闭环
- 00.01_fail_fast_memory_oob.md：未分配/越界 `ld/st.global` 的 fail-fast 口径与实现落点
- 01_ptx64_frontend_and_mapping.md：PTX tokenization 子集、PTX→IR 映射匹配键、operand 解析与 label→pc 改写落点
- 02_instruction_semantics_and_uops.md：inst_desc（IR→uops）契约：descriptor lookup/expand、predication guard、fail-fast 落点
- 03_simt_predication_and_controlflow.md：SIMT 编排层：predication→guard、uniform-only 分支、next_pc 提交与诊断分流
- 04_memory_no_cache_addrspace.md：no_cache_addrspace：selector 校验、param/global 访问路径、对齐/越界/同址冲突与确定性
- 05_observability_and_output_contract.md：Diagnostic/trace/stats 的对外输出：现状字段、写出位置、版本化差距与扩展入口
- 06_public_api_and_assets_packaging.md：对外 API（Runtime/CLI/workload）与资产加载（file vs in-memory）的现状与扩展点
