## Plan: tiny GPT-2 最小覆盖实现设计

把 docs/doc_spec/tiny_gpt2_minimal_coverage_matrix.md 里的 M1–M4 变成“可实现、可验证、可迭代”的工程设计：先补齐最关键的 FP32 global 访存与 FP32 算术闭环，再补地址算术、谓词生效、最小控制流。设计上要显式处理当前链路的硬约束：form 匹配只看 `opcode/type_mod/operand_kinds`，`flags[]` 不参与；`space` 只影响 MemUnit；未知 uop op 目前会静默降级为 MOV，必须 fail-fast 才能安全 bring-up。

### Steps
1. 固化 M1 目标与输入约束，明确“受约束 PTX vs 原生 nvcc PTX”策略，落到 docs/doc_spec/tiny_gpt2_minimal_coverage_matrix.md 的 M1 段落。
2. 设计并落地 `ld/st.global.f32` 的映射与 descriptor：扩展 assets/ptx_isa/demo_ptx64.json 与 assets/inst_desc/demo_desc.json，对齐 `PtxIsaMapper::map_one` 与 `DescriptorRegistry::lookup`（见 src/instruction/ptx_isa.cpp 与 src/instruction/descriptor_registry.cpp）。
3. 设计 FP32 算术执行模型：扩展 `MicroOpOp`（见 include/gpusim/contracts.h）与 `ExecCore::step`（见 src/units/exec_core.cpp），决定 `fma.f32` 走新 op（FMA）还是 `mul+add` 两步实现。
4. 设计地址/索引算术最小集：补 `add.u64` 与 `mul`（wide 语义用 dst 寄存器宽度承载），并同步更新映射/descriptor；约束不依赖 `.wide/.lo/.hi`（因 `flags[]` 不参与匹配，见 src/frontend/parser.cpp 与 src/instruction/ptx_isa.cpp）。
5. 设计谓词与 predication：实现 `setp` 写 `WarpState::p`（见 include/gpusim/units.h），并在 `SimtExecutor` 执行 uops 前把 `InstRecord.pred` 编译到 `MicroOp.guard`（当前 `Expander::expand` 固定全 lane guard，见 src/instruction/expander.cpp 与 src/simt/simt.cpp）。
6. 设计最小 `bra`：新增 label→pc 绑定（Parser/Binder 层，见 src/frontend/parser.cpp），并改造 PC 更新机制（`SimtExecutor` 当前固定 `pc += 1`，见 src/simt/simt.cpp）以支持 ControlUnit 改写 next PC（ControlUnit 现仅 `RET`，见 src/units/control_unit.cpp）。

### Further Considerations
1. PTX 输入策略：直接支持 nvcc 风格 `%fN` 与浮点立即数。
2. `DescriptorRegistry` 的未知 op 行为：改为 fail-fast（强烈建议），避免 JSON 写错导致“静默 MOV”误执行。
3. `bra` 语义边界：直接设计可扩展到 reconvergence（SIMT stack）的接口。
