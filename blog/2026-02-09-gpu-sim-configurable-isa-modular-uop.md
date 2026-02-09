# gpu-sim：可配置指令集、micro-op 组合执行、可组合硬件架构的 PTX/SIMT 虚拟机

> 这篇文章介绍 gpu-sim 的核心设计取向：**指令集可配置**、**微指令（micro-op）驱动的执行链路**、以及 **硬件架构可配置（模块可替换/可组合）**。目前工程对外基线可以概括为：**PTX 6.4（冻结子集）+ sm_70 profile（功能级）**，并且通过 CUDA Runtime shim 能把 clang 编译的 `.cu` 产物以“像跑在真实 CUDA Runtime 上一样”的方式接入到 gpu-sim。

---

## 1. 一句话概览：把“PTX → 可执行语义”拆成可配置的两层

gpu-sim 的执行链路并不是把每条 PTX 指令直接硬编码成一段执行逻辑，而是刻意把“能解析到什么指令形态”和“这条指令语义怎么执行”拆开：

1) **PTX form → IR op（映射层）**：用 `ptx_isa` JSON 描述
- 由 PTX opcode / type 修饰 / operand 形态决定映射到哪个内部 IR opcode。
- 典型文件：`assets/ptx_isa/demo_ptx64.json`。

2) **IR op → micro-ops（语义层）**：用 `inst_desc` JSON 描述
- 语义层定义一条 IR 指令如何被展开为若干条 micro-op，并由基础执行单元执行。
- 典型文件：`assets/inst_desc/demo_desc.json`。

这种拆分的价值是：
- **扩 ISA 覆盖更像“加数据 + 补语义库”**：新增 PTX form 通常先改 JSON；只有当出现新的 IR op 或新的 micro-op 需求时才需要动 C++。
- **把“兼容 PTX 版本差异”变成 mapper/asset 侧的工作**：核心执行链路尽量避免塞进大量版本分支。

相关说明见用户文档：`docs/doc_user/cli.md` 中 “PTX op → IR op 映射（--ptx-isa）”。

---

## 2. micro-op 设计：用稳定契约把执行单元做小、做可复用

在 gpu-sim 的抽象设计里，micro-op（MicroOp）是 Executor 与基础执行单元（units）的“执行契约”。也就是说：

- 前端（frontend）负责把 PTX 文本解析成结构化 `InstRecord` 列表。
- 指令系统（instruction）把 `InstRecord` 映射/展开成 micro-ops。
- SIMT core 驱动每个 warp 的执行节奏、mask、分歧/合流。
- units（ExecCore/ControlUnit/MemUnit）只关心 micro-op 的类别与字段，不需要理解更高层的 PTX 语法细节。

在架构设计文档中，这条链路被明确写成 “**指令执行可组合：按指令描述文件将指令展开为 micro-ops**”。参见：
- `docs/doc_design/arch_design.md`（标题里就写明 *Streaming + 模块化 + micro-op 组合*）

你可以把它理解为：
- “一条 PTX 指令”不一定对应“一个执行函数”；
- 它可以对应一组 micro-op（例如：算术 + flag/predicate 写回，或地址计算 + 访存）。

这种设计非常利于扩展：
- 新增指令时，可以先复用已有 micro-op 组合；
- 真正需要新增执行能力时，新增 micro-op 枚举与 units 的实现点也很集中。

---

## 3. 硬件架构可配：把“硬件块图”落到“可替换软件模块”

gpu-sim 不只做“指令正确执行”，还希望在工程层面把“不同硬件架构/策略”的差异变成可配置、可回归的组合。

仓库里有一份非常明确的设计约束：
- `docs/doc_design/modules/10_modular_hw_sw_mapping.md`

核心思想是：
- 对于 **需要被替换** 的硬件部件（调度策略、并行执行模式、内存模型等），必须有对应的软件模块接口 + 工厂（factory/registry）+ 配置选择；
- 对于不在当前范围的微结构（cache/NoC 等），在 “No-cache” 基线下允许折叠为抽象实现，但 **保留替换出口**，保证未来能扩。

### 3.1 现在已经能“只改配置就切换”的部件

当前版本已经把以下部件做成了配置选择项（见 `docs/doc_user/modular_hw_sw_mapping.md`）：

- CTA 分发（Global CTA Scheduler）
- Warp 调度（Warp Scheduler）
- MemoryModel（No-cache 基线，但保留替换出口）

仓库自带一个演示配置：
- `assets/configs/demo_modular_selectors.json`

它会选择：
- `cta_scheduler=sm_round_robin`
- `warp_scheduler=round_robin_interleave_step`
- `memory_model=no_cache_addrspace`

并在 trace 的 `RUN_START` 事件里写入一次性的 `config_summary`，用于确认本次运行实际选中了哪些组件组合。

---

## 4. 当前对外基线：PTX 6.4 + sm_70（功能级）

工程当前的“对外交付口径”在多处文档中都有体现：

- CUDA demo 文档会建议用 clang 生成的 PTX 头部检查：
  - `.version 6.4`
  - `.target sm_70`
  - `.address_size 64`
  参见：`cuda/demo/README.md`。

- 用户文档中也多次以 `assets/ptx_isa/demo_ptx64.json` 作为默认 PTX ISA map。

这里强调的是 **功能级（functional）正确语义**：
- 支持 SIMT 语义：warp mask、predication、分歧与 reconvergence、barrier/atomic/fence 的功能正确规则（非周期精确）。
- 内存基线是 **No-cache + 地址空间分类**（global/shared/local/const/param）。

如果你想进一步了解“PTX 6.4 冻结子集 + sm70 profile”的规格与 bring-up 约束，仓库里也有专门的 spec/plan 文档可以作为扩展阅读入口，例如：
- `docs/doc_user/tiny_gpt2_minimal_coverage.md`
- `docs/doc_spec/tiny_gpt2_minimal_coverage_matrix.md`

---

## 5. CUDA Runtime shim：让 clang 编译的 `.cu` 可执行程序直接“跑在 gpu-sim 上”

gpu-sim 的一个很实用的能力是：通过 CUDA Runtime shim，把宿主侧对 CUDA Runtime 的调用接到 gpu-sim 的 runtime 上，从而让你能用 clang + CUDA Toolkit 编译 `.cu`，然后在不写 workload JSON 的情况下把 demo 跑起来（或至少显著减少 glue code）。

用户指南见：
- `cuda/docs/doc_user/cuda-shim.md`

文档明确了 shim 的范围（MVP）：
- 以 Linux/WSL 路径为主（demo binary 是 ELF；Windows 建议 WSL2）。
- 已实现的 API 包含：
  - `cudaMalloc/cudaFree/cudaMemcpy/cudaMemcpyAsync/cudaDeviceSynchronize/cudaGetLastError/cudaGetErrorString`
  - 基本 stream API：`cudaStreamCreate/Destroy/Synchronize/Query`
  - 以及 `__cudaRegister*` / `__cudaPush/PopCallConfiguration` 这些 host toolchain hook。
- kernel launch（`cudaLaunchKernel`）支持 demo 路径：fatbin→PTX 提取 + PTX `.param` 参数打包。

### 5.1 为什么它能“跑 executable”

关键点在于：
- 你的 demo 可执行程序在运行时会动态加载 `libcudart.so.12`。
- 通过设置 `LD_LIBRARY_PATH`，让动态加载器优先加载本仓库 build 出来的 shim（同 SONAME），从而把运行时调用劫持/转发到 gpu-sim。

### 5.2 目前的限制与一个实用的 workaround

文档也很诚实地说明：fatbin→PTX 提取仍是 MVP 级别，不同工具链可能把 PTX 以 tokenized/compressed 的形式塞到 fatbin 里。

所以仓库给了一个实用办法：
- 用同一份 `.cu` 源码额外生成 **文本 PTX**（例如 `clang++ ... -S -o streaming_demo.ptx ...`）
- 然后通过环境变量 `GPUSIM_CUDART_SHIM_PTX_OVERRIDE` 显式把 PTX 文本交给 shim。

这条路径在 `cuda/demo/README.md` 与 `cuda/docs/doc_user/cuda-shim.md` 里都有完整命令示例。

---

## 6. 扩展与定制：怎么“加一条指令 / 加一个模块”

### 6.1 扩展 PTX 指令覆盖（推荐路径）

通常按“从数据到语义”的顺序：

1) 在 `ptx_isa`（例如 `assets/ptx_isa/demo_ptx64.json`）里补一条或多条映射 entry
- 把目标 PTX form（`ptx_opcode + type_mod + operand_kinds`）映射到某个 `ir_op`。

2) 在 `inst_desc`（例如 `assets/inst_desc/demo_desc.json`）里为对应 `ir_op` 补齐语义
- 定义要展开成哪些 micro-ops。

3) 如果出现了新的 micro-op 能力需求
- 在 contracts/micro-op 枚举与 units 的实现点补上执行逻辑（通常集中在 `src/units/`）。

4) 用回归锁住
- 最小化的单测/集成测试 + trace 验证，避免新增 form 破坏已有匹配（例如 `DESC_AMBIGUOUS` / `DESC_NOT_FOUND`）。

### 6.2 扩展“硬件模块/策略”（profile/components）

建议遵循仓库里的模块化设计原则：

- 先定义接口与工厂：让 Runtime/Engine 只依赖 contracts
- 在 config 中加入 selector（支持 `arch.profile + arch.components`）
- 在 trace 的 `RUN_START` 写入选择结果，便于排查“到底跑的是哪个实现”
- 增加一个 smoke test：验证 selector 生效（不要只测结果，最好能测 trace/事件或内部标识）

这部分的设计约束与验收标准在：
- `docs/doc_design/modules/10_modular_hw_sw_mapping.md`

---

## 7. 小结

gpu-sim 的核心竞争力不是“把一个固定 GPU 架构做到极致还原”，而是把研究/验证中最常见的两类变化都工程化：

- 指令集变化：通过 `ptx_isa` 与 `inst_desc` 的两层资产，把 PTX form 覆盖与语义库扩展变成数据驱动。
- 架构变化：把硬件块图中的关键可替换点落实为可配置模块，用 profile/components 组合出不同“目标架构”。

如果你正想做的事情是：快速 bring-up 一条 PTX 执行链路、验证 SIMT 语义、或对调度/内存模型做可回归的策略实验，那么这种“可配置 + 可组合”的取向会非常顺手。