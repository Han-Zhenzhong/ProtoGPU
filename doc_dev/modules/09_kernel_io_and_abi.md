# 09 Kernel I/O + ABI（实现落地：数据输入 / 参数输入 / 结果输出）

参照
- 抽象设计：[doc_design/modules/09_kernel_io_and_abi.md](../../doc_design/modules/09_kernel_io_and_abi.md)
- Runtime/Streaming：`doc_dev/modules/07_runtime_streaming.md`
- Engines：`doc_dev/modules/08_engines.md`
- Memory：`doc_dev/modules/04_memory.md`
- Contracts：`doc_dev/modules/00_contracts.md`

落地目标
- Host 侧可表达：H2D 初始化输入、launchKernel 传参、D2H 取回输出。
- `.param` 地址空间可读且可重放：同一组 kernel args 在相同 module/entry 下得到确定行为。
- 结果输出路径清晰：显式 D2H 负责数据结果，RunResult/trace/stats 负责控制结果与诊断。

落地位置（代码建议）
- `include/gpusim/runtime.h`：对外 Runtime API 形态（可先保持 CLI 驱动，但 API 需能表达 I/O + args）。
- `src/runtime/`：HostBuffer/命令构造/参数打包/同步点。
- `src/runtime/engines.*`：CopyEngine/ComputeEngine 对 Command payload 的执行。
- `include/gpusim/memory.h` + `src/memory/`：global + param（以及后续 const/shared/local）地址空间落地。
- `src/frontend/`：解析 `.entry` 参数表（param layout）。

---

## 1) 数据输入/输出：HostBuffer + COPY（H2D/D2H）

### 1.1 HostBuffer 落地
目标
- 用一个最小抽象承载“host 可见的数据”，支持：被 H2D 读取、被 D2H 写入、可用于测试校验。

建议类型与位置
- `src/runtime/host_buffer.{h,cpp}` 或 `src/common/host_buffer.{h,cpp}`（若希望测试复用）。

概念 API
```text
HostBufId host_alloc(bytes) -> HostBufId
host_write(id, offset, bytes)
host_read(id, offset, bytes) -> byte[]
```

实现建议
- 最小实现：`unordered_map<HostBufId, vector<uint8_t>>`。
- 诊断：越界访问返回 `Diagnostic{module:"runtime", code:"E_HOST_OOB"...}`。

### 1.2 DevicePtr 与 global memory
现状
- 当前 `AddrSpaceManager` 仅有 `write_global/read_global`，无显式分配与越界检查。

最小可用落地（阶段 1）
- 允许“隐式地址”：把 `DevicePtr` 视为 u64 key，读不到就报错/返回空。
- COPY(H2D) 通过 `AddrSpaceManager::write_global(addr, bytes)` 写入；COPY(D2H) 通过 `read_global` 读回。

完整落地（阶段 2）
- 在 `AddrSpaceManager` 增加：
  - `alloc_global(bytes, align) -> DevicePtr`
  - `free_global(ptr)`（可选）
  - 内部维护 allocation table，用于越界/未分配诊断。

### 1.3 COPY 命令与 CopyEngine 执行
命令来源
- Streaming Runtime 只负责把数据流动表达为 `Command{kind=COPY, payload=CopyPayload}`。

CopyEngine 执行
- H2D：`HostBuffer` → `AddrSpaceManager.global`。
- D2H：`AddrSpaceManager.global` → `HostBuffer`。

观测
- 事件：`copy_submit/copy_complete`，必须携带：`cmd_id/stream_id/kind/bytes`（用于复盘）。

---

## 2) 参数输入：`.param` 打包与绑定（KernelArgs）

### 2.1 Frontend 输出 param layout
目标
- 让 Runtime 获得 entry kernel 的参数布局信息。

现状缺口
- 当前 `KernelImage` 仅包含 `name/reg_count/insts`，没有 `params`。

建议扩展（接口层）
- 在 `include/gpusim/frontend.h` 的 `KernelImage` 增加：
  - `std::vector<ParamDesc> params;`（或等价结构）

Parser/Binder 落地（最小）
- Parser 在解析 `.visible .entry name(...)` 后，读取 entry 参数声明：
  - 形如：`.param .u64 out_ptr, .param .u32 n`
- Binder 负责输出 `ParamDesc{name,type,size,align,offset}`：
  - 阶段 1：按“自然对齐”顺序 layout（align_up）。
  - 阶段 2：若 PTX 提供明确 offset/align，按 PTX 定义布局。

### 2.2 Runtime 参数打包
目标
- 把 host 侧参数值打包成 `KernelArgs{layout, blob}`，并随 KERNEL 命令提交。

建议位置
- `src/runtime/kernel_args_packer.{h,cpp}`。

打包规则
- 小端序写入。
- `PTR64` 参数写入 `DevicePtr`。
- 参数缺失/类型不匹配：报 `Diagnostic{module:"runtime", code:"E_BAD_ARGS"...}`。

### 2.3 `.param` 地址空间绑定
目标
- 执行 `ld.param` / `.param` 读取时能读到本次 launch 的参数 blob。

建议实现路径
- 在 `AddrSpaceManager` 增加 param 相关接口（至少 read）：
```text
bind_param_buffer(kernel_id, bytes)
read_param(kernel_id, offset, size) -> bytes
```
- ComputeEngine 在 `submit_kernel` 时把 `KernelArgs.blob` 绑定到该 kernel_id。

注意
- 阶段 1 可把 param buffer 简化为“kernel context 内的一段 bytes”，由 MemUnit 在遇到 param space 时回调读取。
- 阶段 2 再把 param space 正式并入 Memory 子系统。

---

## 3) 结果输出：显式 D2H + 控制结果（RunResult）

### 3.1 数据结果（显式）
规则
- Runtime 不隐式回读 device 内存。
- 用户/CLI/测试必须显式提交 D2H COPY 才能得到可比对的输出 bytes。

### 3.2 控制结果（RunResult）
建议结构
- 复用现有 `SimResult`（completed/diag/steps），并在 Runtime 层补充：
  - `cmd_id/kernel_id`（若可用）
  - counters snapshot（可选）

同步点
- `stream_synchronize(stream)`/`synchronize()`：
  - 阶段 1：CLI 驱动 tick 直到队列空 + engines idle。
  - 阶段 2：真正的 runtime API 暴露同步函数。

---

## 4) 验收与测试建议

最小端到端用例（推荐优先实现）
- H2D：写入输入 buffer 到 device
- launchKernel：参数 `out_ptr` 指向 device 输出地址，`n` 为长度
- D2H：回读输出 buffer
- host 比对：输出 bytes == expected

Golden/Trace 断言
- trace 中必须出现：cmd_enq → cmd_ready → submit → complete（COPY/KERNEL 都同理）。
- 若出现错误：`Diagnostic` 必须至少携带 `module/code/message`，并尽可能带 `inst_index` 或 cmd_id。
