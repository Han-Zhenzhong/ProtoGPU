# scripts/（测试脚本）

仓库根目录的 scripts/ 提供了“跑单元测试 / 跑集成测试”的快捷入口，适合本地快速回归。

## 前置条件

- 需要你已经完成 CMake configure + build（产物在 build/ 或你自定义的 build 目录）
- 建议从“仓库根目录”执行脚本（使用相对路径资源）

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

- 脚本优先使用 `ctest --test-dir <build>`；若系统没有 ctest，则直接尝试运行 `gpu-sim-tests` 可执行文件。
- Visual Studio / Xcode 等 multi-config 生成器可通过 `CONFIG=Debug|Release` 选择配置。

## 集成测试

用途：运行 `gpu-sim-cli` 的 demo 路径并做最小校验：

- smoke：运行 demo kernel（检查 trace/stats 产物存在且非空）
- io-demo：运行 `--io-demo`（检查 stdout 包含 `io-demo u32 result: 42`）

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

- 脚本实现与参数约定见 [scripts/README.md](../scripts/README.md)。
