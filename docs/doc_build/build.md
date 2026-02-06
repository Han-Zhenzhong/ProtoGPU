# 如何编译（CMake Build Guide）

本仓库使用 CMake 构建（见根目录 `CMakeLists.txt`）。核心产物是：

- `gpusim_core`：核心库（静态/动态由生成器/平台决定）
- `gpu-sim-cli`：命令行可执行程序（链接 `gpusim_core`）

项目要求：

- CMake `>= 3.20`
- 支持 C++17 的编译器（`CMAKE_CXX_STANDARD 17`）
- 目前没有显式的第三方依赖（代码只用到标准库）

## 推荐的构建方式（Out-of-source）

统一约定：在仓库根目录执行以下命令。

### Windows（MSVC / Visual Studio 2022，Multi-config）

1) 安装 Visual Studio 2022（工作负载：Desktop development with C++），确保包含：
- MSVC toolset
- Windows SDK
- CMake tools（可选，但推荐）

2) 生成与编译：

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

3) 产物位置（常见）：
- `build/Release/gpu-sim-cli.exe`

调试构建：

```bat
cmake --build build --config Debug
```

### Windows / Linux / macOS（Ninja，Single-config）

如果你安装了 Ninja（推荐），使用单配置生成器通常更快：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

产物位置（常见）：
- Windows：`build/gpu-sim-cli.exe`
- Linux/macOS：`build/gpu-sim-cli`

调试构建：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

### Linux / macOS（Unix Makefiles）

```bash
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## 运行与目录约定

`gpu-sim-cli` 默认参数使用仓库内的相对路径资源：

- PTX：`assets/ptx/demo_kernel.ptx`
- PTX ISA map：`assets/ptx_isa/demo_ptx8.json`
- 指令描述：`assets/inst_desc/demo_desc.json`
- 配置：`assets/configs/demo_config.json`

因此建议在“仓库根目录”运行程序；或者显式传入 `--ptx/--ptx-isa/--inst-desc/--config` 的完整路径。

### 运行示例

（从仓库根目录）

```bash
./build/gpu-sim-cli --help
```

不传参数会使用默认输入，并写出到：

- trace：`out/trace.jsonl`
- stats：`out/stats.json`

你也可以指定输出路径：

```bash
./build/gpu-sim-cli \
  --ptx assets/ptx/demo_kernel.ptx \
  --ptx-isa assets/ptx_isa/demo_ptx8.json \
  --inst-desc assets/inst_desc/demo_desc.json \
  --config assets/configs/demo_config.json \
  --trace out/trace.jsonl \
  --stats out/stats.json
```

---

## Kernel I/O + ABI（数据输入/参数输入/结果输出）演示

仓库新增了一个最小端到端演示路径，用来验证：
- kernel `.param` 参数输入（`ld.param`）
- kernel 将结果写入 global memory（`st.global`）
- host 侧显式 D2H 回读作为“执行结果输出”

### 构建后运行（Ninja/Unix Makefiles）

从仓库根目录：

```bash
./build/gpu-sim-cli --ptx assets/ptx/write_out.ptx --io-demo
```

预期：终端打印一行 `io-demo u32 result: 42`，同时照常输出 trace/stats（默认到 `out/`）。

### 构建后运行（Visual Studio multi-config）

```bat
build\Release\gpu-sim-cli.exe --ptx assets\ptx\write_out.ptx --io-demo
```

### 说明与限制（当前实现）

- `--io-demo` 假设 kernel 形参包含一个 `.param .u64 out_ptr`，并向该地址写入一个 `u32`。
- `.param` 读取目前按“参数名 symbol”绑定（例如 `[out_ptr]`），支持 `.u32/.u64` 两类参数。
- global memory 读写为 no-cache 的最小模型，主要用于 bring-up/回归；更完整的 streaming/engines 模型仍在演进。

## 常见问题（Troubleshooting）

- `CMAKE_BUILD_TYPE` 不生效：
  - Visual Studio/Xcode 属于 multi-config 生成器，使用 `cmake --build ... --config Release` 选择配置。
  - Ninja/Makefiles 属于 single-config，使用 `-DCMAKE_BUILD_TYPE=Release|Debug`。

- 找不到编译器 / 生成器：
  - Windows + MSVC：用“x64 Native Tools Command Prompt for VS 2022”打开终端再运行 CMake。
  - Ninja：确认 `ninja` 在 PATH 中（或安装 VS 自带 Ninja / 独立 Ninja）。

- 运行时报找不到资源文件（PTX/JSON）：
  - 确认当前工作目录在仓库根目录；或用 `--ptx/--desc/--config` 指定正确路径。

## 构建系统细节（与代码一致的事实）

- 根目录 `CMakeLists.txt` 里声明：
  - `add_library(gpusim_core ...)` 聚合 `src/*` 下各模块实现
  - `target_include_directories(gpusim_core PUBLIC include)` 对外暴露头文件 `include/gpusim/*`
  - `add_executable(gpu-sim-cli src/apps/cli/main.cpp)`
  - `target_link_libraries(gpu-sim-cli PRIVATE gpusim_core)`
  - 编译标准：C++17，且禁用编译器扩展（`CMAKE_CXX_EXTENSIONS OFF`）
