# sm70 profile（基线）

本文档定义 ProtoGPU 当前阶段“硬件基线 profile”的可验证条目，用于：
- 约束实现目标（避免隐式假设漂移）
- 作为回归测试与资产（PTX/ptx_isa/inst_desc）的对齐依据
- 为后续扩展到其它 `sm_xx` 提供差异承载点

> 说明：这里的 “sm70 profile” 是 ProtoGPU 的工程基线配置与语义边界，并不声称是对真实 NVIDIA Volta（SM70）cycle-accurate 的完整复刻。

---

## Profile Summary

| 项 | 基线值 | 证据/落点 |
|---|---:|---|
| 架构基线 | `sm_70` | demo PTX `.target sm_70`（见 `assets/ptx/*.ptx`） |
| PTX address size | 64-bit | demo PTX `.address_size 64`（见 `assets/ptx/*.ptx`） |
| warp size | 32 | `SimConfig.warp_size` 默认 32（见 `include/gpusim/simt.h`）；demo config 也为 32（见 `assets/configs/demo_config.json`） |
| 并行模式 | 每 SM 一个宿主线程（可选） | `SimtExecutor` 根据 `sim.parallel/sm_count/deterministic` 决定是否并行（见 `src/simt/simt.cpp`） |
| 内存模型选择器 | `no_cache_addrspace`（唯一实现） | selector 校验只接受该值（见 `src/simt/simt.cpp`，以及 `include/gpusim/simt.h` 默认值） |

---

## Warp / Lane 语义边界

### Warp 大小
- 基线 warp 大小为 32。
- 实现约束：运行时校验 `warp_size` 必须在 `[1,32]`（见 `src/simt/simt.cpp`）。

### 部分 warp（partial warp）
- 支持 blockDim 不是 warp_size 整数倍的情况；通过 `active mask` 控制 lane 是否参与执行。
- 回归证据：`tests/builtins_tests.cpp` 包含 partial warp 下 `%tid.x` 行为的验证用例。

---

## Builtin（最小集合）

在本项目中 builtin 以一种 `special` 操作数形式出现（例如 token `%tid.x` → `special=="tid.x"`），要求：
- 前端能将 `%tid.x` 等 token 识别/规范化为 `Special(tid.x)`
- PTX ISA map 包含对应的 form（例如 `mov.u32 (reg, special)`）
- 执行器能在 lane-wise 语义下产生正确值

### 最小支持集合（基线）
- `%tid.{x,y,z}`、`%ntid.{x,y,z}`
- `%ctaid.{x,y,z}`、`%nctaid.{x,y,z}`
- `%laneid`、`%warpid`

说明与验证：
- 用户文档列出了该集合（见 `docs/doc_user/cli.md`）。
- `tests/builtins_tests.cpp` 对 `tid.x` 给出可重复的语义验证（3D block inversion、partial warp）。

---

## 内存模型边界（no_cache_addrspace）

### 目标
该基线的定位是：
- **无 cache**（不模拟 L1/L2、tag、替换策略、写回等）
- **保留 PTX 的地址空间概念**（至少能区分不同 addrspace 的访问路径/语义边界）

### 当前实现约束（必须明确写死的边界）
- `memory.model` selector 目前仅实现 `no_cache_addrspace`：
  - 当 selector 非该值且 `allow_unknown_selectors=false` 时，运行应失败并返回 `E_MEMORY_MODEL`。
  - 当 `allow_unknown_selectors=true` 时，未知 selector 允许回退到基线行为（不改变实际内存行为）。

> 进一步的内存一致性、fence、原子、scope/ordering 等语义，在引入新 memory_model 之前都不应“默默假定存在”。

---

## 面向 tiny GPT-2 的差距检查（硬件语义视角）

如果目标是跑通 tiny GPT-2（FP32 推理），在“sm70 profile（功能级）”维持不变的前提下，需要明确以下两类工作：

对应的 PTX→IR→uops 覆盖矩阵见：`docs/doc_spec/tiny_gpt2_minimal_coverage_matrix.md`。

### 1) 必须实现的功能语义
- FP32 数据通路：至少支持 lane-wise 的 FP32 加/乘/融合乘加（对应 ExecCore/MicroOp 扩展）
- 全局内存读：`ld.global.*` 需要可用（当前全局 store 可用，但全局 load 还需要 `ptx_isa` form 与执行路径对齐）

### 2) 控制流/谓词：二选一的路线
- 路线 A（更通用）：实现 `setp` / `bra` / predication，并补齐 divergence/reconvergence（warp 级控制流语义）
- 路线 B（更简单但限制多）：约束 GPT-2 kernel 不使用分支/谓词（或通过 padding/launch 配置消除边界分支），把“循环”尽量通过展开或多 kernel 调度替代

建议：先选定路线（A 或 B）并写入计划与回归用例，否则“能否跑通 GPT2”会被隐藏在 kernel 编译策略里难以稳定复现。

---

## 附录 A：Control-flow baseline（bring-up：M4）

本附录用于约束 bring-up 阶段的控制流设计与实现口径（功能级，不追求 cycle-accurate）。

### A.1 Uniform-only（强制约束）

- 基线仅支持 **uniform control-flow**：同一 warp 内所有 active lanes 对分支条件必须一致。
- 允许 predicated branch 形态（`@%p` / `@!%p`），但必须满足 uniform-only。
- 若检测到 divergence（同 warp 的 active lanes 条件不一致），实现必须 fail-fast 并返回可定位诊断（例如 `E_DIVERGENCE_UNSUPPORTED`）。

### A.2 PC 更新与可观察性

- 对 `bra` 这类控制 micro-op，执行单步结果必须能覆盖下一条指令的 pc（例如通过 `next_pc` 通道）。
- 若一次 inst 展开多个控制 micro-op（不常见但应防御），它们的 next_pc 必须一致；否则应诊断失败。

### A.3 非目标（明确不保证）

- 不支持 SIMT reconvergence（SIMT stack / reconvergence token）。
- 不支持真正意义上的 divergent branch / per-lane pc。

---

## 附录 B：Memory baseline（no_cache_addrspace）

本附录用于把 `no_cache_addrspace` 的“可观察行为边界”写清楚，避免 design 文档隐式假设漂移。

### B.1 基线承诺

- 无 cache：不模拟 L1/L2、替换/写回、tag 等。
- 保留地址空间概念：至少能区分 `param/global` 的访问路径与错误边界。
- lane-wise 访问：每个 active lane 的内存读写按其计算出的地址独立生效。

### B.2 对齐与未对齐访问

- bring-up 阶段建议采用“严格模式”：
  - 对于 `ld/st`，若地址不满足自然对齐（size=4 对齐 4，size=8 对齐 8），允许实现选择 fail-fast 并给出诊断（推荐），或按字节访问实现（若已实现且有测试覆盖）。
- design doc 必须明确选择哪一种，并给出回归用例。

### B.3 越界与未分配地址

- 对未分配/越界的 global 访问，必须 fail-fast 并返回诊断（而不是静默返回 0 或忽略写）。

### B.4 同地址多 lane 冲突（同一 inst 内）

- bring-up 阶段不承诺写冲突的硬件一致性细节。
- 若同一 warp 的多个 lanes 对同一地址执行 `st`，允许实现：
  - 采用确定性规则（例如按 lane id 从小到大覆盖），或
  - 直接诊断不支持。
- 不论采用哪种策略，都必须在 design doc 中显式写出并配套回归用例（否则视为未定义行为）。

---

## 附录 C：Determinism baseline（parallel/deterministic）

本附录用于约束“可复现回归”的口径，特别是并行执行与 trace/stats 的可观察性。

### C.1 配置约束

- 当 `sim.deterministic=true` 时：必须禁用并行 worker（即便 `sim.parallel=true`），或直接报错拒绝启动；推荐前者以便回归默认可跑。
- 当 `sim.parallel=true` 且 `sim.sm_count>1` 且 `sim.deterministic=false` 时：允许开启“每 SM 一个宿主线程”。

### C.2 可复现承诺（回归口径）

- `sim.deterministic=true`：
  - 要求 functional 结果（内存输出、返回码）可复现。
  - trace/stats 的顺序与内容应尽可能可复现（至少对同一输入不应随机波动）。
- `sim.deterministic=false`：
  - 允许 trace 事件顺序出现跨 SM 的交错差异；design doc 不应承诺严格事件顺序一致。

---

## 扩展策略（到其它 sm_xx）

当需要扩展到其它 `sm_xx` 时，建议按以下顺序承载差异：
1. 新增/扩展 profile 表（本文档）列出差异项（warp_size/地址宽度/新增 builtin/内存模型选择器等）
2. 通过独立的 `assets/ptx_isa/*.json` 与指令可用性矩阵承载“指令形态差异”
3. 仅在必要时引入 Parser/Binder 的 feature flags；避免在执行器写大量版本分支
