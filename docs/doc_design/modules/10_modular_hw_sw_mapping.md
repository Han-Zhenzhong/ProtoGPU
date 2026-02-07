# 10 硬件模块 ↔ 仿真软件模块映射（可组合架构）

说明
- 本文将 [doc_spec/gpu_block_diagram.puml](../doc_spec/gpu_block_diagram.puml) 的“硬件块图”与 `src/` 现有软件模块对齐，并定义 **哪些硬件块必须有可替换的软件模块**，哪些块可在 No-cache 约束下 **折叠为抽象实现**。
- 本文的目标是支撑需求：
  - “对照每个硬件模块有对应的仿真软件模块（便于做出可配置不同模块组合成新硬件架构）”。
  - 约束：**cache 结构不用仿**，内存按 PTX 需要的分类（global/shared/local/const/param）建模。
- 设计基准：与 [doc_design/arch_modules_block.diagram.puml](../arch_modules_block.diagram.puml) 对齐；若文本与图冲突，以图为准。

相关文档
- Contracts（跨模块契约）：[doc_design/modules/00_contracts.md](00_contracts.md)
- Memory（No-cache + AddrSpace）：[doc_design/modules/04_memory.md](04_memory.md)
- Units（Exec/Control/Mem）：[doc_design/modules/05_units.md](05_units.md)
- SIMT Core（step + schedulers）：[doc_design/modules/06_simt_core.md](06_simt_core.md)
- 多 SM 并行（每 SM 一个线程）：[doc_design/modules/06.02_sm_parallel_execution.md](06.02_sm_parallel_execution.md)
- Runtime/Streaming/Engines：[doc_design/modules/07_runtime_streaming.md](07_runtime_streaming.md)、[doc_design/modules/08_engines.md](08_engines.md)

---

## 目标（Goals）
- 定义“模块一一对应”的**可执行解释**：
  - 对于需要可替换/可配置的硬件部件：必须有明确的软件模块（interface + 可选实现 + 配置选择）。
  - 对于不在范围内的硬件微结构（cache/NoC/VA 时序等）：允许折叠成抽象层，保证 PTX/SIMT 语义正确。
- 让“组合成新硬件架构”变成工程能力：
  - 通过配置选择不同实现/策略来组成一个“目标架构 profile”。
  - 不需要改 SIMT Executor 的核心语义（contracts 不变）。

## 非目标（Non-goals）
- 不要求每个块图方框都对应一个独立的 C++ class/namespace（避免空壳模块与过度设计）。
- 不做 cycle-accurate、cache 命中/替换、NoC 拥塞等时序精确建模。

---

## 核心原则（Principles）

### P1. “硬件块”与“软件模块”允许多对一
在 No-cache 约束下，块图中的 `NoC/L1/L2/DRAM/VA` 这类实现手段可以折叠为一个 `MemoryModel` 抽象模块。

### P2. 只有“需要被替换”的部件才必须模块化
下列类型必须可替换（否则无法称为“可组合新架构”）：
- **调度策略**：CTA 分发（Global Scheduler）、warp issue（SM Scheduler）。
- **执行语义扩展点**：例如 fence/atomic 的语义强弱或实现策略（保守正确 vs 更细粒度）。
- **并行执行模式**：单线程/多 SM worker、确定性模式。

### P3. 跨模块依赖面必须稳定
所有可替换模块必须仅依赖 contracts 中定义的结构与接口（`InstRecord/MicroOp/StepResult/MemResult/Event/Diagnostic`）。

---

## 硬件块图 → 软件模块映射（Mapping）

> 下面以块图术语为主，给出对应的软件模块/抽象与当前实现归属。

### Host / Driver Side
- Host App：不在仓库范围（外部驱动）。
- CUDA-like Runtime API：`src/apps/cli` + `src/runtime`（Host 侧调用的抽象入口）。
- Module Loader（PTX load/link）：当前折叠到 Runtime + Frontend；可后续独立成 loader（非必需）。
- PTX Frontend（parse→IR0）：`src/frontend`（Parser/Binder），输出 Module/Function/InstList。

### Device Side（顶层）
- Runtime (Device)：`src/runtime` + `src/runtime/engines.cpp`（调度/资源实例化/trace/errors）。
- Global Scheduler（CTA Distributor）：
  - 设计上属于 SIMT/Engines 边界（见 06.02）。
  - 必须模块化（可替换策略），即便当前先实现 FIFO。
- Interconnect/NoC：在 No-cache 约束下折叠到 MemoryModel（不单独建模）。

### SM Cluster
- SM Scheduler（warp scheduling）：`WarpScheduler`（见 06 SIMT Core），必须可替换（RR/公平/最老优先等）。
- SIMT Core：`Executor + Contexts + Reconvergence`（见 06 SIMT Core）。
- Reg File：折叠到 `ThreadContext`（lane-wise regs/preds，属于 SIMT Core 的数据结构）。
- Shared Memory / Scratchpad：`SharedMemory`（见 04 Memory），CTA 私有。
- Load/Store Unit：`MemUnit`（见 05 Units）+ `AddrSpaceManager`（见 04 Memory）。
- SFU/Tensor：可选执行单元（未来扩展点）；当前不强制实现。

### Sync & Consistency
- Barrier Manager：CTA 级 barrier 状态属于 CTAContext + Memory（见 06 SIMT Core、04 Memory）。
- Atomics Unit：在 No-cache 下实现为 MemoryModel 的 atomic path（见 04 Memory 的 atomic 接口）。
- Fence / Memory Model：
  - “MemoryModel”本身是可替换模块。
  - fence 的语义与 scope 可配置，但不要求时序精确。

### Memory Subsystem（No-cache 折叠）
- L2/DRAM/VA：折叠为 `GlobalMemory + AddrSpaceManager` 抽象；只保证寻址/越界/对齐与可见性语义。
- Const Cache：折叠为 ConstMemory（只读）。
- Texture/Sampler：不在必需范围（optional）。

---

## “模块可组合”的软件结构要求（Software Architecture Requirements）

### 1) 每个可替换模块都必须有 Interface + Factory
概念接口（示意，仅用于设计约束）：

```text
ICtaScheduler:
  submit(cta_work_item)
  try_acquire(sm_id) -> optional<cta_work_item>

IWarpScheduler:
  pick_ready_warp(cta_ctx) -> optional<warp_id>

IMemoryModel:
  read/write/atomic/fence/barrier_sync(...)

IObsSink (optional):
  emit(Event)
```

要求
- 实现通过 **registry/factory** 创建：`create("fifo") / create("round_robin")`。
- Runtime/Engine 只依赖接口，不依赖具体实现类型。

### 2) 配置必须能表达“一个目标架构 profile”
建议配置以“profile + components”表达：

```text
arch.profile: "baseline_no_cache"
arch.sm_count: 4
arch.parallel: true
arch.deterministic: false

arch.global_scheduler: "fifo"
arch.warp_scheduler: "round_robin"
arch.memory_model: "no_cache_addrspace"

arch.fence_mode: "conservative"
arch.atomic_mode: "serialize_by_address"
```

约束
- 同一 profile 的组合必须可复现（至少在 deterministic 模式下）。
- profile 的变更不得要求改动 SIMT Core 的 contracts。

### 3) 折叠模块必须明确边界与可替换出口
例如：不仿 cache ≠ 不允许未来加入 cache。
- 现在：`IMemoryModel = NoCacheAddrSpaceModel`。
- 未来：可以新增 `SimpleCacheModel`，但不改变 `MemUnit -> IMemoryModel` 的依赖面。

---

## 观测与可调试性（Observability Requirements）
为了让“组合不同模块”可调试，必须满足：
- Trace/Event 中必须包含：`profile/components/sm_id` 等信息（至少在启动与 kernel_submit 事件中）。
- 多 SM 并行时，`ObsControl.emit` 线程安全。
- 建议提供 `deterministic=true` 的回归模式，保证 golden traces 可维护。

---

## 验收标准（Acceptance Criteria）

### A1. 可组合（配置驱动）
- 仅通过配置切换以下至少两项而无需改代码路径：
  - `arch.global_scheduler`（至少 FIFO 与另一个策略的占位实现）。
  - `arch.warp_scheduler`（至少 round-robin 与另一个策略的占位实现）。

### A2. 内存分类正确（No-cache）
- MemoryModel 必须按 PTX 地址空间路由：global/shared/local/const/param（见 04 Memory）。
- 不要求 cache 命中/延迟建模。

### A3. 多 core 并行
- `arch.sm_count > 1 && arch.parallel=true` 时：启动多个 SMWorker，并在 Trace 中能看到不同 `sm_id` 的 CTA/warp 执行事件（见 06.02）。

### A4. 可回归
- `arch.deterministic=true` 时：运行结果与 trace 在相同输入下可稳定复现（基线允许通过禁用并行来实现）。

---

## 迁移建议（How to evolve）
- 先把“可替换点”做成接口与工厂（即便只有一个实现），再做第二实现；否则很难保证边界稳定。
- 优先模块化调度与并行模式；MemoryModel 维持 No-cache 基线，同时保留替换接口。
- 每新增一个可替换模块，实现应当配套：
  - 配置项
  - trace 标识
  - 最小 smoke test（验证该模块真的被选中并生效）
