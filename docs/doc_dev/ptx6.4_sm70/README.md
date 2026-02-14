# Dev: PTX 6.4 + sm70 (frozen baseline / Tier-0)

> Chinese version: [README.zh-CN.md](README.zh-CN.md)

This directory is the implementation-level playbook for [docs/doc_design/ptx6.4_sm70/00_scope_and_quality_gate.md](../../doc_design/ptx6.4_sm70/00_scope_and_quality_gate.md): it tells you what to change, how to validate, and how to prevent the “frozen contract” from drifting.

## Related (single source of truth)

- Specs:
  - [docs/doc_spec/ptx64_baseline.md](../../doc_spec/ptx64_baseline.md)
  - [docs/doc_spec/sm70_profile.md](../../doc_spec/sm70_profile.md)

- Frozen design (external contract):
  - [docs/doc_design/ptx6.4_sm70/README.md](../../doc_design/ptx6.4_sm70/README.md)
  - [docs/doc_design/ptx6.4_sm70/00_scope_and_quality_gate.md](../../doc_design/ptx6.4_sm70/00_scope_and_quality_gate.md)

- Existing implementation-level module docs (more detailed code landing points):
  - [docs/doc_dev/README.md](../README.md)
  - `docs/doc_dev/modules/*`

## Documents in this directory

- 00_tier0_gate_workflow.md: how Tier‑0 gate (M1–M4) closes the loop across assets/code/regression
- 00.01_fail_fast_memory_oob.md: fail-fast policy and implementation landing points for unallocated / out-of-bounds `ld/st.global`
- 01_ptx64_frontend_and_mapping.md: PTX tokenization subset, PTX→IR mapping match keys, operand parsing, label→pc rewrite landing points
- 02_instruction_semantics_and_uops.md: inst_desc (IR→uops) contract: descriptor lookup/expand, predication guards, fail-fast landing points
- 03_simt_predication_and_controlflow.md: SIMT orchestration: predication→guards, uniform-only branches, next_pc commit and diagnostic routing
- 04.01_simt_divergence_and_reconvergence.md: SIMT divergence/reconvergence (M5): reconvergence precompute via CFG+ipdom, SIMT stack, lane-wise ret, and test suggestions
- 04_memory_no_cache_addrspace.md: no_cache_addrspace: selector validation, param/global access paths, alignment/OOB/same-address conflicts and determinism
- 05_observability_and_output_contract.md: external output for Diagnostic/trace/stats: current fields, write-out locations, versioning gaps, extension points
- 06_public_api_and_assets_packaging.md: external API (Runtime/CLI/workload) and asset loading (file vs in-memory): current state and extension points
