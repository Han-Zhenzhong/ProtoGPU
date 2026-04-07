# scripts/

脚本入口。

用途
- 本地 build/run/test/format 的辅助脚本（Windows 友好）。

说明
- 构建方式与环境要求见构建文档：
	- [docs/doc_build/build.zh-CN.md](../docs/doc_build/build.zh-CN.md)
- `run_unit_tests.*` / `run_integration_tests.*` 在发现 build 目录或目标可执行文件不存在时，会自动先调用 Bash 平台上的 `scripts/build_all.sh` 或 Windows 上的 `scripts/build.bat` 进行 configure + build。
- Bash 平台上如果要清理本地构建产物，可使用 `scripts/clean_all_builds.sh`。

## 测试脚本

本目录提供两个入口：

- 单元测试：运行 `gpu-sim-tests` / `ctest`
- 集成测试：运行 `gpu-sim-cli` 的 demo 路径并校验关键输出/产物

在 Linux/WSL 上，集成测试还会尝试跑一个 CUDA Runtime shim 的端到端 demo 回归（如果 `cuda/demo/demo` 可执行且 shim 已构建）：
- `scripts/run_cuda_shim_demo_integration.sh`
- `scripts/run_cuda_shim_multi_ptx_demo_integration.sh`

如果本机安装了 clang + CUDA Toolkit（能找到 `CUDA_PATH/include/cuda_runtime.h`），集成测试也会尝试：
- 编译并运行 `cuda/demo/demo.cu`，并从同一源码生成 PTX override，用于基础 shim demo 路径
- 脚本入口：`scripts/run_cuda_shim_demo_integration.sh`
- 编译并运行 `cuda/demo/streaming_demo.cu`（通过 shim + `GPUSIM_CUDART_SHIM_PTX_OVERRIDE`；在 Linux/WSL 上该变量可以是单个 PTX 路径或用 `:` 分隔的 PTX 路径列表）
- 脚本入口：`scripts/run_cuda_shim_streaming_demo_cu.sh`
- 编译并运行 `cuda/demo/warp_reduce_add_demo_executable.cu`，并从 `cuda/demo/warp_reduce_add_demo_ptx.cu` 生成 PTX override（通过 shim 验证 inline PTX `warp_reduce_add` 端到端路径）
- 脚本入口：`scripts/run_cuda_shim_warp_reduce_add_demo_cu.sh`

同时，测试脚本会包含一个 tiny GPT-2 bring-up 的最小端到端回归（CTests）：
- `gpu-sim-tiny-gpt2-mincov-tests`

默认约定 build 目录为 `build/`；你也可以传入自定义 build 目录作为第一个参数。

### Windows（cmd）

```bat
scripts\run_unit_tests.bat build
scripts\run_integration_tests.bat build
```

如使用 Visual Studio multi-config，设置环境变量 `CONFIG=Debug|Release`：

```bat
set CONFIG=Release
scripts\run_unit_tests.bat build
```

### Bash（Git Bash / WSL / Linux / macOS）

```bash
bash scripts/run_unit_tests.sh build
bash scripts/run_integration_tests.sh build
bash scripts/clean_all_builds.sh
```

同样可用 `CONFIG=Debug|Release` 选择 multi-config 的配置：

```bash
CONFIG=Release bash scripts/run_integration_tests.sh build

```

## Tier-0（merge gate）

Tier-0 的唯一 merge gate CTest 名称为：
- `gpu-sim-tiny-gpt2-mincov-tests`

如果你希望只跑 Tier-0：

```bash
ctest --test-dir build -C Release -V -R "^gpu-sim-tiny-gpt2-mincov-tests$"
```
