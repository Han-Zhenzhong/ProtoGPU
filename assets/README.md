# assets/

> Chinese version: [README.zh-CN.md](README.zh-CN.md)

Built-in inputs and data assets for running and testing.

- `configs/`: runtime configuration JSON (single-kernel / parallel / modular selectors, etc.)
- `inst_desc/`: instruction descriptor JSON (IR semantics library: `ir_op → uops`)
- `ptx/`: example PTX programs
- `ptx_isa/`: PTX ISA mapping JSON (PTX instruction form → `ir_op` + `operand_kinds`)
- `workloads/`: WorkloadSpec (streams/commands) example JSON (for `gpu-sim-cli --workload`)

---

## configs/ (runtime configuration)

Common examples:

- `configs/demo_config.json`
  - Default demo configuration (serial).

- `configs/demo_parallel_config.json`
  - Multi-SM parallel execution example (one host thread per SM).
  - Typical pairing: use `--grid 4,1,1` to create multiple CTAs to observe parallelism.

- `configs/demo_modular_selectors.json`
  - Modular HW/SW mapping example (profile/components + selectors).
  - Selects: `cta_scheduler=sm_round_robin`, `warp_scheduler=round_robin_interleave_step`, `memory_model=no_cache_addrspace`.

Related docs:

- User docs: [docs/doc_user/modular_hw_sw_mapping.md](../docs/doc_user/modular_hw_sw_mapping.md)
- Build and run: [docs/doc_build/build.md](../docs/doc_build/build.md)

---

## inst_desc/, ptx_isa/, ptx/

These three kinds of files are typically used together:

- `ptx/*.ptx`: input programs (kernels)
- `ptx_isa/*.json`: maps PTX instruction forms to internal IR semantic entries
- `inst_desc/*.json`: rules for expanding internal IR semantic entries into micro-ops

Default demo combination:

- `ptx/demo_kernel.ptx`
- `ptx_isa/demo_ptx64.json`
- `inst_desc/demo_desc.json`

---

## workloads/ (WorkloadSpec)

WorkloadSpec describes replayable streams/commands inputs, used with `gpu-sim-cli --workload <file>`.

Related docs:

- [docs/doc_user/cli.md](../docs/doc_user/cli.md)
