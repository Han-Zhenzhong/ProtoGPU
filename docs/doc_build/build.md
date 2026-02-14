# How to build (CMake Build Guide)

> Chinese version: [build.zh-CN.md](build.zh-CN.md)

This repository uses CMake (see the repo-root `CMakeLists.txt`). The main build artifacts are:

- `gpusim_core`: core library (static/shared depends on generator/platform)
- `gpu-sim-cli`: command-line executable (links `gpusim_core`)

Requirements:

- CMake `>= 3.20`
- A C++17-capable compiler (`CMAKE_CXX_STANDARD 17`)
- No explicit third-party dependencies currently (standard library only)

## Recommended build (out-of-source)

Convention: run the following commands from the repo root.

### Windows (MSVC / Visual Studio 2022, multi-config)

1) Install Visual Studio 2022 (workload: Desktop development with C++) and ensure you have:

- MSVC toolset
- Windows SDK
- CMake tools (optional, recommended)

2) Configure and build:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

3) Typical artifact location:

- `build/Release/gpu-sim-cli.exe`

Debug build:

```bat
cmake --build build --config Debug
```

### Windows / Linux / macOS (Ninja, single-config)

If you have Ninja installed (recommended), single-config builds are typically faster:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Typical artifact locations:

- Windows: `build/gpu-sim-cli.exe`
- Linux/macOS: `build/gpu-sim-cli`

Debug build:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

### Linux / macOS (Unix Makefiles)

```bash
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Running and directory conventions

`gpu-sim-cli` defaults to repo-relative inputs:

- PTX: `assets/ptx/demo_kernel.ptx`
- PTX ISA map: `assets/ptx_isa/demo_ptx64.json`
- Instruction descriptor: `assets/inst_desc/demo_desc.json`
- Config: `assets/configs/demo_config.json`

So it’s recommended to run from the **repo root**, or pass full paths via `--ptx/--ptx-isa/--inst-desc/--config`.

Notes

- `scripts/run_unit_tests.*` / `scripts/run_integration_tests.*` automatically `cd` to the repo root and will auto-build if the build dir or executables are missing.

### Example run

(from the repo root)

```bash
./build/gpu-sim-cli --help
```

With no args, it uses defaults and writes:

- trace: `out/trace.jsonl`
- stats: `out/stats.json`

Output contract notes (v1 baseline)

- Line 1 of `trace.jsonl` is `TRACE_HEADER` (includes `format_version/schema/profile/deterministic`).
- Top-level fields in `stats.json` include `format_version/schema/profile/deterministic` (and `counters`).

You can also specify output paths:

```bash
./build/gpu-sim-cli \
  --ptx assets/ptx/demo_kernel.ptx \
  --ptx-isa assets/ptx_isa/demo_ptx64.json \
  --inst-desc assets/inst_desc/demo_desc.json \
  --config assets/configs/demo_config.json \
  --trace out/trace.jsonl \
  --stats out/stats.json
```

---

## Tier‑0 (single merge gate) quick regression

Tier‑0 CTest name: `gpu-sim-tiny-gpt2-mincov-tests`

Run only Tier‑0:

```bash
ctest --test-dir build -C Release -V -R "^gpu-sim-tiny-gpt2-mincov-tests$"
```

Or via scripts (auto-builds if missing artifacts):

- Windows: `scripts\run_unit_tests.bat build`
- Bash: `bash scripts/run_unit_tests.sh build`

The repo also includes more targeted regressions (also included by default in `run_unit_tests.*`):

- `gpu-sim-inst-desc-tests`
- `gpu-sim-simt-tests`
- `gpu-sim-memory-tests` (memory.model / addrspace paths and diagnostic-code lock-in)
- `gpu-sim-public-api-tests` (public Runtime API: in-memory PTX + in-memory JSON assets regression)

`gpu-sim-public-api-tests` is packaging/embedding friendly: it does not rely on `assets/` file paths, and instead validates the public API with in-memory PTX and in-memory JSON assets.

User-facing reference:

- `docs/doc_user/public_api.md`

---

## Multi-SM parallel execution (06.02)

This project supports a baseline “one host thread per SM” parallel execution mode (see user doc: [../doc_user/sm_parallel_execution.md](../doc_user/sm_parallel_execution.md)).

Enable it via the config `sim` section:

- `sim.sm_count`: number of SM workers (>1 to matter)
- `sim.parallel`: enable parallel workers
- `sim.deterministic`: deterministic regression mode (baseline: when true, parallel is disabled)

A parallel example config is provided:

- `assets/configs/demo_parallel_config.json`

Example run (from repo root):

```bash
./build/gpu-sim-cli \
  --config assets/configs/demo_parallel_config.json \
  --workload assets/workloads/smoke_single_stream.json \
  --trace out/parallel.trace.jsonl \
  --stats out/parallel.stats.json
```

---

## Modular architecture selection (10: HW/SW mapping)

You can select swappable modules/strategies via the config file to compose a target architecture (see design/implementation docs:

- `docs/doc_design/modules/10_modular_hw_sw_mapping.md`
- `docs/doc_dev/modules/10_modular_hw_sw_mapping.md`

### Config entry points (selectors / profile)

Currently supported selector entry points (kept for backward compatibility):

- `sim.cta_scheduler`: CTA distribution policy (default `fifo`)
- `sim.warp_scheduler`: warp issue policy (default `in_order_run_to_completion`)
- `memory.model`: memory model selector (default `no_cache_addrspace`)
- `sim.allow_unknown_selectors`: whether unknown selectors are allowed (default `false`, i.e. strict by default)

The preferred style is:

- `arch.profile`: architecture profile name (currently `baseline_no_cache`)
- `arch.components`: component override map (takes priority over `sim.*` selectors)

A demo config is provided:

- `assets/configs/demo_modular_selectors.json`
  - enables 2-SM parallel
  - `cta_scheduler=sm_round_robin`
  - `warp_scheduler=round_robin_interleave_step`

### Example run

(from repo root, Ninja/Unix Makefiles)

```bash
./build/gpu-sim-cli \
  --config assets/configs/demo_modular_selectors.json \
  --ptx assets/ptx/demo_kernel.ptx \
  --ptx-isa assets/ptx_isa/demo_ptx64.json \
  --inst-desc assets/inst_desc/demo_desc.json \
  --grid 4,1,1 \
  --block 32,1,1 \
  --trace out/modsel.trace.jsonl \
  --stats out/modsel.stats.json
```

(Visual Studio multi-config)

```bat
build\Release\gpu-sim-cli.exe --config assets\configs\demo_modular_selectors.json --grid 4,1,1 --block 32,1,1
```

### How to confirm selectors took effect (trace)

The trace includes a one-time `RUN_START` event. Its `extra` field carries a config_summary (profile/components/sm_count/parallel/deterministic).

For minimal verification, you can use the integration-test scripts (see `docs/doc_user/scripts.md`).

- Windows: `scripts\run_integration_tests.bat build`
- Bash: `bash scripts/run_integration_tests.sh build`

The scripts validate:

- trace contains `"action":"RUN_START"`
- the modular selector strings (e.g. `sm_round_robin` and `round_robin_interleave_step`) are observable in the trace

## 3D kernel launch (grid/block)

`gpu-sim-cli` supports launching kernels with 3D `grid_dim` / `block_dim` (aligned with the abstract semantics in `doc_design/modules/06.01_launch_grid_block_3d.md`).

CLI flags

- `--grid x,y,z`: gridDim (3D CTA counts)
- `--block x,y,z`: blockDim (3D thread dimensions)

Defaults

- `--grid` defaults to `1,1,1`
- `--block` defaults to `<warp_size>,1,1` (warp_size comes from `sim.warp_size` in config)

Example run (from repo root)

```bash
./build/gpu-sim-cli --ptx assets/ptx/demo_kernel.ptx --grid 2,1,1 --block 32,1,1
```

## SIMT divergence demo (M5)

The repo provides a minimal divergent `bra` example PTX: `assets/ptx/demo_divergence.ptx`.

Example run

```bash
./build/gpu-sim-cli \
  --ptx assets/ptx/demo_divergence.ptx \
  --ptx-isa assets/ptx_isa/demo_ptx64.json \
  --inst-desc assets/inst_desc/demo_desc.json \
  --config assets/configs/demo_config.json \
  --block 2,1,1 \
  --trace out/diverge.trace.jsonl \
  --stats out/diverge.stats.json
```

How to validate

- Search for `SIMT_SPLIT` in the trace (indicates warp divergence split occurred).

Notes

- Whether the demo PTX “actually reads builtins (e.g. `%tid.x`)” depends on the PTX file and ISA/inst_desc assets you provide; launch dimensions flow through SIMT contexts (CTA/warp/lane).
- If `block_dim.x * block_dim.y * block_dim.z` is not a multiple of warp_size, the last warp is partial (inactive lanes are masked off).

---

## WorkloadSpec (streams/commands: `--workload`)

`gpu-sim-cli` supports a replayable workload input file (WorkloadSpec JSON) that describes:

- buffers (host/device)
- modules (bind ptx + ptx_isa + inst_desc)
- streams (per-stream FIFO commands)
- `event_record` / `event_wait` (cross-stream dependencies)

This aligns with the abstract/implementation design:

- `doc_design/modules/07_runtime_streaming.md`
- `doc_design/modules/07.01_stream_input_workload_spec.md`
- `doc_dev/modules/07_runtime_streaming.md`
- `doc_dev/modules/07.01_stream_input_workload_spec.md`

### Demo workloads

Two minimal smoke workloads are included:

- `assets/workloads/smoke_single_stream.json`
- `assets/workloads/smoke_two_stream_event.json`

### Example run

(from the repo root)

```bash
./build/gpu-sim-cli \
  --config assets/configs/demo_config.json \
  --workload assets/workloads/smoke_single_stream.json \
  --trace out/workload.trace.jsonl \
  --stats out/workload.stats.json
```

Notes

- Workload mode is mutually exclusive with single-kernel flags (`--ptx/--ptx-isa/--inst-desc/--grid/--block/--io-demo`).
- Trace includes additional STREAM-category events (e.g. `cmd_enq/cmd_ready/cmd_submit/cmd_complete`) carrying `cmd_id/stream_id` (event-related commands also carry `event_id`).

---

## Kernel I/O + ABI demo (data input / param input / result output)

The repo includes a minimal end-to-end demo path to validate:

- kernel `.param` argument input (`ld.param`)
- kernel writes results to global memory (`st.global`)
- host explicitly reads back via D2H as “result output”

### Run after building (Ninja/Unix Makefiles)

From the repo root:

```bash
./build/gpu-sim-cli --ptx assets/ptx/write_out.ptx --io-demo
```

Expected: the console prints `io-demo u32 result: 42`, and trace/stats are produced as usual (default under `out/`).

### Run after building (Visual Studio multi-config)

```bat
build\Release\gpu-sim-cli.exe --ptx assets\ptx\write_out.ptx --io-demo
```

### Notes and limitations (current implementation)

- `--io-demo` assumes the kernel has a `.param .u64 out_ptr` parameter and writes a `u32` to that address.
- `.param` reads are currently bound by “parameter name symbol” (e.g. `[out_ptr]`), supporting `.u32` and `.u64` parameters.
- Global memory uses the minimal no-cache model primarily for bring-up/regression; a more complete streaming/engines model is still evolving.

## Troubleshooting

- `CMAKE_BUILD_TYPE` has no effect:
  - Visual Studio/Xcode are multi-config generators; use `cmake --build ... --config Release`.
  - Ninja/Makefiles are single-config; use `-DCMAKE_BUILD_TYPE=Release|Debug`.

- Compiler/generator not found:
  - Windows + MSVC: open a terminal via “x64 Native Tools Command Prompt for VS 2022”, then run CMake.
  - Ninja: ensure `ninja` is on PATH (or install VS-bundled Ninja / standalone Ninja).

- Runtime cannot find resource files (PTX/JSON):
  - Ensure your working directory is the repo root, or pass correct paths via `--ptx/--desc/--config`.

## Build system details (facts aligned with code)

- Repo-root `CMakeLists.txt` declares:
  - `add_library(gpusim_core ...)` aggregates module implementations under `src/*`
  - `target_include_directories(gpusim_core PUBLIC include)` exposes public headers `include/gpusim/*`
  - `add_executable(gpu-sim-cli src/apps/cli/main.cpp)`
  - `target_link_libraries(gpu-sim-cli PRIVATE gpusim_core)`
  - Build standard: C++17 and compiler extensions disabled (`CMAKE_CXX_EXTENSIONS OFF`)

---

## Running tests (CTest)

This repo uses CTest to register test cases (see repo-root `CMakeLists.txt`).

Common unit-test CTests (all covered by `scripts/run_unit_tests.*`):

- `gpu-sim-tests`
- `gpu-sim-inst-desc-tests` (targeted tests around inst_desc loader/contracts)
- `gpu-sim-simt-tests` (targeted tests around SIMT predication/uniform-only/next_pc routing)
- `gpu-sim-builtins-tests`
- `gpu-sim-config-parse-tests`
- `gpu-sim-tiny-gpt2-mincov-tests` (Tier‑0 gate)

Ninja/Makefiles (single-config)

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -V
```

Visual Studio (multi-config)

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release -V
```

---

## tiny GPT-2 bring-up: minimal coverage regression (M1–M4)

The repo contains an end-to-end regression to ensure the minimal loop required for tiny GPT-2 bring-up stays continuously available:

- CTest: `gpu-sim-tiny-gpt2-mincov-tests`

It covers:

- M1: `ld/st.global.f32` + `fma.f32` + predication
- M4: `bra` loop (uniform control-flow)

### How to run (recommended)

Windows (cmd):

```bat
scripts\run_unit_tests.bat build
scripts\run_integration_tests.bat build
```

Bash (Git Bash / WSL / Linux / macOS):

```bash
bash scripts/run_unit_tests.sh build
bash scripts/run_integration_tests.sh build
```

### Run CTest directly

```bash
ctest --test-dir build -C Release -V -R "^gpu-sim-tiny-gpt2-mincov-tests$"
```

For more details, see the user doc: `docs/doc_user/tiny_gpt2_minimal_coverage.md`.
