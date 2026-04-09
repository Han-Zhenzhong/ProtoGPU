## Plan: 可组合架构模块化 Dev Plan

基于设计文档与实现级 dev doc，把“硬件块↔软件模块映射”落成可执行的开发拆解：先固化配置选择入口（selectors）与接口/工厂（即使只有 1 个实现），再逐步把 CTA 分发、warp issue、memory model、observability sink 从内联逻辑中抽出来并可注入；同时补齐 trace 的“组件组合可见性”和最小回归测试，保证组合切换可验证且不破坏既有配置兼容性。

### Steps
1. 新增 dev plan 文件 `docs/doc_plan/plan_dev/plan-modularHwSwMapping.prompt.md`，引用 `docs/doc_design/modules/10_modular_hw_sw_mapping.md` 与 `docs/doc_dev/modules/10_modular_hw_sw_mapping.md`。
2. 定义配置 selectors 与兼容策略：扩展 `SimConfig`（见 `include/gpusim/simt.h`）并在 `src/runtime/runtime.cpp` 解析 `sim.cta_scheduler/sim.warp_scheduler/memory.model`，缺省映射为当前行为。
3. 抽出 `ICtaScheduler` + factory：将 CTA 队列语义从 `src/simt/simt.cpp` 拆为可替换实现，默认 `fifo`，并由 Runtime/Engine 侧注入到 SIMT 执行入口。
4. 引入 `IWarpScheduler` + baseline 实现：先提供“保持现状”的 `in_order_run_to_completion`，再加一个第二实现（可简化但必须可选），并把选择点从 `SimtExecutor` 内联逻辑替换为接口调用。
5. 抽象 `IMemoryModel`（适配器优先）：在 `include/gpusim/memory.h` 定义接口，用适配器封装现有 `AddrSpaceManager`（实现仍在 `src/memory/memory.cpp`），并让 `MemUnit` 只依赖接口（见 `src/units/mem_unit.cpp`）。
6. 让“组合可见且可验收”：在 `src/observability/observability.cpp` 增加一次性 `run_start/config_summary` 事件（包含 selectors 与 `sm_count/parallel/deterministic`），并补齐 unit+integration smoke（参照 `tests/unit/config_parse_tests.cpp` 与 scripts 下并行 smoke）验证“配置确实生效”。

### Further Considerations
1. WarpScheduler 第二实现范围：“真 RR 交错 step”。
2. 配置命名空间：同步引入 `arch.profile`（更清晰但改动更广）。
3. 选择失败策略：unknown selector 是 hard error（Diagnostic）,兼顾宽容性，加一个显式开关：sim.allow_unknown_selectors: true，默认 false（即默认严格模式）。
