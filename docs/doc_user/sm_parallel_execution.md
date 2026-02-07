# 多 SM 并行执行（06.02：每 SM 一个宿主线程）

本项目支持一个“吞吐优先”的并行执行基线：当一次 kernel launch / workload 包含多个 CTA（blocks）时，可将不同 CTA 分发到多个 SM worker（宿主线程）并发执行。

参照设计文档
- 抽象设计：`doc_design/modules/06.02_sm_parallel_execution.md`
- 实现落地：`doc_dev/modules/06.02_sm_parallel_execution.md`

---

## 1. 如何启用

在运行配置 JSON 的 `sim` 节点中加入以下字段（缺省值保持向后兼容）：

- `sim.sm_count: int`
  - 含义：SM worker 数量（每个 worker 一个 `std::thread`）。
  - 建议：从 `2` 开始做并行 smoke。

- `sim.parallel: bool`
  - 含义：是否启用并行 CTA 分发与 worker 线程。

- `sim.deterministic: bool`
  - 含义：确定性回归模式。
  - 当前实现（基线）：当为 `true` 时会禁用并行（等价于强制单线程路径），用于稳定回归与 golden traces。

仓库内示例配置：
- `assets/configs/demo_config.json`：默认（串行）
- `assets/configs/demo_parallel_config.json`：示例（并行，`sm_count=2`）

---

## 2. 运行示例

### 2.1 Workload 模式（推荐用于多 CTA）

```bash
./build/gpu-sim-cli \
  --config assets/configs/demo_parallel_config.json \
  --workload assets/workloads/smoke_single_stream.json \
  --trace out/parallel.trace.jsonl \
  --stats out/parallel.stats.json
```

### 2.2 单 kernel 模式

并行是否“可见”，取决于你的 `--grid` 是否产生多个 CTA：

```bash
./build/gpu-sim-cli \
  --config assets/configs/demo_parallel_config.json \
  --ptx assets/ptx/demo_kernel.ptx \
  --grid 4,1,1 \
  --block 32,1,1 \
  --trace out/parallel.trace.jsonl \
  --stats out/parallel.stats.json
```

---

## 3. Trace 行为（`sm_id` 与 `ts`）

### 3.1 `sm_id`

并行模式下，trace events 会填充 `sm_id` 字段，表示该事件来自哪个 SM worker。
- 你可以用它来验证“多个 CTA 确实被分配到不同 SM”。

### 3.2 时间戳 `ts`

并行模式下的 `Event.ts` 使用全局原子逻辑时钟：
- `ts` 只保证全局单调递增；不保证等价于某个“tick/周期”。
- 不同 SM 的事件会交织出现，这是正常现象。

确定性模式（`sim.deterministic=true`）下：
- 当前基线会禁用并行，因此 trace 的事件顺序可稳定复现。

---

## 4. 正确性与限制（当前基线）

- CTA 粒度并行：CTA 会被分发到某个 SM worker，并在该 worker 上完整执行。
- No-cache 内存：不仿真 L1/L2/cache 命中与带宽争用，只按 PTX 地址空间做语义正确的内存访问。
- 并发安全：global/param 内存访问采用 coarse-grain mutex（correctness-first）。

当前未实现（或仍在演进）：
- lock-step deterministic（多 worker 但每 tick 同步的确定性并行）。
- 更细粒度的 CTA 内 warp 交织调度（当前仍以现有执行路径为基线）。

---

## 5. 常见问题

### Q: 我设置了 `sm_count>1`，但看不到并行效果？
- 确认 `sim.parallel=true` 且 `sim.deterministic=false`。
- 确认一次运行实际产生了多个 CTA（例如 `--grid 4,1,1`）。

### Q: 并行模式下 trace 每次不一样，是 bug 吗？
- 不是。并行模式下事件交织与 `ts` 分配顺序会随调度变化。
- 如果需要稳定 trace，用 `sim.deterministic=true`（当前基线会回退到串行）。
