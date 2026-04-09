# 10 硬件模块 ↔ 仿真软件模块映射（实现级：可组合架构的接口与落点）

参照
- 抽象设计：`doc_design/modules/10_modular_hw_sw_mapping.md`
- 模块图：`doc_design/arch_modules_block.diagram.puml`
- Contracts：`doc_dev/modules/00_contracts.md`
- Memory（No-cache）：`doc_dev/modules/04_memory.md`
- SIMT Core：`doc_dev/modules/06_simt_core.md`
- 多 SM 并行：`doc_dev/modules/06.02_sm_parallel_execution.md`
- Runtime/Streaming：`doc_dev/modules/07_runtime_streaming.md`

落地目标
- 把“硬件块 → 软件模块”的映射从设计描述落到**可执行的工程约束**：
  - 哪些部件必须可替换（接口 + 工厂 + 配置选择）。
  - 哪些部件在 No-cache 约束下允许折叠成抽象实现（但仍要有可替换出口）。
- 明确当前代码结构与未来可替换点之间的差距，并给出**具体落点（文件/类型/依赖方向）**。

---

## 1. 当前代码结构（Reality Check）

本仓库当前的“最小可运行路径”大致为：
- CLI：`src/apps/cli/main.cpp`
- Runtime：`src/runtime/runtime.cpp`（加载 config，驱动单 kernel 或 workload）
- SIMT 执行：`src/simt/simt.cpp` 内 `SimtExecutor::run()`（枚举 CTA/warp 并执行）
- Memory：`src/memory/memory.cpp` 的 `AddrSpaceManager`（global/param；并行下 coarse mutex correctness-first）
- Observability：`src/observability/observability.cpp`（trace jsonl/stats json）

与“可组合架构”的关系：
- “模块边界”目前以目录为主（`src/runtime|simt|memory|units|frontend|instruction|observability`）。
- 但“可替换策略（scheduler/memory/parallel mode）”尚未通过接口/工厂完全显式化；部分策略逻辑仍内联在 `SimtExecutor::run()`。

本 dev doc 的核心，是把设计文档中要求的可替换点，变成代码层面的**明确接口 + 可注入实现**。

---

## 2. 必须模块化（可替换）的部件清单

按 `doc_design/modules/10_modular_hw_sw_mapping.md` 的 P2 原则，以下必须可替换：

### 2.1 CTA 分发（Global Scheduler）
职责
- 接收 kernel 的 CTA work items，并将 CTA 分配给 SM workers。

实现落点建议
- 接口：`include/gpusim/runtime.h` 或新增 `include/gpusim/scheduler.h`（更清晰）
- 实现：`src/runtime/`（因为它属于 Runtime/Engine 层，而不是 SIMT 内核语义本身）

建议接口（实现级约束，不要求一次到位）：
```text
struct CtaWorkItem { kernel_id, cta_id, grid/block, params_handle, ... }

class ICtaScheduler {
  submit(vector<CtaWorkItem>)
  acquire(SmId sm_id) -> optional<CtaWorkItem>   // 阻塞/非阻塞均可，但必须定义 stop 协议
  request_stop() -> void
}
```

### 2.2 Warp issue（Warp Scheduler）
职责
- 在一个 SM（或一个 CTA）内部选择下一个 ready warp。

实现落点建议
- 接口：`include/gpusim/simt.h`（或新增 `include/gpusim/simt_scheduler.h`）
- 实现：`src/simt/`

建议接口：
```text
class IWarpScheduler {
  on_cta_start(cta_id, warp_ids...)
  pick_ready_warp(cta_ctx) -> optional<WarpId>
}
```

要求
- 只依赖 contracts（WarpState/CTAContext/blocked_reason），不得依赖具体 Units/Memory 的内部实现。

### 2.3 MemoryModel（No-cache 折叠，但必须可替换）
职责
- 提供对 PTX 地址空间（global/shared/local/const/param）的语义正确访问。

现状
- 当前 `AddrSpaceManager` 仍是“具体实现 + 部分地址空间”。

落地约束
- 对 Units（尤其是 MemUnit）暴露的依赖面必须稳定：优先通过一个“内存接口”完成。

实现落点建议
- 接口：`include/gpusim/memory.h` 内抽象出 `IMemoryModel`，或新增 `include/gpusim/memory_model.h`
- 实现：`src/memory/`

建议接口（与现有 04 Memory dev doc 对齐）：
```text
class IMemoryModel {
  alloc_global(bytes, align) -> DevicePtr
  read_global(addr, size) -> bytes | nullopt
  write_global(addr, bytes) -> void
  set_param_layout(layout)
  set_param_blob(blob)
  read_param_symbol(name, size) -> bytes | nullopt
  // 后续：shared/local/const/atomic/fence
}
```

### 2.4 并行执行模式（SM worker / deterministic）
职责
- 决定是否启用 `sm_count` worker 并行执行，以及 deterministic 回归策略。

现状
- `SimConfig` 已支持 `sm_count/parallel/deterministic`。
- 基线：`deterministic=true` 禁用并行。

落地约束
- 并行模式切换应位于 Runtime/Engine 的“编排层”，而不是埋在 Units 内。
- deterministic 策略需要对测试友好：CI 能强制稳定（允许通过禁并行实现基线）。

### 2.5 Observability sink（可选但推荐）
目标
- 允许替换 trace 的落地方式（jsonl、内存 ring buffer、采样过滤、未来可视化），而不影响执行语义。

落点建议
- `ObsControl` 保持为默认实现；额外抽出一个 `IObsSink`（可选）用于将 `Event` 送往不同后端。

---

## 3. “折叠模块”的实现边界与替换出口

设计允许将 cache/NoC/DRAM/VA 等微结构折叠为抽象 MemoryModel，但必须满足：
- 对外接口仍清晰：Units 只能通过内存接口访问 global/shared/param 等。
- 未来引入 `SimpleCacheModel` 时，不改变 MemUnit 的调用点（只替换工厂创建出来的实现）。

工程约束（建议写进 code review checklist）：
- 禁止绕过 MemoryModel 直接读写内部容器（例如直接触碰 `global_` map）。
- fence/atomic 的语义边界必须能在 trace 中复盘（即使是保守实现）。

---

## 4. 配置（Configuration）如何表达“架构 profile”

现状
- 运行配置结构为：
  - `sim.*`
  - `observability.*`

建议（阶段性演进）

### 4.1 Phase A（保持兼容，先把可替换点显式化）
- 继续使用 `sim.sm_count/sim.parallel/sim.deterministic`。
- 新增（可选）字符串选择器（即使只有一个实现，也先把“选择入口”固定下来）：
```text
sim.cta_scheduler: "fifo"
sim.warp_scheduler: "round_robin"
memory.model: "no_cache_addrspace"
```

### 4.2 Phase B（引入 `arch.profile`）
当可替换实现超过 1 个后，引入更清晰的 profile：
```text
arch.profile: "baseline_no_cache"
arch.components: {
  cta_scheduler: "fifo",
  warp_scheduler: "round_robin",
  memory_model: "no_cache_addrspace"
}
```

约束
- profile 的选择必须能在 trace/启动信息中被观察到（便于定位“到底跑的是哪个组合”）。

---

## 5. Trace/调试要求（让“组合”可验证）

最低要求
- 并行执行时：事件必须携带 `sm_id`（当前已支持，见 `src/observability/observability.cpp`）。

建议增强（后续）
- 在 trace 中增加一次性的“run_start/config_summary”事件：
  - profile/components 名称
  - `sm_count/parallel/deterministic`
- 对 scheduler 的关键动作打点：
  - `cta_acquire/cta_done`（带 `cta_id/sm_id`）

---

## 6. 验收与测试（与可替换点绑定）

建议将测试按“接口契约”组织：
- Unit tests
  - 配置解析与默认值：已由 `gpu-sim-config-parse-tests` 覆盖（见 `tests/unit/config_parse_tests.cpp`）
  -（后续）WarpScheduler 策略：给定阻塞/完成状态的 pick 结果
- Integration smoke
  - 并行配置跑通并确认 trace 含 `"sm_id":`（见 scripts 集成测试的并行 smoke）
  - deterministic=true 时 trace 稳定（当前基线通过禁并行实现）

---

## 7. 迁移顺序（把“映射”变成工程能力）

推荐顺序（最小风险）：
1) 先把“选择入口”固定（接口 + 工厂），实现只有 1 个也没问题。
2) 把 `SimtExecutor::run()` 里内联的策略逻辑外提到 Runtime/Engine（CTA 分发、线程模型）。
3) MemoryModel 抽象化：MemUnit 只依赖接口，具体实现留在 `src/memory/`。
4) 增加第二个策略实现（例如 warp scheduler 的另一个策略），用测试验证“配置确实生效”。
