# scripts/（测试脚本）

仓库根目录的 scripts/ 提供了“跑单元测试 / 跑集成测试”的快捷入口，适合本地快速回归。

## 前置条件

- 建议从“仓库根目录”执行脚本（使用相对路径资源）。
- 说明：脚本内部会自动切换到仓库根目录执行，因此也可以从任意工作目录调用。

说明：目前 `run_unit_tests.*` / `run_integration_tests.*` 在发现 build 目录或目标可执行文件不存在时，会自动先调用 Bash 平台上的 `scripts/build_all.sh` 或 Windows 上的 `scripts/build.bat` 进行 configure + build。

## 构建（可选手动执行）

用途：执行 CMake configure + build。

Windows（cmd）

```bat
scripts\build.bat build Release
```

Bash（Git Bash / WSL / Linux / macOS）

```bash
bash scripts/build_all.sh build Release
```

常用环境变量

- `CONFIG=Debug|Release`：选择 multi-config 的配置（Visual Studio / Xcode）；Ninja 单配置时也会作为 `CMAKE_BUILD_TYPE` 使用
- `BUILD_TESTING=ON|OFF`：是否构建测试目标（默认 ON）
- `GENERATOR`：强制指定 CMake generator（例如 `Ninja`），不设置则优先自动使用 Ninja（如果系统有）

如果要在 Bash 平台上清理本地构建产物，可运行：

```bash
bash scripts/clean_all_builds.sh
```

## 单元测试

用途：运行 `gpu-sim-tests`（或通过 `ctest` 运行已注册的 CTest 用例）。

Windows（cmd）

```bat
scripts\run_unit_tests.bat build
```

Bash（Git Bash / WSL / Linux / macOS）

```bash
bash scripts/run_unit_tests.sh build
```

说明

- 脚本优先使用 `ctest --test-dir <build>`；若系统没有 ctest，则直接尝试运行单元测试可执行文件。
- Visual Studio / Xcode 等 multi-config 生成器可通过 `CONFIG=Debug|Release` 选择配置。

当前也会包含一个 tiny GPT-2 bring-up 的最小端到端回归：
- `gpu-sim-tiny-gpt2-mincov-tests`（M1 fma/ld/st/predication + M4 bra loop）

同时也会包含一些更 targeted 的单元测试（例如 inst_desc loader/契约相关）：
- `gpu-sim-inst-desc-tests`
- `gpu-sim-simt-tests`（SIMT predication/uniform-only/next_pc 分流相关）
- `gpu-sim-memory-tests`（memory.model / addrspace 路径与诊断码锁定回归）
- `gpu-sim-observability-contract-tests`（trace/stats 输出契约：TRACE_HEADER 与 stats meta 字段）
- `gpu-sim-public-api-tests`（public Runtime API：in-memory PTX + in-memory JSON assets 回归）

其中也包含若干 fail-fast 行为的锁定回归（例如 global OOB/未分配访问必须报错、以及 global 访问对齐检查）。

## 集成测试

用途：运行 `gpu-sim-cli` 的 demo 路径并做最小校验：

- smoke：运行 demo kernel（检查 trace/stats 产物存在且非空）
- trace 元信息：检查 trace 包含一次性 `RUN_START`（用于观测 profile/components 组合）
- io-demo：运行 `--io-demo`（检查 stdout 包含 `io-demo u32 result: 42`）
- modular selectors：运行 `assets/configs/demo_modular_selectors.json` 并检查选择器字符串在 trace 中可观察

同时（若系统存在 `ctest`）也会运行：
- `gpu-sim-tiny-gpt2-mincov-tests`（tiny GPT-2 最小覆盖回归）

Windows（cmd）

```bat
scripts\run_integration_tests.bat build
```

Bash（Git Bash / WSL / Linux / macOS）

```bash
bash scripts/run_integration_tests.sh build
```

输出

- 产物目录：`<build>/test_out/`
- 文件包含：stdout/stderr、trace/stats 等，便于定位失败原因

## 更详细说明

- 脚本实现与参数约定见 [scripts/README.zh-CN.md](../../scripts/README.zh-CN.md)。
