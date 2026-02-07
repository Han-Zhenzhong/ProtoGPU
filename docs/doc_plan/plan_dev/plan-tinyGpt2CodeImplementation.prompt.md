## Plan: tiny GPT-2 最小覆盖代码实现计划

按 docs/doc_dev/tiny_gpt2/minimal_coverage_dev.md 与 docs/doc_design/tiny_gpt2/minimal_coverage_design.md，把 M1→M4 依赖链拆成可直接开工的实现顺序：先让资产与解析链路 fail-fast 并支持 `%fN/0fXXXXXXXX`，打通 `ld/st.global.f32 + fma.f32`；再补 `mul`/地址算术；随后引入 `required_flags + setp + predication guard`；最后做 `bra` 的 label→pc 与 next_pc 提交机制。

### Steps
1. 做 `DescriptorRegistry` fail-fast，扩展 uop op 字符串解析于 src/instruction/descriptor_registry.cpp 的 `parse_uop_op`/`parse_uop_kind`，同步扩展 include/gpusim/contracts.h 的 `MicroOpOp`.
2. 支持 `%fN` 与 `0fXXXXXXXX` 操作数解析：改 src/instruction/ptx_isa.cpp 的 `parse_operand_by_kind("reg"/"imm")`，并补齐 src/frontend/parser.cpp 的寄存器计数（保证 `%f` 走 `r_u32` bank）.
3. 落地 M1 资产与执行：补 assets/ptx_isa/demo_ptx64.json 与 assets/inst_desc/demo_desc.json 的 `ld/st.f32`、`fma.f32` entries，实现 src/units/exec_core.cpp 的 `ExecCore::step` 里 `MicroOpOp::Fma`（按 `uop.attrs.type==F32` 做 bitcast 运算）.
4. 落地 M2 乘法与地址算术：新增 `MicroOpOp::Mul`（include/gpusim/contracts.h）并实现 src/units/exec_core.cpp 的 `Mul`，同步补齐 assets/ptx_isa/demo_ptx64.json / assets/inst_desc/demo_desc.json 的 `mul`/`add.u64` forms.
5. 落地 M3 `required_flags + setp + predication`：扩展 schemas/ptx_isa.schema.json 与 src/instruction/ptx_isa.cpp（`PtxIsaEntry.required_flags` + 匹配过滤），实现 `MicroOpOp::Setp` 于 src/units/exec_core.cpp，并在 src/simt/simt.cpp 的 `SimtExecutor` 执行每条 inst 前把 `InstRecord.pred` 编译进 `uop.guard`.
6. 落地 M4 `bra`：在 src/frontend/parser.cpp 做两遍扫描建立 label→pc 并把 `bra label` 改写为 `imm(pc)`；在 include/gpusim/contracts.h 的 `StepResult` 增加 `next_pc`，实现 `MicroOpOp::Bra` 于 src/units/control_unit.cpp，并在 src/simt/simt.cpp commit 逻辑里优先应用 `next_pc` 否则 `pc += 1`.

### Further Considerations
1. `%fN` 寄存器声明与计数：为 %f 单独建 float reg bank。
2. `SETP`/predicate bank 约束：pred_id 越界报错，定为 fail-fast，避免 bounds-check 静默失效。
3. `BRA` 初期 uniform 约束：直接上 SIMT reconvergence（SIMT stack）。
