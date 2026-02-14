# PTX 6.4 + sm70 (functional) frozen design (Tier-0: tiny GPT-2 M1–M4)

> Chinese version: [README.zh-CN.md](README.zh-CN.md)

This directory defines the **design-level** landing of a “deliverable frozen baseline”:

- PTX input contract: a frozen subset of PTX 6.4 (matching key: `opcode/type_mod/operand_kinds`)
- Hardware contract: an sm70 profile (functional level; not cycle-accurate)
- Quality gate: tiny GPT-2 bring-up M1–M4 as Tier‑0 (must continuously pass regression)

It is not a replacement for the repo-wide module design docs; it *narrows the external contract* into a frozen set of verifiable, extensible, non-drifting interfaces and boundaries.

## Entry points and dependencies

- Planning source:
  - Full simulation and delivery roadmap: ../../doc_plan/plan_design/plan-realPTXSetAndGPUHWSim.prompt.md

- Spec baselines (must stay consistent):
  - PTX 6.4 baseline: ../../doc_spec/ptx64_baseline.md
  - sm70 profile: ../../doc_spec/sm70_profile.md

- Modular design docs (this directory “converges the frozen contract” on top of these):
  - Frontend: ../modules/02_frontend.md
  - Instruction System: ../modules/03_instruction_system.md
  - SIMT Core: ../modules/06_simt_core.md
  - Units: ../modules/05_units.md
  - Memory: ../modules/04_memory.md
  - Observability: ../modules/01_observability.md
  - Contracts: ../modules/00_contracts.md

## Documents in this directory

- 00_scope_and_quality_gate.md: scope, Tier‑0 quality gate, validation commands
- 01_ptx64_frontend_and_mapping.md: PTX 6.4 tokenization subset, frozen mapping keys, label→pc, `%f` and `0f` immediates
- 02_instruction_semantics_and_uops.md: IR op conventions, inst_desc/expander contract, fail-fast
- 03_simt_predication_and_controlflow.md: predication guards, uniform-only branches, next_pc commit
- 04_memory_no_cache_addrspace.md: no_cache_addrspace observable boundaries and error policy
- 05_observability_and_output_contract.md: diag/trace/stats format contract and versioning suggestions
- 06_public_api_and_assets_packaging.md: external API shape and “in-memory assets” loading strategy

## Design principles (frozen)

- Frozen match key: `ptx_opcode + type_mod + operand_kinds`; during bring-up, `space/flags` do not participate in matching.
- Fail-fast: unknown form / unknown µop / divergence / out-of-bounds memory access must return a locatable Diagnostic; silent fallback is forbidden.
- Minimum regressive coverage: each new PTX form must include:
  1) an `assets/ptx_isa/*.json` entry
  2) semantics in `assets/inst_desc/*.json`
  3) a fixture PTX
  4) a CTest (or inclusion into existing Tier‑0)
