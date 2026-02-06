## Plan: Kernel I/O + ABI 开发计划

按 09 的 design+dev-doc，把“host 控制 PTX 执行”的三件事落地：数据输入/输出（H2D/D2H）、参数输入（`.param` 打包/绑定）、结果输出（显式 D2H + 控制结果 + 同步点）。优先走最小可用路径：先让 H2D/D2H 跑通，再补齐 kernel args/layout，再让 kernel 能把结果写回 global memory，最后把这一切串进单 stream 的 enqueue/tick/synchronize 模型，逐步对齐 07/08 的 streaming 设计而不一次性重构全 runtime。

### Steps 5
1. 定义最小公共类型与 API 轮廓：在 [include/gpusim/runtime.h](include/gpusim/runtime.h) 增补 `HostBufId`/`DevicePtr`/`KernelArgs` 等，并明确 `launch_kernel`、`memcpy_async`、`stream_synchronize` 的最小签名。
2. 先落地 HostBuffer + global 分配 + COPY(H2D/D2H)：新增 runtime 内部 HostBuffer 存储与 `AddrSpaceManager::alloc_global`，用 [include/gpusim/memory.h](include/gpusim/memory.h) 与 [src/memory/memory.cpp](src/memory/memory.cpp) 实现可诊断的读写/越界策略。
3. 让 Frontend 输出 param layout：扩展 [include/gpusim/frontend.h](include/gpusim/frontend.h) 的 `KernelImage` 增加 `params`，并在 [src/frontend/parser.cpp](src/frontend/parser.cpp) 解析 `.entry (...)` 中的 `.param` 声明，按自然对齐生成 `offset/align/size`。
4. 实现 args 打包与 `.param` 绑定，并打通最小内存指令：新增 `kernel_args_packer`，在 runtime/compute path 绑定 param blob；补齐 `st.global`/`ld.param` 的最小 micro-op 与 MemUnit 执行路径（涉及 [src/units/mem_unit.cpp](src/units/mem_unit.cpp)、指令描述/expander、以及地址操作数解析）。
5. 引入单 stream 的 CommandQueue + tick + synchronize：在 [src/runtime/](src/runtime/) 以最小方式落地 `Command/COPY/KERNEL`、队列、以及 `stream_synchronize()`；在 [src/apps/cli/main.cpp](src/apps/cli/main.cpp) 驱动端到端序列：H2D → launchKernel(params) → D2H → sync，并输出 trace/stats。

### Further Considerations 3
1. `.param` 存放位置：并入 Memory param-space。
2. DevicePtr 分配策略：bump allocator + allocation table。
3. lane 语义的取舍：直接实现按 lane_mask 的并行读写。

