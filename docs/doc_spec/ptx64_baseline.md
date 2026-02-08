# PTX 6.4 baseline（冻结子集）

本文档定义 gpu-sim 当前阶段的 **PTX 6.4 基线**：
- 作为“完整仿真”对外口径的 PTX 输入约束
- 作为 `assets/ptx/*.ptx`、`assets/ptx_isa/*.json`、`assets/inst_desc/*.json` 的对齐依据
- 作为后续扩展到 PTX 7.x/8.x 的兼容层边界（增量目标）

> 说明：gpu-sim 的前端当前以“tokenization + 映射”的方式处理 PTX；部分 header/directive 目前属于 **约定**（资产一致性），未必在代码里强制校验。

---

## Baseline Summary

| 项 | 基线值 | 资产/实现落点 |
|---|---|---|
| PTX 版本（对外口径） | `.version 6.4` | demo 资产：`assets/ptx/demo_kernel.ptx`、`assets/ptx/write_out.ptx` |
| 硬件 target（对外口径） | `.target sm_70` | demo 资产：`assets/ptx/*.ptx`；硬件基线见 `docs/doc_spec/sm70_profile.md` |
| 地址宽度（PTX header） | `.address_size 64` | demo 资产：`assets/ptx/*.ptx` |
| 输入模型 | PTX asm text → tokenized insts → `ptx_isa` map → IR InstRecord → `inst_desc` 展开 | 代码路径：`Parser` / `PtxIsaMapper` / `DescriptorRegistry` / `SimtExecutor` |

---

## PTX 文件结构（子集）

### Header（约定）
基线资产应包含并保持一致：
- `.version 6.4`
- `.target sm_70`
- `.address_size 64`

备注：当前 `Parser` 主要关注 kernel 的 `.visible .entry` 结构与指令行，对 header 的语义约束主要由 **资产约定** 与测试/文档保证。

### Kernel entry（必需）
基线支持单个或多个 kernel entry：
- `.visible .entry <name>( ... ) { ... }`

`Binder` 侧支持：
- 选择第一个 kernel
- 或按 entry 名字选择

### 参数声明（子集）
在 entry 参数表内支持：
- `.param .u32 <name>`
- `.param .u64 <name>`

参数布局规则：按声明顺序做 natural alignment（U32 对齐 4，U64 对齐 8），并写入 `KernelTokens.params`。

### 寄存器声明（子集）
在 kernel body 内支持：
- `.reg .u32 %r<N>;`
- `.reg .u64 %rd<N>;`
- `.reg .f32 %f<N>;`

解析逻辑：从 `%r<...>` / `%rd<...>` 的 `<N>` 提取最大计数，用于 `KernelTokens.reg_u32_count/reg_u64_count`。

补充：`%fN` 的实现约定
- `%fN` 在执行器侧按 `ValueType::F32` 解释，但底层存储复用 `WarpState.r_u32` 的 32-bit payload（bitcast）。
- 因此寄存器计数上，`.reg .f32 %f<...>` 会计入 `reg_u32_count`（与 `%r` 同一个 bank 的计数口径）。

---

## 指令行语法（tokenization 子集）

### 基本形态
- 可选谓词 guard：`@%p0` / `@!%p0`
- 指令与修饰：`<ptx_opcode>[.<mod>]*`
- 操作数 token：逗号分隔，以 `;` 结束

示例：
- `mov.u32 %r0, 1;`
- `@!%p0 add.u32 %r2, %r0, %r1;`
- `ld.param.u64 %rd0, [out_ptr];`

### 可识别的 mod（子集）
`Parser` 将 mod 归类为：
- `type_mod`：`u32/s32/u64/s64/f32`
- `space`：`global/shared/local/const/param`
- 其它：进入 `flags[]`（由后续阶段决定是否解释）

---

## 操作数与 `ptx_isa` 映射（冻结接口）

gpu-sim 将“PTX 指令形态”与“IR 语义（inst_desc）”解耦：
- PTX → IR：由 `assets/ptx_isa/*.json`（schema：`schemas/ptx_isa.schema.json`）定义
- IR → uops：由 `assets/inst_desc/*.json` 定义

### operand_kinds（当前实现子集）
`ptx_isa` 的 `operand_kinds` 当前枚举为：
- `reg`：寄存器 `%rN/%rdN/%fN`
- `pred`：谓词 `%pN`
- `imm`：立即数（十进制整数，或 `0fXXXXXXXX` 形式的 32-bit 浮点 bit-pattern）
- `addr`：地址 `[%rdN]` 或 `[%rdN+imm]`
- `symbol`：符号（含参数名等）
- `special`：builtin（例如 `%tid.x`）

> 注意：builtin 的 token 在 `Parser` 阶段不会类型化；是否能正确映射/执行取决于 `ptx_isa` 中是否存在对应 form（例如 `mov.u32 (reg, special)`）。

---

## 指令覆盖（PTX 6.4 基线：最小集合）

### 当前可执行覆盖（repo demo 级别）

基线“可执行”覆盖以仓库 demo 资产与默认映射为准：
- `mov.u32`：`(reg, imm)`、`(reg, special)`、`(reg, symbol)`
- `add.u32`：`(reg, reg, reg)`
- `ld.param.u64`：`(reg, symbol)`（`MemUnit` 的 param load 当前只接受 symbol；`[param_name]` 会在映射阶段归一化为 symbol）
- `st.u32`：`(addr, reg)`
- `ret`：`()`

对应资产：
- demo PTX：`assets/ptx/demo_kernel.ptx`
- demo PTX ISA map：`assets/ptx_isa/demo_ptx64.json`

---

## tiny GPT-2 bring-up baseline（M1–M4 级）

本节定义“为了跑通 tiny GPT-2（FP32 推理）bring-up”所冻结的最小可执行子集（M1–M4）。

目的
- 作为 design/dev/test 的共同基线：写 PTX fixture、扩展 `ptx_isa/inst_desc`、实现 Units/SIMT 时都以此为准。
- 强制可回归：每次扩展必须保持 `gpu-sim-tiny-gpt2-mincov-tests` 通过。

### 约束（必须满足）

- 映射匹配规则仍冻结为：`ptx_opcode + type_mod + operand_kinds`。
	- `space` 与大多数 `flags[]` 不参与 mapper 匹配（除非未来专项引入 `required_flags`）。
- bring-up 阶段 fail-fast：
	- `inst_desc` 中未知 uop `kind/op` 必须在加载阶段报错（禁止 silent fallback）。
- 控制流约束（M4）：
	- 只支持 **uniform control-flow** 的 `bra/ret`。
	- 若 `@%p bra` 在同一 warp 内出现分歧（divergence），必须诊断失败（例如 `E_DIVERGENCE_UNSUPPORTED`）。

### 必须支持的 PTX 形式（冻结接口）

说明
- 下表描述的是“PTX 可书写形态”；实际执行取决于 `assets/ptx_isa/*.json` 是否能把它映射到 IR op，以及 `assets/inst_desc/*.json` 是否提供语义（uops）。

M1：FP32 数值闭环（load + fma + store）
- `ld.param.u64 (reg, symbol)`
- `ld.param.u32 (reg, symbol)`
- `ld.global.f32 (reg, addr)`
- `st.global.f32 (addr, reg)`（可被 predication guard 关闭）
- `fma.f32 (reg, reg, reg, reg)`
- `fma.f32 (reg, reg, reg, imm)`（用于 `0fXXXXXXXX`）

M2：地址/索引算术
- `add.u64 (reg, reg, reg)`
- `mul.u32 (reg, reg, reg)`
- `mul.u32 (reg, reg, imm)`（常用：`tid * 4` 这种字节缩放）

M3：predicate 与 predication
- `setp.s32 (pred, reg, reg)`（比较条件由 flags 决定；当前实现支持至少 `lt/le/gt/ge/eq/ne`）
- predication：`@%pN` 与 `@!%pN` 必须对 uops 生效（lane mask 注入到 `uop.guard`）

M4：最小分支
- `bra (imm)`（label 在 Parser 阶段改写为 pc 立即数）
- `ret ()`

### 资产落点（当前默认）

- PTX ISA map：`assets/ptx_isa/demo_ptx64.json`
- inst_desc：`assets/inst_desc/demo_desc.json`

### 回归入口（必须存在）

- CTest：`gpu-sim-tiny-gpt2-mincov-tests`
	- fixture PTX：`tests/fixtures/ptx/m1_fma_ldst_f32.ptx`、`tests/fixtures/ptx/m4_bra_loop_u32.ptx`
	- 运行（示例）：`ctest --test-dir build -C Release -V -R "^gpu-sim-tiny-gpt2-mincov-tests$"`

---

---

## 面向 tiny GPT-2 的差距检查（增量目标）

要跑通 tiny GPT-2（FP32 推理），repo demo 级别的最小集合远不够；建议把需求拆成“PTX 层面可表达 + IR/uops 层面可执行 + 运行时/内存可支撑”三组增量。

备注：本仓库已落地 tiny GPT-2 bring-up baseline（M1–M4），其冻结子集见上节；下面内容更多用于继续扩展到更真实的 GPT-2 kernel/workload。

### A. PTX→IR 映射需要补齐的 form
- `ld.global.*`：需要新增 `ptx_isa` entry 使用 `(reg, addr)`（因为全局 load 的执行器当前只接受 `OperandKind::Addr`）
- `st.global.*`：补齐更多类型（至少 `st.global.f32`）

### B. 执行器需要补齐的数值能力（核心）
- FP32：`mov.f32`、`add.f32`、`mul.f32`、`fma.f32`
-（可能需要）类型转换：`cvt.*`（例如 index/offset 到 pointer/addr 计算）

### C. 控制流/谓词（用于边界检查与循环）
- `setp.*`、`bra` 与 predication（`@%p` / `@!%p`）
- divergence/reconvergence 机制（或明确约束：kernel 不含分支/所有边界通过 launch 配置与 padding 消除）

### D. 内存与张量数据布局（最小）
- `ld.global.f32` / `st.global.f32`（权重/激活读写）
- 64-bit pointer + base+imm addressing 的普遍覆盖（`[%rdN+imm]`）

结论：如目标确定为 tiny GPT-2，建议把本文件中的“最小集合”保留为 demo bring-up 基线，同时在后续计划里新增一份“GPT2 子集覆盖矩阵”（opcode/type_mod/operand_kinds → ir_op → uops）。

覆盖矩阵文档：`docs/doc_spec/tiny_gpt2_minimal_coverage_matrix.md`。

---

## 非目标/不保证（明确边界）

在“PTX 6.4 基线”阶段，下列能力不做承诺（除非另有专项计划）：
- 全量 PTX directive 解析与严格验证（例如 `.pragma`、`.func`、`.callprototype` 等）
- 完整的 type system 与转换指令覆盖
- 完整的 memory consistency/atomics/fence/scope 语义
- 对真实硬件 cycle-accurate 的时序复刻

---

## 后续扩展（PTX 7.x/8.x 兼容层）

建议优先用“数据驱动”扩展：
1. 新增/扩展 `assets/ptx_isa/*.json`（可按版本/子集拆分）
2. 扩展 `inst_desc`（增加 IR op 语义库）
3. 仅在必要时扩展 `Parser`（增加 tokenization 能力或对 header 做显式校验）
