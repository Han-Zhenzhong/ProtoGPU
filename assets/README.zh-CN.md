# assets/

仓库内置的输入与数据资源（面向运行与测试）。

- `configs/`：运行配置 JSON（单 kernel / 并行 / 模块化 selectors 等）
- `inst_desc/`：指令描述 JSON（IR 语义库：ir_op → uops）
- `ptx/`：示例 PTX 程序
- `ptx_isa/`：PTX ISA 映射表 JSON（PTX op 形态 → ir_op + operand_kinds）
- `workloads/`：WorkloadSpec（streams/commands）示例 JSON（供 `gpu-sim-cli --workload` 使用）

---

## configs/（运行配置）

常用示例：

- `configs/demo_config.json`
	- 默认示例配置（串行）。

- `configs/demo_parallel_config.json`
	- 多 SM 并行执行示例（每 SM 一个宿主线程）。
	- 典型配合：`--grid 4,1,1` 产生多个 CTA 以观察并行。

- `configs/demo_modular_selectors.json`
	- 模块化 HW/SW mapping（profile/components + selectors）示例。
	- 选择：`cta_scheduler=sm_round_robin`、`warp_scheduler=round_robin_interleave_step`、`memory_model=no_cache_addrspace`。

相关文档：
- 使用者文档：[docs/doc_user/modular_hw_sw_mapping.md](../docs/doc_user/modular_hw_sw_mapping.md)
- 构建与运行：[docs/doc_build/build.zh-CN.md](../docs/doc_build/build.zh-CN.md)

---

## inst_desc/、ptx_isa/、ptx/

这三类文件通常配合使用：

- `ptx/*.ptx`：输入程序（kernel）
- `ptx_isa/*.json`：把 PTX 指令形态映射到内部 IR 语义条目
- `inst_desc/*.json`：内部 IR 语义条目对应的 micro-ops 展开规则

默认 demo 组合见：
- `ptx/demo_kernel.ptx`
- `ptx_isa/demo_ptx64.json`
- `inst_desc/demo_desc.json`

---

## workloads/（WorkloadSpec）

WorkloadSpec 用于描述 streams/commands 的可重放输入，配合 `gpu-sim-cli --workload <file>` 使用。

相关文档：
- [docs/doc_user/cli.md](../docs/doc_user/cli.md)
