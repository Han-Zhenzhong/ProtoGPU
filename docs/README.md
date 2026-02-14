# Docs

> Chinese version: [README.zh-CN.md](README.zh-CN.md)

This directory contains all documentation for `gpu-sim`.

## Quick links

- User docs (how to run / how to configure)
  - [doc_user/README.md](doc_user/README.md)
- Build docs (how to compile)
  - [doc_build/README.md](doc_build/README.md)
  - [doc_build/build.md](doc_build/build.md)

## Design and implementation

- Abstract design (module responsibilities, semantics, and flow contracts)
  - [doc_design/README.md](doc_design/README.md)
  - Overview (architecture design): [doc_design/arch_design.md](doc_design/arch_design.md)

- Implementation-level design (guidance for coding: types/interfaces/dirs/errors/config/trace)
  - [doc_dev/README.md](doc_dev/README.md)

## Specs and schemas

- Specs / diagrams (PUML, etc.)
  - [doc_spec/README.md](doc_spec/README.md)

- JSON schemas (inst_desc / ptx_isa / workload)
  - [../schemas/README.md](../schemas/README.md)

## Tests and plans

- Test docs
  - [doc_tests/README.md](doc_tests/README.md)

- Planning docs (roadmap / design plan / dev plan / test plan)
  - [doc_plan/overall_plan.md](doc_plan/overall_plan.md)

## Related directories

- scripts (build/unit/integration test scripts)
  - [../scripts/README.md](../scripts/README.md)

- tools (generators / trace viewer, etc.)
  - [../tools/README.md](../tools/README.md)

---

## Conventions

- Docs cross-link using relative paths, and links should work even when opened from the repo root.
- If text conflicts with PUML diagrams, diagrams win (see each doc_* README for details).
