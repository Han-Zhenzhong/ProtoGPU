# scripts/

> Chinese version: [README.zh-CN.md](README.zh-CN.md)

Script entry points.

## Purpose

- Helper scripts for local build/run/test/format (Windows-friendly).

## Notes

- Build and environment requirements are documented here:
  - [docs/doc_build/build.md](../docs/doc_build/build.md)
- `run_unit_tests.*` / `run_integration_tests.*` will automatically call `scripts/build_all.sh` on Bash-based platforms or `scripts/build.bat` on Windows to configure + build if the `build/` directory or target executables are missing.

## Test scripts

This directory provides two entry points:

- Unit tests: run `gpu-sim-tests` / `ctest`
- Integration tests: run `gpu-sim-cli` demo paths and validate key outputs/artifacts

On Linux/WSL, integration tests will also try to run an end-to-end CUDA Runtime shim demo regression (if `cuda/demo/demo` exists and the shim has been built):

- `scripts/run_cuda_shim_demo_integration.sh`
- `scripts/run_cuda_shim_multi_ptx_demo_integration.sh`

If clang + CUDA Toolkit are installed (i.e. `CUDA_PATH/include/cuda_runtime.h` is found), integration tests will also attempt to:

- Build and run `cuda/demo/streaming_demo.cu` (via the shim + `GPUSIM_CUDART_SHIM_PTX_OVERRIDE`, which may be a single PTX path or a `:`-delimited PTX path list on Linux/WSL)
- Script entry: `scripts/run_cuda_shim_streaming_demo_cu.sh`
- Build and run `cuda/demo/warp_reduce_add_demo_executable.cu`, and generate PTX override from `cuda/demo/warp_reduce_add_demo_ptx.cu` (inline PTX `warp_reduce_add` end-to-end path via shim + PTX override)
- Script entry: `scripts/run_cuda_shim_warp_reduce_add_demo_cu.sh`

In addition, the test scripts include a minimal end-to-end tiny GPT-2 bring-up regression (CTests):

- `gpu-sim-tiny-gpt2-mincov-tests`

By default, the build directory is `build/`. You can also pass a custom build directory as the first argument.

### Windows (cmd)

```bat
scripts\run_unit_tests.bat build
scripts\run_integration_tests.bat build
```

For Visual Studio multi-config generators, set `CONFIG=Debug|Release`:

```bat
set CONFIG=Release
scripts\run_unit_tests.bat build
```

### Bash (Git Bash / WSL / Linux / macOS)

```bash
bash scripts/run_unit_tests.sh build
bash scripts/run_integration_tests.sh build
```

You can also use `CONFIG=Debug|Release` for multi-config:

```bash
CONFIG=Release bash scripts/run_integration_tests.sh build
```

## Tier-0 (merge gate)

The single Tier-0 merge-gate CTest name is:

- `gpu-sim-tiny-gpt2-mincov-tests`

To run only Tier-0:

```bash
ctest --test-dir build -C Release -V -R "^gpu-sim-tiny-gpt2-mincov-tests$"
```
