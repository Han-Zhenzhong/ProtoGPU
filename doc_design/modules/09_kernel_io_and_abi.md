# 09 Kernel I/O + ABI（数据输入 / 参数输入 / 结果输出）

说明
- 本文将“PTX 执行如何被 host 控制”拆成可实现的抽象：**数据输入**、**参数输入**、**结果输出**。
- 设计基准：与 [doc_design/modules/07_runtime_streaming.md](07_runtime_streaming.md)、[doc_design/modules/08_engines.md](08_engines.md)、以及 Memory/SIMT 的契约对齐。
- 若与 [doc_design/sequence.diagram.puml](../sequence.diagram.puml) 或 [doc_design/arch_modules_block.diagram.puml](../arch_modules_block.diagram.puml) 冲突，以图为准。

---

## 09.1 数据输入/输出（Buffers + COPY 语义）

目标
- 让 PTX 执行可以接受“数据输入”（Host → Device）并能把“执行结果”（Device → Host）取回。
- 保持 Streaming Runtime 的核心抽象一致：所有数据流动都通过 `COPY` 命令表达（见 07 模块）。

抽象对象
```text
HostBuffer {
  id: HostBufId
  bytes: byte[]           // 逻辑抽象；实现可为外部指针、文件映射或内部 vector
}

DevicePtr = u64           // global 地址空间指针（模拟器内地址）

BufferRef = oneof {
  host:   { id: HostBufId, offset: u64 }
  device: { ptr: DevicePtr }
}

CopyPayload {
  kind: H2D | D2H | D2D | MEMSET
  dst:  BufferRef
  src:  BufferRef | { imm_byte: u8 }   // MEMSET 使用 imm
  bytes: u64
}
```

语义约束
- H2D：`src=host` 且 `dst=device`。
- D2H：`src=device` 且 `dst=host`。
- D2D：`src=device` 且 `dst=device`。
- MEMSET：`dst=device`。
- COPY 的副作用只作用于 `04 Memory` 的 global memory（基线：No-cache）。

与资源模型的关系
- `DevicePtr` 的分配策略可以有两种基线实现：
  - 简化实现：由 Memory 子系统按需“隐式分配”（用于 demo/早期 bring-up）。
  - 完整实现：由 Runtime 提供 `malloc/free`（或 arena）API，并在 COPY/KERNEL 前做地址合法性检查。

错误处理（诊断）
- 越界、未分配地址、对齐违规等：由 Memory Subsystem 报 `Diagnostic{module:"memory", ...}`。
- Runtime/Streaming 负责把错误关联到 `cmd_id/stream_id`（便于 trace 复盘），并停止后续提交。

---

## 09.2 参数输入（Kernel .param 打包与绑定）

目标
- `KERNEL` 命令必须携带一份**可重放**的参数 blob。
- ComputeEngine/SIMT 在执行时可通过 `.param` 地址空间读取该 blob（例如 `ld.param`）。

关键点
- `.param` 语义上是只读的“参数缓冲区”。
- 其布局由 Frontend（Parser/Binder）解析 `.entry` 的形参声明获得。
- Runtime 负责将 host 侧参数值按布局打包，并绑定到本次 kernel launch。

数据结构（概念）
```text
ParamType = U8|U16|U32|U64|S32|S64|F32|F64|PTR64|...

ParamDesc { name: string, type: ParamType, size: u32, align: u32, offset: u32 }

KernelArgs {
  layout: vector<ParamDesc>
  blob:   byte[]            // 按 layout.offset 写入，小端序
}

KernelPayload {
  module, entry
  grid_dim:  dim3
  block_dim: dim3
  params:    KernelArgs
  shared_bytes?: u32
}
```

打包规则（基线）
- 若 Frontend 能提供每个参数的 `offset/align/size`：Runtime 必须按该布局写入。
- 若 Frontend 仅提供 `type/size/align`：Runtime 使用“自然对齐”顺序布局（`offset = align_up(prev_end, align)`）。
- 指针参数统一视为 `PTR64`（64-bit little-endian），其值是 `DevicePtr`（global 地址）。

与 Memory 的绑定
- 每次 `launch_kernel` 生成一个只读 param buffer：
  - 实现选项 A：在 Memory 子系统中建立一个 param-space 对象（与 global/shared/local/const 并列）。
  - 实现选项 B：将 param buffer 放在 global memory 中但标记为 read-only（便于早期实现）。
- 执行时，`.param` 地址空间的读取必须映射到该 buffer。

---

## 09.3 PTX 执行结果输出（Result + 同步点）

“结果”定义
- **显式结果**：用户通过 D2H COPY 将 device global memory 的指定范围拷回 host buffer，作为 kernel 计算结果。
- **控制结果**：kernel 是否完成、是否触发诊断错误、执行步数与可选 counters/trace。

同步点
- `synchronize()` / `stream_synchronize(stream)`：阻塞直到相关 stream 的队列清空且所有已提交命令完成；然后返回/可查询执行状态。

返回对象（概念）
```text
RunResult {
  completed: bool
  diag?: Diagnostic
  steps: u64
  counters?: map<string, u64>
}
```

约束
- Runtime 不隐式“回读”任何 device 内存：只有显式 D2H 才会产生 host 可见的数据输出。
- 若需要自动化验证（tests/golden）：推荐通过 D2H 把输出回读到 host 后对比期望值；同时保留 trace/stats 用于定位。

---

## 09.4 端到端示例（概念序列）

```text
host:
  hb_in  = host_alloc(bytes)
  hb_out = host_alloc(bytes)
  dev_in  = device_malloc(bytes)
  dev_out = device_malloc(bytes)

  memcpy_async(stream0, dev_in, hb_in, bytes, H2D)
  launch_kernel(stream0, module, entry,
                grid, block,
                params={ out_ptr=dev_out, n=N })
  memcpy_async(stream0, hb_out, dev_out, bytes, D2H)
  stream_synchronize(stream0)

  assert(hb_out == expected)
```

验收要点
- Stream 内顺序保证 COPY(H2D) → KERNEL → COPY(D2H) 的可复盘因果链。
- trace 中能看到：cmd_enq/ready/submit/complete 与 kernel_complete。
- 若参数布局不匹配或地址非法，应有清晰诊断并可定位到具体 cmd/kernel。
