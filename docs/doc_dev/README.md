# Dev Docs (implementation-level design)

> Chinese version: [README.zh-CN.md](README.zh-CN.md)

## Notes

- This directory contains implementation-level design that *guides code development*: directory/module boundaries, API landing points, data structure landing points, config/observability landing points, error handling and diagnostics conventions.
- Design baseline: module boundaries and dependency directions follow the PUML diagrams (especially `doc_design/arch_modules_block.diagram.puml` and `doc_design/sequence.diagram.puml`). If text conflicts with diagrams, diagrams win.
- Instruction descriptor file format is fixed to JSON.

## Relationship to abstract design

- Abstract logic design lives in `doc_design/`: module responsibilities, semantics, and flow contracts.
- Implementation-level design lives in `doc_dev/`: where types live, API shape, directory/file organization, external error/diagnostic shape, config and trace output formats.

## Code directory alignment

- `src/common/`: Contracts (core types, diagnostics, events, configs, serialization)
- `src/observability/`: ObsControl, TraceBuffer, Counters
- `src/instruction/`: DescriptorRegistry (JSON + schema validation) and MicroOpExpander
- `src/frontend/`: Parser / Binder / ModuleBuilder
- `src/memory/`: AddrSpaceManager and address space implementations
- `src/units/`: ExecCore / ControlUnit / MemUnit
- `src/simt/`: Contexts / Schedulers / Executor
- `src/runtime/`: Runtime / Streams / Queues / DependencyTracker
- `src/apps/cli/`: end-to-end entry (input PTX/descriptor/config, output trace/stats)

## Implementation order (dependency order)

1) Contracts
2) Observability
3) Instruction System
4) Frontend
5) Memory
6) Units
7) SIMT Core
8) Runtime + Streaming
9) Engines
10) Kernel I/O + ABI (spans Runtime/Engines/Memory)

## Config and I/O conventions

- Runtime config: JSON file (path provided via CLI)
- Instruction descriptors: JSON file (path provided via CLI)
- Instruction descriptor schema: under `schemas/` (see Instruction System dev doc)
- Trace: JSON Lines (one Event JSON object per line)
- Stats: JSON (Counters snapshot)

## Error and diagnostics conventions

- External errors are unified as `Diagnostic` (module/code/message/location/inst_index)
- Any error must be locatable to: source location (file/line/column) or `inst_index` (at least one)

## Index (by module)

- `doc_dev/modules/00_contracts.md`
- `doc_dev/modules/01_observability.md`
- `doc_dev/modules/02_frontend.md`
- `doc_dev/modules/02.01_frontend_desc_driven_decode.md`
- `doc_dev/modules/03_instruction_system.md`
- `doc_dev/modules/04_memory.md`
- `doc_dev/modules/05_units.md`
- `doc_dev/modules/06_simt_core.md`
- `doc_dev/modules/06.01_launch_grid_block_3d.md`
- `doc_dev/modules/06.02_sm_parallel_execution.md`
- `doc_dev/modules/07_runtime_streaming.md`
- `doc_dev/modules/07.01_stream_input_workload_spec.md`
- `doc_dev/modules/08_engines.md`
- `doc_dev/modules/09_kernel_io_and_abi.md`
- `doc_dev/modules/10_modular_hw_sw_mapping.md`
