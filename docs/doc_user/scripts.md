# scripts/ (test scripts)

> Chinese version: [scripts.zh-CN.md](scripts.zh-CN.md)

The repo-root `scripts/` directory provides quick entry points to run unit tests and integration tests, which is handy for fast local regression.

## Prerequisites

- It’s recommended to run the scripts from the **repo root** (they use relative paths to assets).
- Note: the scripts will automatically `cd` to the repo root internally, so you can also invoke them from any working directory.

Currently, `run_unit_tests.*` / `run_integration_tests.*` will auto-call `scripts/build_all.sh` on Bash-based platforms or `scripts/build.bat` on Windows (configure + build) if the build directory or target executables are missing.

## Build (optional manual step)

Purpose: run CMake configure + build.

Windows (cmd)

```bat
scripts\build.bat build Release
```

Bash (Git Bash / WSL / Linux / macOS)

```bash
bash scripts/build_all.sh build Release
```

Common environment variables

- `CONFIG=Debug|Release`: selects the multi-config configuration (Visual Studio / Xcode); for single-config Ninja it is also used as `CMAKE_BUILD_TYPE`
- `BUILD_TESTING=ON|OFF`: whether to build test targets (default: ON)
- `GENERATOR`: force a specific CMake generator (e.g. `Ninja`); if not set, the scripts prefer Ninja automatically when available

## Unit tests

Purpose: run `gpu-sim-tests` (or run registered CTest cases via `ctest`).

Windows (cmd)

```bat
scripts\run_unit_tests.bat build
```

Bash (Git Bash / WSL / Linux / macOS)

```bash
bash scripts/run_unit_tests.sh build
```

Notes

- The scripts prefer `ctest --test-dir <build>`; if `ctest` is not available, they attempt to run the unit test executable directly.
- For Visual Studio / Xcode multi-config generators, use `CONFIG=Debug|Release` to select the configuration.

Unit tests currently also include a minimal end-to-end tiny GPT-2 bring-up regression:

- `gpu-sim-tiny-gpt2-mincov-tests` (M1 fma/ld/st/predication + M4 bra loop)

It also includes more targeted tests (for example, around inst_desc loading/contracts):

- `gpu-sim-inst-desc-tests`
- `gpu-sim-simt-tests` (SIMT predication / uniform-only / next_pc routing)
- `gpu-sim-memory-tests` (memory model / addrspace paths and diagnostic-code lock-in)
- `gpu-sim-observability-contract-tests` (trace/stats output contract: TRACE_HEADER + stats meta fields)
- `gpu-sim-public-api-tests` (public Runtime API: in-memory PTX + in-memory JSON assets regression)

There are also regressions that lock in fail-fast behaviors (e.g. global OOB / unallocated access must error, and global access alignment checks).

## Integration tests

Purpose: run `gpu-sim-cli` along a demo path and perform minimal validation:

- smoke: run a demo kernel (verify trace/stats artifacts exist and are non-empty)
- trace metadata: verify trace contains a one-time `RUN_START` (to observe profile/components selection)
- io-demo: run `--io-demo` (verify stdout contains `io-demo u32 result: 42`)
- modular selectors: run `assets/configs/demo_modular_selectors.json` and verify the selector string is observable in the trace

Also (if `ctest` exists), it runs:

- `gpu-sim-tiny-gpt2-mincov-tests` (tiny GPT-2 minimal coverage regression)

Windows (cmd)

```bat
scripts\run_integration_tests.bat build
```

Bash (Git Bash / WSL / Linux / macOS)

```bash
bash scripts/run_integration_tests.sh build
```

Outputs

- Artifact directory: `<build>/test_out/`
- Contains: stdout/stderr logs, trace/stats, etc., which helps debug failures

## More details

- Script implementation and argument conventions: [../../scripts/README.md](../../scripts/README.md)
