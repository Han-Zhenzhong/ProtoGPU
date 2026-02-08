# 01 PTX6.4 前端与映射（冻结接口）

本设计在 ../modules/02_frontend.md 与 ../modules/02.01_frontend_desc_driven_decode.md 的基础上，进一步**冻结对外可依赖的 PTX 6.4 子集**与 mapper 匹配规则。

规格来源：../../doc_spec/ptx64_baseline.md

## 1. 输入约束（Header 与 kernel 结构）

- Header（资产约定）：
  - `.version 6.4`
  - `.target sm_70`
  - `.address_size 64`

- Kernel entry（必需）：`.visible .entry <name>(...) { ... }`
- 参数声明（最小）：`.param .u32` / `.param .u64`
- 寄存器声明（最小）：`.reg .u32 %r<N>`、`.reg .u64 %rd<N>`、`.reg .f32 %f<N>`

约定（必须写死）：
- `%fN` 复用 u32 bank 的 32-bit payload（bitcast 语义）；因此 `.reg .f32` 计入 `reg_u32_count`。

## 2. tokenization 子集

指令行形态：
- 可选 predication：`@%p0` / `@!%p0`
- opcode + modifiers：`<ptx_opcode>[.<mod>]*`
- operands：逗号分隔，`;` 结束

mods 归类：
- `type_mod`：`u32/s32/u64/s64/f32`
- `space`：`global/shared/local/const/param`
- 其它 → `flags[]`（bring-up 阶段不参与 mapper 匹配）

## 3. PTX → IR（PtxIsaMapper）匹配键冻结

bring-up（M1–M4）阶段**冻结**：
- 唯一匹配键：`ptx_opcode + type_mod + operand_kinds`
- `space` 与大多数 `flags[]` 不参与匹配

原因
- 该规则能保证资产扩展“可控且可回归”，避免由于 flags/space 的组合爆炸导致隐式行为漂移。

未来扩展（但不属于本冻结基线）
- 如确需让 flags 参与匹配，必须引入显式机制（例如 `required_flags`），并在规格与 schema 中同步定义；不能用“隐式字符串比较”。

## 4. operand_kinds（冻结枚举）

与 ../../doc_spec/ptx64_baseline.md 一致：
- `reg`：`%r/%rd/%f`
- `pred`：`%p`
- `imm`：十进制整数；或 `0fXXXXXXXX`（32-bit payload）
- `addr`：`[%rdN]` 或 `[%rdN+imm]`
- `symbol`：符号（参数名等）
- `special`：builtin（例如 `%tid.x`）

### 4.1 `0fXXXXXXXX` 立即数

冻结语义：
- `0fXXXXXXXX` 解析为 32-bit 无符号 payload（hex），在执行期按 `ValueType::F32` 做 bitcast 解释。
- 该形式必须通过 `operand_kind=imm` 进入 mapper/descriptor。

### 4.2 `%fN` 寄存器

冻结语义：
- 在 mapper/IR 层面 `%fN` 的 `operand_kind` 仍为 `reg`。
- `type_mod=f32` 决定执行器按 F32 解释 payload。

## 5. Label → PC 改写（M4）

冻结策略：
- Parser/Binder 两遍扫描生成 `label_index: label -> pc_index`。
- 对 `bra <label>`：在绑定完成后将 label 操作数改写为 `imm(pc_index)`。

约束：
- 该改写只服务于 M4 最小分支（uniform-only）。
- 改写后的 IR/descriptor 都以 `operand_kind=imm` 进行匹配（而不是 symbol/label）。

## 6. 失败策略（fail-fast）

- 任意 PTX 指令形态在 `ptx_isa` 中找不到 entry：必须返回诊断失败（并包含 file/line 与 inst_index）。
- `ptx_isa` entry 找到但 inst_desc 缺失/歧义：同样必须诊断失败。

该策略是 Tier-0 回归可维护性的前提：缺口必须在第一时间暴露，而不是在执行期产生静默错误。
