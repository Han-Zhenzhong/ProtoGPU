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
- PTX ISA map：`assets/ptx_isa/demo_ptx64.json`
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
  --ptx-isa assets/ptx_isa/demo_ptx64.json \
  --inst-desc assets/inst_desc/demo_desc.json \
  --config assets/configs/demo_config.json \
  --trace out/trace.jsonl \
  --stats out/stats.json
```

---

## 多 SM 并行执行（06.02）

本项目支持“每个 SM 一个宿主线程”的并行执行基线（见用户文档 [docs/doc_user/sm_parallel_execution.md](../doc_user/sm_parallel_execution.md)）。

启用方式：在 config 的 `sim` 节点中设置：
- `sim.sm_count`：SM worker 数（>1 才有意义）
- `sim.parallel`：是否启用并行 worker
- `sim.deterministic`：确定性回归模式（当前基线：为 true 时会禁用并行）

仓库内提供了一个并行示例配置：
- `assets/configs/demo_parallel_config.json`

运行示例（从仓库根目录）：

```bash
./build/gpu-sim-cli \
  --config assets/configs/demo_parallel_config.json \
  --workload assets/workloads/smoke_single_stream.json \
  --trace out/parallel.trace.jsonl \
  --stats out/parallel.stats.json
```

---

## 模块化架构选择（10：HW/SW Mapping）

本项目支持通过配置文件选择“可替换模块/策略”，用于把不同的软件实现组合成一个目标架构（见设计/实现文档：
- `docs/doc_design/modules/10_modular_hw_sw_mapping.md`
- `docs/doc_dev/modules/10_modular_hw_sw_mapping.md`
）

### 配置入口（Selectors / Profile）

目前支持以下选择入口（保持向后兼容）：

- `sim.cta_scheduler`：CTA 分发策略（默认 `fifo`）
- `sim.warp_scheduler`：warp issue 策略（默认 `in_order_run_to_completion`）
- `memory.model`：内存模型选择器（默认 `no_cache_addrspace`）
- `sim.allow_unknown_selectors`：unknown selector 是否允许（默认 `false`，即默认严格模式）

更推荐的写法是使用：

- `arch.profile`：架构 profile 名称（当前提供 `baseline_no_cache`）
- `arch.components`：组件选择覆盖（优先级高于 `sim.*` selectors）

仓库内提供了一个演示配置：
- `assets/configs/demo_modular_selectors.json`
  - 启用 2 个 SM 并行
  - `cta_scheduler=sm_round_robin`
  - `warp_scheduler=round_robin_interleave_step`

### 运行示例

（从仓库根目录，Ninja/Unix Makefiles）

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

（Visual Studio multi-config）

```bat
build\Release\gpu-sim-cli.exe --config assets\configs\demo_modular_selectors.json --grid 4,1,1 --block 32,1,1
```

### 如何确认“选择生效”（Trace）

trace 中会出现一次性的 `RUN_START` 事件，`extra` 字段携带 config_summary（profile/components/sm_count/parallel/deterministic）。

你可以用 scripts 的集成测试做最小验证（见 `docs/doc_user/scripts.md`）：

- Windows：`scripts\run_integration_tests.bat build`
- Bash：`bash scripts/run_integration_tests.sh build`

其中会检查：
- trace 含 `"action":"RUN_START"`
- modular selectors trace 中能观察到 `sm_round_robin` 与 `round_robin_interleave_step`

## 3D Kernel Launch（grid/block）

`gpu-sim-cli` 支持以 3D `grid_dim` / `block_dim` 形式启动 kernel（对齐 `doc_design/modules/06.01_launch_grid_block_3d.md` 的抽象语义）。

CLI 参数
- `--grid x,y,z`：gridDim（三维 CTA 数）
- `--block x,y,z`：blockDim（三维 thread 维度）

默认值
- `--grid` 默认 `1,1,1`
- `--block` 默认 `<warp_size>,1,1`（warp_size 来自 config 中 `sim.warp_size`）

运行示例（从仓库根目录）

```bash
./build/gpu-sim-cli --ptx assets/ptx/demo_kernel.ptx --grid 2,1,1 --block 32,1,1
```

说明
- 当前 demo PTX 是否“实际使用 builtin（例如 `%tid.x`）”取决于你传入的 PTX 文件与 ISA/inst_desc 资产；launch 维度会贯穿 SIMT 侧上下文（CTA/warp/lane）。
- `block_dim.x * block_dim.y * block_dim.z` 不是 warp_size 的整数倍时，最后一个 warp 会是部分 warp（`active_mask` 会关闭无效 lanes）。

---

## WorkloadSpec（streams/commands：--workload）

`gpu-sim-cli` 支持一个“可重放的 workload 输入文件”（WorkloadSpec JSON），用来描述：
- buffers（host/device）
- modules（ptx + ptx_isa + inst_desc 绑定）
- streams（每 stream FIFO commands）
- event_record / event_wait（跨 stream 依赖）

该模式与抽象/实现设计对齐：
- `doc_design/modules/07_runtime_streaming.md`
- `doc_design/modules/07.01_stream_input_workload_spec.md`
- `doc_dev/modules/07_runtime_streaming.md`
- `doc_dev/modules/07.01_stream_input_workload_spec.md`

### Demo workloads

仓库内提供两个最小 smoke workload：
- `assets/workloads/smoke_single_stream.json`
- `assets/workloads/smoke_two_stream_event.json`

### 运行示例

（从仓库根目录）

```bash
./build/gpu-sim-cli \
  --config assets/configs/demo_config.json \
  --workload assets/workloads/smoke_single_stream.json \
  --trace out/workload.trace.jsonl \
  --stats out/workload.stats.json
```

说明
- `--workload` 模式与单-kernel参数（`--ptx/--ptx-isa/--inst-desc/--grid/--block/--io-demo`）互斥。
- trace 里会新增 `STREAM` 类别事件（例如 `cmd_enq/cmd_ready/cmd_submit/cmd_complete`），并携带 `cmd_id/stream_id`（event 相关命令额外携带 `event_id`）。

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

---

## 运行测试（CTest）

本仓库使用 CTest 注册测试用例（见根目录 `CMakeLists.txt`）。

Ninja/Makefiles（single-config）

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -V
```

Visual Studio（multi-config）

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release -V
```

Workload smoke tests（若启用 BUILD_TESTING）
- `gpu-sim-workload-smoke-single-stream`
- `gpu-sim-workload-smoke-two-stream-event`
