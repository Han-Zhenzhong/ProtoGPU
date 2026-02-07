# scripts/

脚本入口。

用途
- 本地 build/run/test/format 的辅助脚本（Windows 友好）。

说明
- 第一版交付后再在 docs/doc_build/ 中补齐构建文档；脚本可先按需要逐步增加。

## 测试脚本

本目录提供两个入口：

- 单元测试：运行 `gpu-sim-tests` / `ctest`
- 集成测试：运行 `gpu-sim-cli` 的 demo 路径并校验关键输出/产物

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
```

同样可用 `CONFIG=Debug|Release` 选择 multi-config 的配置：

```bash
CONFIG=Release bash scripts/run_integration_tests.sh build
```
