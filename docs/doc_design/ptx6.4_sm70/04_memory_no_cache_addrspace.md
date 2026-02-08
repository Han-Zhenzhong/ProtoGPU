# 04 Memory：no_cache_addrspace（sm70 profile 附录 B/C 对齐）

本设计将 ../../doc_spec/sm70_profile.md 的附录 B（Memory baseline）与附录 C（Determinism baseline）落实为“可观察行为边界”。

相关模块设计：../modules/04_memory.md

## 1. memory.model selector（冻结）

- 基线仅实现：`no_cache_addrspace`。
- selector 校验策略：
  - `allow_unknown_selectors=false`：未知 selector 必须失败并返回 `E_MEMORY_MODEL`。
  - `allow_unknown_selectors=true`：允许回退到基线行为，但必须发出告警诊断（不改变实际内存行为）。

## 2. 地址空间与最小覆盖

bring-up（M1–M4）阶段至少覆盖：
- param：`ld.param.u64/u32 (reg, symbol)`
- global：`ld.global.f32 (reg, addr)`、`st.global.f32 (addr, reg)`

冻结要求
- 必须保留 addrspace 概念：param/global 的访问路径、错误边界与诊断必须可区分。

## 3. 对齐策略（必须显式选择）

规格建议（附录 B.2）允许两种策略：
- 严格模式（推荐用于 bring-up）：未对齐访问 fail-fast。
- 字节模式：以字节访问实现未对齐（需要额外测试覆盖）。

本冻结设计选择：严格模式（bring-up）
- 对 `ld/st`：
  - size=4 要求地址 4 对齐；size=8 要求地址 8 对齐。
  - 不满足 → fail-fast，并返回可定位诊断（module=memory 或 mem_unit）。

（如果未来要切换到字节模式，必须同步更新规格、回归与本设计文档。）

## 4. 越界与未分配地址（强制 fail-fast）

- 对未分配/越界 global 访问：必须 fail-fast。
- 禁止静默返回 0、禁止忽略写入。

诊断要求
- 必须包含：addr、size、space、inst_index、以及触发的 lane 信息（至少 lane_id）。

## 5. 同地址多 lane 冲突（同一 inst 内）

附录 B.4 允许两种策略：确定性覆盖或直接不支持。

本冻结设计选择：确定性覆盖（lane id 从小到大覆盖）
- 对同一 warp、同一条 `st`：若多个 lanes 写同一地址，按 lane_id 升序执行写入。
- 该规则必须写入回归（否则属于未定义）。

## 6. Determinism 与并行（附录 C）

冻结约束
- 当 `sim.deterministic=true`：必须禁用并行 worker（即便 `sim.parallel=true`），或拒绝启动；推荐禁用以便默认回归可跑。

可复现承诺
- deterministic=true：functional 结果必须可复现；trace/stats 尽可能可复现。
- deterministic=false：允许跨 SM 的 trace 事件交错差异；不承诺事件严格顺序一致。
