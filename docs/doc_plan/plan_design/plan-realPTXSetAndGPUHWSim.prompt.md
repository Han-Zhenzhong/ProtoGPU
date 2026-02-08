## Plan: 后续完整仿真与对外交付路线

在已交付“多 core 并行（每 SM 一个线程）+ 硬件模块↔软件模块映射/可配置组合”之后，下一阶段建议把“完整仿真（对外交付口径）”拆成四条可并行但有依赖的工作流：先冻结一个 PTX 版本与指令/操作数覆盖矩阵，再冻结一个硬件 profile（不等于 cycle-accurate 的真实 SKU，而是可配置模块组合的基准模型），随后把输出格式契约化与版本化，最后收敛成可被外部工程稳定依赖的 API（含打包/版本策略与嵌入式加载方式）。

补充：当前仓库已形成一个可回归的 bring-up 子集（tiny GPT-2 M1–M4）。后续“完整仿真”的对外口径建议以“6.4 冻结子集 + sm70 profile”为边界，并把 M1–M4 作为 Tier-0（必须持续通过的质量闸门）。

### Steps
1. 冻结 PTX 版本范围与覆盖矩阵，落到 docs/doc_plan/plan_design/ 并引用 [docs/doc_design/modules/03_instruction_system.md](../../doc_design/modules/03_instruction_system.md) 与 [docs/doc_design/modules/02_frontend.md](../../doc_design/modules/02_frontend.md)。
	- 当前 PTX 基线（6.4 冻结子集）规格表见 [docs/doc_spec/ptx64_baseline.md](../../doc_spec/ptx64_baseline.md)。
	- 以其中的 “tiny GPT-2 bring-up baseline（M1–M4 级）” 作为 Tier-0 覆盖：每个 form 都必须同时具备 `ptx_isa` entry、`inst_desc` 语义、以及最小回归（CTests/fixtures）。
2. 对齐资产与实现的 PTX 版本命名，明确 “demo kernels .version/.target” 与 “ptx_isa 映射文件” 的关系，并消除命名歧义（现有示例见 [assets/ptx/demo_kernel.ptx](../../../assets/ptx/demo_kernel.ptx) 与 [assets/ptx_isa/demo_ptx64.json](../../../assets/ptx_isa/demo_ptx64.json)）。
3. 定义一个“硬件型号=架构 profile”基线（no-cache、可替换调度/并行模式/内存语义强弱），在 docs/doc_plan/plan_design/ 建 profile 表，并与 [docs/doc_design/modules/10_modular_hw_sw_mapping.md](../../doc_design/modules/10_modular_hw_sw_mapping.md) 和 [docs/doc_spec/gpu_block_diagram.puml](../../doc_spec/gpu_block_diagram.puml) 对齐。
	- 当前工程硬件基线为 sm70，具体条目表见 [docs/doc_spec/sm70_profile.md](../../doc_spec/sm70_profile.md)。
	- profile 设计必须显式覆盖其附录约束：Control-flow uniform-only（M4 bring-up）、`no_cache_addrspace` 的可观察行为边界、以及 deterministic/parallel 的回归口径。
4. 规范化执行结果输出：为 trace/stats/（可选）memory snapshot 建立格式契约与版本字段，更新用户文档入口并对齐现有输出实现 [src/observability/observability.cpp](../../../src/observability/observability.cpp) 与 golden 目录约定 [tests/golden/README.md](../../../tests/golden/README.md)。
5. 设计并冻结对外 API 形态：C++ 稳定 API（可选 C API）+ “in-memory module/ISA map” 支持，基于现有头文件入口 [include/gpusim/runtime.h](../../../include/gpusim/runtime.h)（workload 入口当前为 `Runtime::run_workload(path)`），并把打包/安装/版本策略写入 docs/doc_dev/。

### Further Considerations
1. PTX “完整仿真”口径：工程当前以 **PTX 6.4** 为基准开发时，建议把“对外交付的完整仿真基线”固定为 **6.4 + 冻结子集**（覆盖矩阵与资产版本化一起落地）。其中 tiny GPT-2 M1–M4 作为 Tier-0（必须回归通过）；对 PTX 7.x/8.x 的支持作为 **兼容层/增量目标**：用独立的 `ptx_isa` map（以及必要时的 Parser/Binder 特性开关）逐步扩展，而不是在核心执行链路里引入大量版本分支。
2. 硬件“完整仿真”口径：当前工程以 **sm70** 作为硬件基线开发时，建议把对外说明锚定为“sm70 profile（可配置模块组合）”。短期除 warp 大小（32）、基础 builtin、寄存器/地址宽度外，还应把 bring-up 阶段最容易产生隐式假设漂移的边界固定下来：
	- Control-flow：uniform-only + divergence 必须 fail-fast（见 sm70_profile 附录 A）
	- Memory：对齐/越界/同地址多 lane 冲突的策略必须显式（见 sm70_profile 附录 B）
	- Determinism：deterministic 与 parallel 的承诺边界（见 sm70_profile 附录 C）
	后续扩展到其它 `sm_xx` 时，用“profile + 指令可用性矩阵 + ptx_isa map”三件套承载差异，而非把架构分支散落到执行器。
3. 输出优化优先级：Option A 先做 schema+版本化；Option B 再做可视化/trace viewer 对接（见 [tools/trace_viewer/README.md](../../../tools/trace_viewer/README.md)）。

4. 质量闸门建议：
	- 每次扩展 PTX form 或 profile 行为时，必须新增/更新最小回归（fixtures + CTests），并确保 `gpu-sim-tiny-gpt2-mincov-tests` 维持通过。
