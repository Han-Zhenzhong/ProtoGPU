# Public Runtime API（C++）

本页面向“把 ProtoGPU 当作一个 C++ 库嵌入到自己工程里”的使用者：

- 不依赖仓库目录结构（无需 `assets/...` 路径）
- 可直接传入 **PTX text** 与 **JSON assets text**（`ptx_isa` / `inst_desc`）
- 可使用 Runtime 提供的 host/device 内存与 memcpy helpers

入口头文件：`include/gpusim/runtime.h`

---

## 1. 两类入口：file-path vs in-memory

### 1.1 file-path（与 CLI 等价）

当你已有文件资产（PTX/JSON 都在磁盘上），使用：

- `Runtime::run_ptx_kernel(...)`
- `Runtime::run_ptx_kernel_with_args(...)`
- `Runtime::run_ptx_kernel_*_entry_launch(...)`

这些 API 的语义与 `gpu-sim-cli` 的 `--ptx/--ptx-isa/--inst-desc/--grid/--block` 一致。

### 1.2 in-memory（面向打包/嵌入）

当你希望把 PTX 与 JSON 资产“打包在程序里”（字符串、网络下发、数据库读取等），使用：

- `Runtime::run_ptx_kernel_text(ptx_text, ptx_isa_json_text, inst_desc_json_text)`
- `Runtime::run_ptx_kernel_with_args_text(..., args)`
- 以及对应的 `*_launch` / `*_entry_launch` 变体

说明
- 这些 overload **不会改变语义**：内部仍然走同一条 Parser → Mapper → Descriptor → SIMT 的执行链。
- `*_entry_launch` 允许在 PTX module 里选择特定 `.entry`。

---

## 2. LaunchConfig（grid/block/warp_size）

### 2.1 默认值（非 `_launch` 版本）

`run_ptx_kernel_text(...)` / `run_ptx_kernel_with_args_text(...)` 的默认 launch 为：

- `grid_dim = (1,1,1)`
- `block_dim = (warp_size,1,1)`（warp_size 来自 `AppConfig.sim.warp_size`）
- `launch.warp_size = AppConfig.sim.warp_size`

### 2.2 显式 launch

如需控制 `grid/block` 或覆盖 warp_size，使用 `*_launch` 版本。

---

## 3. Kernel 参数（KernelArgs / param blob）

参数通过 `KernelArgs` 传入，其中：

- `KernelArgs.blob`：小端序 bytes，按 `.param` 的 layout 顺序写入

推荐做法
- 用户侧维护一个简单的 pack helper（例如 pack u32/u64/pointer）。
- 每次 run 前都显式填充 `KernelArgs.blob`。

注意
- Runtime 会在“无 args 的 run”时把 param blob 清空，避免跨 run 隐式复用。

---

## 4. 内存与 memcpy helpers

Runtime 提供最小的一组“方便写回归/嵌入 demo”的 helpers：

- Host buffer：`host_alloc/host_write/host_read`
- Device global：`device_malloc`
- Copy：`memcpy_h2d` / `memcpy_d2h`

语义（基线）
- device memory 使用 no-cache + addrspace 的语义模型。
- global read/write 越界或访问未分配区域会 fail-fast（通常以 Diagnostic 或抛异常的方式暴露）。

---

## 5. 错误处理与 Diagnostic

所有 run 返回 `RunOutputs`：

- `out.sim.completed`：是否完成执行
- `out.sim.diag`：失败时的诊断信息（module/code/message 以及可选源位置等）

建议的调用约定
- **先检查** `completed` / `diag`，再做后续读取（例如 D2H 回读）。

常见诊断码（非穷举）
- `runtime:E_ENTRY_NOT_FOUND`：指定 entry 名称不存在
- `runtime:E_LAUNCH_DIM` / `runtime:E_LAUNCH_OVERFLOW`：launch 维度非法
- `instruction:DESC_NOT_FOUND`：PTX ISA map 缺少某条指令的映射
- `instruction:OPERAND_FORM_MISMATCH` / `frontend:OPERAND_PARSE_FAIL`：operand form 不匹配或解析失败
- `simt:E_DESC_MISS`：inst_desc 缺少某条 IR 指令的语义描述
- `simt:E_DIVERGENCE_UNSUPPORTED`：出现分歧控制流（当前里程碑不支持）
- `simt:E_MEMORY_MODEL`：配置选择了未知 memory model（且 strict）

---

## 6. 资产 JSON 的最小要求（ptx_isa / inst_desc）

你需要同时提供：

- `ptx_isa`：把 PTX form 映射到 IR opcode（`ir_op`）
- `inst_desc`：为 IR opcode 提供可执行的 uops 语义

如果你在 `ptx_isa` 里引入了新的 `ir_op` 或新的 operand form，请同步在 `inst_desc` 里补齐对应条目，否则会在 SIMT 执行时出现 descriptor miss。

---

## 7. 回归入口

仓库提供了一个“完全不依赖 assets 路径”的回归用例：

- CTest：`gpu-sim-public-api-tests`

它会覆盖：
- in-memory PTX + in-memory JSON assets 的最小执行链
- 带参数的 kernel 与 global 写回 + host 回读
