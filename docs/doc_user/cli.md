# gpu-sim-cli（命令行）

`gpu-sim-cli` 是最小可运行入口：加载 config/descriptor/PTX，执行仿真，并输出 trace/stats。

## 参数

- `--workload <file>`：运行 WorkloadSpec（streams/commands）JSON（与单-kernel模式互斥）
- `--ptx <file>`：PTX 输入文件路径
- `--ptx-isa <file>`：PTX ISA 映射表 JSON 路径（必需：PTX op 形态 → ir_op + operand_kinds）
- `--inst-desc <file>`：指令描述 JSON（inst_desc，IR 语义库：ir_op → uops）
  - 兼容别名：`--desc <file>`
- `--config <file>`：运行配置 JSON 路径
- `--trace <file>`：trace 输出路径（jsonl）
- `--stats <file>`：stats 输出路径（json）
- `--io-demo`：启用 Kernel I/O + ABI 的最小端到端演示（见下文）
- `--grid x,y,z`：设置 3D gridDim（默认 `1,1,1`）
- `--block x,y,z`：设置 3D blockDim（默认 `<warp_size>,1,1`；warp_size 来自 config 的 `sim.warp_size`）

## 默认输入

在仓库根目录运行时，默认使用：
- PTX：`assets/ptx/demo_kernel.ptx`
- PTX ISA map：`assets/ptx_isa/demo_ptx8.json`
- 指令描述（inst_desc）：`assets/inst_desc/demo_desc.json`
- 配置：`assets/configs/demo_config.json`

## 常规运行

```bash
./build/gpu-sim-cli \
  --ptx assets/ptx/demo_kernel.ptx \
  --ptx-isa assets/ptx_isa/demo_ptx8.json \
  --inst-desc assets/inst_desc/demo_desc.json \
  --config assets/configs/demo_config.json \
  --grid 1,1,1 \
  --block 32,1,1 \
  --trace out/trace.jsonl \
  --stats out/stats.json
```

## 多 SM 并行执行（06.02）

`gpu-sim-cli` 支持“每 SM 一个宿主线程”的并行执行基线，配置与限制见：
- [doc_user/sm_parallel_execution.md](sm_parallel_execution.md)

## 模块化架构选择（10：profile/components）

如果你希望“只改配置就切换 CTA/warp 调度策略与内存模型选择器”，参照：
- [doc_user/modular_hw_sw_mapping.md](modular_hw_sw_mapping.md)

提示：运行时 trace 会写入一次性的 `RUN_START` 事件，用于观察本次运行实际选择了哪些组件组合。

## WorkloadSpec（--workload：streams/commands）

用途
- 用一个“可重放”的 JSON 文件描述：buffers/modules/streams/commands。
- 支持多 stream FIFO 与 `event_record` / `event_wait` 的依赖判定；trace 可复盘 `cmd_enq/cmd_ready/cmd_submit/cmd_complete`。

参照文档
- 抽象设计：`doc_design/modules/07_runtime_streaming.md`、`doc_design/modules/07.01_stream_input_workload_spec.md`
- 实现设计：`doc_dev/modules/07_runtime_streaming.md`、`doc_dev/modules/07.01_stream_input_workload_spec.md`

运行示例

```bash
./build/gpu-sim-cli \
  --config assets/configs/demo_config.json \
  --workload assets/workloads/smoke_single_stream.json \
  --trace out/workload.trace.jsonl \
  --stats out/workload.stats.json
```

模式互斥
- 提供 `--workload` 时，禁止组合单-kernel参数：`--ptx/--ptx-isa/--inst-desc/--grid/--block/--io-demo`。

WorkloadSpec v0（当前实现能力）
- buffers
  - `buffers.host[name]`: `{ bytes, init? }`，init 支持：`zeros`、`{hex: "..."}`、`{file: "path"}`
  - `buffers.device[name]`: `{ bytes, align? }`
- modules
  - `modules[name]`: `{ ptx, ptx_isa, inst_desc }`
- streams
  - `streams[name].commands[]`: oneof：`copy/kernel/event_record/event_wait/sync`

支持的命令（v0 基线）
- `copy.kind`：目前仅支持 `H2D` 与 `D2H`（`D2D/MEMSET` 会报 schema/限制错误）
- `kernel`：必须包含 `module/entry/grid_dim/block_dim/args`
  - `grid_dim/block_dim` 支持两种写法：`{x,y,z}` 或 `[x,y,z]`
  - `args` oneof：`{u32: N}` / `{u64: N}` / `{ptr_device: "dev_buf_name"}` / `{bytes_hex: "..."}`
- `event_record/event_wait`：`{ event: "NAME" }`（event name 会 deterministically 映射到 EventId）
- `sync {}`：作为显式同步点（当前实现为同步 runtime 下的 barrier/no-op，但会出现在 trace 的 cmd 生命周期中）

Workload schema 文件
- `schemas/workload.schema.json` 作为“字段规范/契约”存在。
- 当前实现采用“结构化校验 + 语义引用校验”的方式，不依赖通用 JSON Schema 引擎。

错误处理（基线）
- JSON 解析/结构不合法：`runtime:E_WORKLOAD_SCHEMA`
- 引用不存在的 buffer/module/event：`runtime:E_WORKLOAD_REF`
- kernel entry 不存在：`runtime:E_ENTRY_NOT_FOUND`
- 参数缺失/类型不匹配：`runtime:E_BAD_ARGS`
- launch 维度非法：`runtime:E_LAUNCH_DIM` / `runtime:E_LAUNCH_OVERFLOW`
- 所有 stream 的队首都不 ready（常见原因：等待一个从未 record 的 event）：`runtime:E_WORKLOAD_DEADLOCK`

注意（当前实现限制）
- runtime/streaming 的 “engines 分层 + async tick” 仍在演进；WorkloadSpec v0 目前以同步执行实现 FIFO 与 event 依赖语义。
- `--workload` 运行 kernel 时，会按 `modules[name]` 指定的 `ptx/ptx_isa/inst_desc` 加载并执行；为保证可重放，请在 workload 里显式绑定这些路径。

## 3D Kernel Launch（grid/block）语义

`--grid` 与 `--block` 控制一次 kernel launch 的执行域：

- `grid_dim = (grid.x, grid.y, grid.z)`：CTA（block）数量
- `block_dim = (block.x, block.y, block.z)`：每个 CTA 内线程数为 `block.x * block.y * block.z`

SIMT 侧会对 CTA/warp/lane 做确定性枚举，并为部分 warp 设置 `active_mask`：
- 当 `threads_per_block` 不是 `warp_size` 的整数倍时，最后一个 warp 只激活前 `threads_per_block % warp_size` 个 lanes。

示例

```bash
# 2 个 CTA；每个 CTA 为 40 threads（会产生 2 个 warp，其中第 2 个 warp 只激活 8 lanes）
./build/gpu-sim-cli --grid 2,1,1 --block 40,1,1
```

错误处理（基线）
- 任一维度为 0：`runtime:E_LAUNCH_DIM`
- `block.x * block.y * block.z` 乘法溢出：`runtime:E_LAUNCH_OVERFLOW`

## builtin（例如 %tid.x / %ctaid.x）

本项目将 builtin 作为一种操作数类型（`special`）来支持。当前基线支持：
- `%tid.{x,y,z}`、`%ntid.{x,y,z}`、`%ctaid.{x,y,z}`、`%nctaid.{x,y,z}`
- `%laneid`、`%warpid`

注意
- 是否能“执行到 builtin 读取”取决于你提供的 `--ptx-isa` / `--inst-desc` 是否包含对应 form（例如 `mov.u32 (reg, special)`）。
- demo 资产已提供最小 form：`mov.u32 %r0, %tid.x;` 能映射并执行。

## PTX op → IR op 映射（`--ptx-isa`）

用户侧只需要关心“PTX8 的指令长什么样，映射到哪个通用 IR opcode”。这一步由 `--ptx-isa` 提供的 PTX ISA map 完成：

- `ptx_opcode`：PTX 指令名（例如 `mov/add/ld/ret`）
- `type_mod`：类型修饰（例如 `u32/u64`；空字符串表示 wildcard）
- `operand_kinds`：该 PTX form 期望的操作数种类序列（例如 `reg/imm/symbol/addr/pred`）
- `ir_op`：映射到的 IR opcode（后续用 `--inst-desc` 查语义并展开 uops）

示例：同一个 PTX opcode `mov.u32` 可能有多种 form，但都可以映射到同一个 IR opcode `mov`：

```json
{
  "ptx_opcode": "mov",
  "type_mod": "u32",
  "operand_kinds": ["reg", "imm"],
  "ir_op": "mov"
}
```

```json
{
  "ptx_opcode": "mov",
  "type_mod": "u32",
  "operand_kinds": ["reg", "symbol"],
  "ir_op": "mov"
}
```

mapper 会按 `operand_kinds` 尝试解析 token：

- `mov.u32 %r0, 0;` → 匹配 `(reg,imm)`
- `mov.u32 %r0, foo;` → 匹配 `(reg,symbol)`

如果同一条 PTX 指令被多条 entry 同时匹配，会报 `DESC_AMBIGUOUS`，并在 message 里列出所有匹配到的候选签名（`(operand_kinds) -> ir_op.type_mod`），便于你把 PTX ISA map 调整到“只有一条能匹配”。

注意：`--ptx-isa` 只决定 PTX→IR 的映射；真正执行语义来自 `--inst-desc`（IR opcode → uops）。因此你在 `--ptx-isa` 里引入新的 `ir_op` 或新的 operand form 时，也需要在 inst_desc 里提供对应语义条目。

## Kernel I/O 演示（`--io-demo`）

用途
- 验证：`.param` 参数输入 → kernel 写 global memory → host 显式 D2H 回读

运行

```bash
./build/gpu-sim-cli --ptx assets/ptx/write_out.ptx --io-demo
```

预期输出
- 终端打印：`io-demo u32 result: 42`
- `out/trace.jsonl` 与 `out/stats.json` 正常生成

## 测试（scripts/）

如需快速回归单元/集成测试，直接使用仓库根目录的脚本入口，见 [doc_user/scripts.md](scripts.md)。
