# 01 Dev：PTX6.4 前端与映射（实现落点）

本文是 [docs/doc_design/ptx6.4_sm70/01_ptx64_frontend_and_mapping.md](../../doc_design/ptx6.4_sm70/01_ptx64_frontend_and_mapping.md) 的实现级说明：把“冻结的 PTX6.4 子集 + mapper 匹配键”落实到代码路径、关键函数与常见排错。

单一真源（规格）
- [docs/doc_spec/ptx64_baseline.md](../../doc_spec/ptx64_baseline.md)

---

## 1) 端到端数据路径（Runtime 视角）

典型调用链（以 tests/runtime 路径为准）：

- `Runtime::run_ptx_kernel*_launch(...)`
  - 解析 PTX：`Parser::parse_ptx_file_tokens()`（src/frontend/parser.cpp）
  - 选 entry：`Binder::bind_*()`（src/frontend/binder.cpp；当前仅做选择）
  - 载入 PTX ISA map：`PtxIsaRegistry::load_json_file()`（src/instruction/ptx_isa.cpp）
  - 映射 PTX→IR：`PtxIsaMapper::map_kernel()`（src/instruction/ptx_isa.cpp）
  - 进入 Instruction System（inst_desc lookup/expand）与 SIMT 执行（不在本文展开）

实现提示
- 只要你看到 `KernelTokens`，说明还在“PTX tokenization”阶段；看到 `InstRecord`，说明已经完成“PTX→IR”映射。

---

## 2) PTX tokenization 子集（Parser）

实现文件
- `src/frontend/parser.cpp`

关键输出
- `ModuleTokens` / `KernelTokens`：
  - `name`
  - `params`（包含 offset/size/align；自然对齐布局）
  - `reg_u32_count/reg_u64_count`（注意 `.reg .f32` 计入 u32 bank）
  - `insts: vector<TokenizedPtxInst>`（包含 `ptx_opcode/mods/operand_tokens/pred/dbg`）

### 2.1 参数布局（natural alignment）

- `.param .u32`：align=4；`.param .u64`：align=8
- offset 计算位于 `parse_ptx_text_tokens()` 的 param 处理分支：按声明顺序做自然对齐。

这保证了 `KernelArgs.layout/blob` 可以按照 `ParamDesc.offset/size` 直接 pack。

### 2.2 寄存器计数

- `.reg .u32 %r<N>` → `reg_u32_count = max(reg_u32_count, N)`
- `.reg .u64 %rd<N>` → `reg_u64_count = max(reg_u64_count, N)`
- `.reg .f32 %f<N>` → **同样计入 `reg_u32_count`**

注意
- 这是 `%f` 复用 u32 payload bank 的关键约定点。

### 2.3 predication tokenization

- `tokenize_inst_line()` 会把 `@%pN` / `@!%pN` 提取为 `TokenizedPtxInst.pred`。
- predication 的 lane-wise 语义在 SIMT 层注入 guard（见相应 design/dev doc）。

### 2.4 Label → PC 改写（M4）

实现位置
- `Parser::parse_ptx_text_tokens()` 在遇到 `}`（kernel end）时执行两遍 body 扫描：
  1) pass1：构建 `label_to_pc`（只对“真正指令行”计数）
  2) pass2：tokenize 指令并把 `bra <label>` 改写为 `bra <imm_pc>`

实现约束
- 只有 `bra` 且 `operand_tokens.size()==1` 时会做改写。
- 改写后的 operand 是十进制字符串（例如 "12"），后续会以 `operand_kind=imm` 被 mapper 解析。

---

## 3) PTX → IR 映射（PtxIsaMapper）

实现文件
- `src/instruction/ptx_isa.cpp`

### 3.1 匹配键冻结（bring-up）

- `PtxIsaRegistry::candidates()`：按 `ptx_opcode/type_mod/operand_count` 预筛。
- `PtxIsaMapper::map_one()`：对每个 candidate 逐 operand 调用 `parse_operand_by_kind(kind, token, type_mod, ...)`。

冻结语义（对应 design）
- 唯一匹配键：`ptx_opcode + type_mod + operand_kinds`。
- `mods.space` 与大多数 `mods.flags` 不参与匹配。

错误码（常见）
- `DESC_NOT_FOUND`：没有任何 entry（opcode/type_mod/operand_count）
- `OPERAND_FORM_MISMATCH`：有候选 entry，但都 parse 失败
- `DESC_AMBIGUOUS`：有多个 entry 都 parse 成功（会把可选签名拼进 message）
- `OPERAND_PARSE_FAIL`：parse 过程产生了更具体的失败原因（例如 `%f` 解析失败、addr offset 解析失败等）

### 3.2 operand_kinds 的实现细节（parse_operand_by_kind）

支持的 kind（冻结枚举）：`reg/pred/imm/addr/symbol/special`。

- `reg`
  - `%rN` → `OperandKind::Reg` + `ValueType::U32`
  - `%rdN` → `OperandKind::Reg` + `ValueType::U64`
  - `%fN` → `OperandKind::Reg` + `ValueType::F32`（payload 存在 u32 bank）
  - 额外规则：若 `type_mod` 是 `u64/s64` 且读到了 `%r`，会把 operand.type 升到 U64（用于诸如 `mul.u32 %rd, %r, imm` 这类写法）

- `imm`
  - 十进制/0x/负数：走 `std::stoll(..., base=0)`
  - `0fXXXXXXXX`：解析为 32-bit payload，写入 `imm_i64`（低 32 位有效），并把 `Operand.type=F32`

- `addr`
  - 仅接受 bracket 形式：`[%rdN]` 或 `[%rdN+imm]`
  - 解析为：base reg id + imm offset，存入 `Operand{kind=Addr, reg_id, imm_i64}`

  开发建议
  - fixture 里优先用 `[%rd+imm]`，避免依赖 `add.u64 (reg,reg,imm)` 的 form（否则测试可能提前在 mapper 失败，覆盖不到真正的 memory 行为）。

- `symbol`
  - 符号不能是立即数字面量（例如 "0"/"-1"/"0x10" 会被拒绝），用于消除 `imm vs symbol` 的歧义。
  - bracketed symbol（如 `[out_ptr]`）会被归一化为 `symbol`（只要 inner 不是寄存器）。

- `special`
  - 目前仅接受一小组 builtin：`%tid.{x,y,z} %ntid.{x,y,z} %ctaid.{x,y,z} %nctaid.{x,y,z} %laneid %warpid`。
  - 注意：`%laneid/%warpid` 是标量 builtin（无 `.x/.y/.z` 后缀）。

---

## 4) 新增/修改一个 PTX form 的推荐流程

1. 先写 fixture（你想支持的 PTX 写法）。
2. 补 `assets/ptx_isa/*.json`：
   - `ptx_opcode/type_mod/operand_kinds/ir_op`
   - 确保 `operand_kinds` 的顺序与 PTX 操作数顺序一致。
3. 跑 Tier‑0 回归：`gpu-sim-tiny-gpt2-mincov-tests`。
4. 若失败，按 `diag.code` 分流：
   - `DESC_NOT_FOUND`：缺 ptx_isa entry 或 opcode/type_mod 写错
   - `OPERAND_PARSE_FAIL`：token 写法不符合 kind（例如 addr 没有 `[]`）
   - `OPERAND_FORM_MISMATCH`：operand_kinds 写错、或存在歧义导致 parse 都失败
   - `DESC_AMBIGUOUS`：有多个 entry 都能 parse 成功，需要收敛 forms（或未来引入 required_flags 并在规格+schema 里同步）

---

## 5) 验收入口

- Tier‑0（推荐）：

```bash
ctest --test-dir build -C Release -V -R "^gpu-sim-tiny-gpt2-mincov-tests$"
```

- 典型 fixture 位置：`tests/fixtures/ptx/*.ptx`

（若你新增了新的 mapper 行为，优先把覆盖并入 Tier‑0；除非你能明确说明它不应成为 Tier‑0 的长期承诺。）
