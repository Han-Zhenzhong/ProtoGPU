# 模块化 HW/SW Mapping（10：架构 profile 与可替换组件）

本项目支持把“硬件块图中的关键可替换点”落实为可配置的仿真软件模块（interface + factory + 配置选择），从而仅通过修改配置文件即可组合出不同“目标架构”。

参照文档（设计/实现）
- 抽象设计：`docs/doc_design/modules/10_modular_hw_sw_mapping.md`
- 实现落地：`docs/doc_dev/modules/10_modular_hw_sw_mapping.md`

---

## 1. 你能配置什么（当前版本）

当前已模块化并可通过配置选择的部件：

1) CTA 分发（Global CTA Scheduler）
- 选择键：`sim.cta_scheduler` 或 `arch.components.cta_scheduler`

2) Warp 调度（Warp Scheduler）
- 选择键：`sim.warp_scheduler` 或 `arch.components.warp_scheduler`

3) MemoryModel（No-cache 折叠，但保留替换出口）
- 选择键：`memory.model` 或 `arch.components.memory_model`

说明
- 目前内存模型只实现了 “no-cache + 地址空间” 基线：`no_cache_addrspace`。
- 未来可新增更多实现，但 `MemUnit -> IMemoryModel` 的依赖面保持稳定。

---

## 2. 配置结构与优先级

### 2.1 直接 selectors（兼容写法）

在 config 的 `sim` 节点中使用：

- `sim.cta_scheduler: string`（默认 `fifo`）
- `sim.warp_scheduler: string`（默认 `in_order_run_to_completion`）
- `sim.allow_unknown_selectors: bool`（默认 `false`）

以及在 `memory` 节点中使用：

- `memory.model: string`（默认 `no_cache_addrspace`）

### 2.2 `arch.profile` + `arch.components`（推荐写法）

推荐用一个 profile 表达“目标架构”，并用 components 覆盖具体实现：

- `arch.profile: string`
  - 当前提供：`baseline_no_cache`

- `arch.components: object`
  - `cta_scheduler: string`
  - `warp_scheduler: string`
  - `memory_model: string`

优先级（从高到低）
1) `arch.components.*`
2) `arch.profile` 设定的默认值（如果该 profile 已知）
3) `sim.*` selectors / `memory.model`
4) `SimConfig` 内置默认值

---

## 3. 支持的取值（当前实现）

### 3.1 CTA scheduler（`cta_scheduler`）

- `fifo`
  - 行为：全局 FIFO 队列，SM workers 从同一个队列抢占 CTA。

- `sm_round_robin`
  - 行为：确定性分配，CTA i 固定分配到 SM `(i % sm_count)`。
  - 目的：让“策略差异”在 trace 中更容易被观察到（非 stealing）。

### 3.2 Warp scheduler（`warp_scheduler`）

- `in_order_run_to_completion`
  - 行为：复现旧基线；一个 warp 完成后再推进下一个 warp。

- `round_robin_interleave_step`
  - 行为：真 RR：每次只推进一个 warp 的一步（step），在多个 warps 之间交错。

### 3.3 Memory model（`memory.model` / `memory_model`）

- `no_cache_addrspace`
  - 行为：不仿真 cache，只按 PTX 地址空间分类（global/param 等）提供语义正确的访问。

---

## 4. unknown selector 的行为（严格/宽容）

默认严格（推荐）
- 当选择器值未知时，会返回 `Diagnostic` 并中止运行。

宽容模式（用于 bring-up/兼容）
- 设置：`sim.allow_unknown_selectors: true`
- 行为：unknown selector 会回退到基线实现：
  - CTA scheduler → `fifo`
  - Warp scheduler → `in_order_run_to_completion`
  - Memory model：当前仍只支持 `no_cache_addrspace`；unknown 会被忽略（但不会改变实际内存行为）。

---

## 5. 示例：用 demo 配置跑一遍（并验证选择生效）

仓库内置演示配置：
- `assets/configs/demo_modular_selectors.json`

它做了什么
- 开启 2 SM 并行执行（见 06.02 并行执行文档）
- 选择：
  - `cta_scheduler=sm_round_robin`
  - `warp_scheduler=round_robin_interleave_step`
  - `memory_model=no_cache_addrspace`

### 5.1 运行

```bash
./build/gpu-sim-cli \
  --config assets/configs/demo_modular_selectors.json \
  --ptx assets/ptx/demo_kernel.ptx \
  --ptx-isa assets/ptx_isa/demo_ptx8.json \
  --inst-desc assets/inst_desc/demo_desc.json \
  --grid 4,1,1 \
  --block 32,1,1 \
  --trace out/modsel.trace.jsonl \
  --stats out/modsel.stats.json
```

### 5.2 验证（Trace：RUN_START config_summary）

trace 中会写入一次性的 `RUN_START` 事件，`extra` 字段携带 `config_summary`。

最小检查（示例）：

```bash
grep -F '"action":"RUN_START"' out/modsel.trace.jsonl
grep -F 'sm_round_robin' out/modsel.trace.jsonl
grep -F 'round_robin_interleave_step' out/modsel.trace.jsonl
```

也可以直接跑集成测试脚本，它会自动检查这些条件：
- Windows：`scripts\\run_integration_tests.bat build`
- Bash：`bash scripts/run_integration_tests.sh build`

---

## 6. 与多 SM 并行/确定性模式的关系

- 当 `sim.parallel=true && sim.deterministic=false && sim.sm_count>1` 时启用并行 worker。
- 当 `sim.deterministic=true` 时，当前基线会禁用并行（回退到单线程路径），用于稳定回归与 golden traces。

参照：`docs/doc_user/sm_parallel_execution.md`
