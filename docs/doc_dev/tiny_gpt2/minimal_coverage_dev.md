# Dev: tiny GPT-2 最小可跑覆盖（M1–M4）

本 dev doc 把 `docs/doc_design/tiny_gpt2/minimal_coverage_design.md` 落到**可执行的开发步骤**（按依赖顺序），并给出每一步的验收方式与排错抓手。

关联：
- Design：`docs/doc_design/tiny_gpt2/minimal_coverage_design.md`
- Spec/矩阵：`docs/doc_spec/tiny_gpt2_minimal_coverage_matrix.md`

---

## 0. 准备：统一约定与验收口径

### 0.1 约定（本阶段强制）
- form 匹配仍遵循：`ptx_opcode + type_mod + operand_kinds`。
- `mods.space` 不参与匹配，只影响 `uop.attrs.space`（由 `Expander` 写入）。
- bring-up 阶段必须 **fail-fast**：任何 JSON uop op/kind 不合法，要在加载时直接失败。

### 0.2 最小回归形式
优先做 integration style（走 runtime/cli 全链路），每个里程碑至少一个：
- M1：f32 `ld` + `fma` + `st` 数值正确
- M2：地址算术正确（每 lane 访问正确）
- M3：bounds-check（predication 生效）
- M4：loop（`bra`/pc 正确）

---

## 1. M1（第一优先）：`ld/st.global.f32` + `fma.f32` 闭环

### 1.1 资产：补齐 `ptx_isa` 与 `inst_desc`
改动文件：
- `assets/ptx_isa/demo_ptx64.json`
- `assets/inst_desc/demo_desc.json`

新增 entry（最小集合）：
- `ld.f32 (reg, addr) -> ir_op=ld`
- `st.f32 (addr, reg) -> ir_op=st`
- `fma.f32 (reg, reg, reg, reg) -> ir_op=fma`

验收（静态）：
- `ptx_isa` 能唯一匹配（无 ambiguous）
- `inst_desc` 能被 lookup 到（无 `E_DESC_MISS`）

### 1.2 DescriptorRegistry：fail-fast + 新 uop op 解析
目标：消除“未知 op 静默变 MOV”。

改动点：
- `src/instruction/descriptor_registry.cpp`
  - `parse_uop_op`：新增 `FMA`（以及后续的 `MUL/SETP/BRA`），未知字符串直接 `throw` 或返回可定位错误
  - `parse_uop_kind`：未知 kind 同样 fail-fast

验收：
- 用一个故意写错的 `op: "FMM"` 的 inst_desc，程序必须在加载阶段失败并给出错误信息（至少包含 op 字符串）。

### 1.3 Operand 解析：支持 `%fN` 与浮点立即数 `0fXXXXXXXX`
目标：不再要求手改 PTX。

实现位置：
- 推荐放在 `src/instruction/ptx_isa.cpp` 的 `parse_operand_by_kind("reg"/"imm", ...)`（因为映射链路走这个路径）。

任务：
- `reg`：新增 `%fN` 识别：
  - `Operand.kind=Reg`
  - `Operand.type=F32`
  - `Operand.reg_id=N`
  - 存储仍落在 `WarpState.r_u32`（写/读时按 type=F32 走 u32 bank）
- `imm`：支持 `0fXXXXXXXX`：
  - 解析 `XXXXXXXX` 为 32-bit unsigned
  - `Operand.kind=Imm`
  - `Operand.type=F32`
  - `Operand.imm_i64` 存放 bit-pattern（建议仅使用低 32-bit）

验收：
- PTX 中 `mov.f32 %f0, 0f3f800000;` 能成功 tokenize+map（不要求执行正确，先不报 operand parse fail）。

### 1.4 Contracts + ExecCore：新增 `MicroOpOp::Fma` 并实现 FP32 语义
改动点：
- `include/gpusim/contracts.h`
  - 扩展 `enum class MicroOpOp`：新增 `Fma`
- `src/units/exec_core.cpp`
  - 扩展 switch：实现 `MicroOpOp::Fma`

实现要点：
- 当 `uop.attrs.type == ValueType::F32`：
  - inputs 读出 32-bit payload（来自 reg/imm），bitcast 为 `float`
  - 执行 `a*b+c`（float32），结果再 bitcast 回 u32 写回
- 先不追求严格 IEEE rounding 控制（PTX 的 `.rn/.rz/...` flags 暂不支持），但结果必须与 host float32 基本一致。

验收（M1 integration）：
- 写一个最小 kernel：
  - `ld.global.f32` 读 A、B
  - `fma.f32` 计算 `A*B + C`（C 可以用 imm 或第三个 global）
  - `st.global.f32` 写回 out
- host 初始化数据（包含小数/负数），运行后对比输出。

排错指引：
- 若报 `E_LD_GLOBAL_KIND`：检查 `ld` 的 form 是否是 `(reg, addr)`，且 PTX 操作数形态是 `[%rdN(+imm)]`
- 若报 `E_DESC_MISS`：检查 inst_desc 的 `operand_kinds` 顺序是否与解析到的 kind 列表一致

---

## 2. M2：地址/索引算术（让 GEMM 访问正确）

### 2.1 资产：补 `add.u64` 与 `mul` forms
改动文件：
- `assets/ptx_isa/demo_ptx64.json`
- `assets/inst_desc/demo_desc.json`

最小 forms：
- `add.u64 (reg, reg, reg) -> ir_op=add`（uop 复用 `EXEC ADD`）
- `mul.u32 (reg, reg, reg) -> ir_op=mul`（需要新增 uop op `MUL`）

### 2.2 ExecCore：新增 `MicroOpOp::Mul`
改动点：
- `include/gpusim/contracts.h`：新增 `Mul`
- `src/units/exec_core.cpp`：实现 `Mul`

语义（最小）：
- 如果输出 operand.type 是 U64/S64（例如 dst 为 `%rdN` 或 type 被提升）：输出 64-bit 乘积
- 否则输出低 32-bit

验收：
- kernel：`addr = base + tid*4`，读写数组，验证每 lane 正确。

---

## 3. M3：`setp` + predication 生效

### 3.1 `setp` 的 mapping 与 flags 区分（推荐）
新增设计：让 `ptx_isa` entry 可声明 `required_flags[]` 并参与匹配。

改动点：
- `schemas/ptx_isa.schema.json`：增加可选字段 `required_flags: string[]`
- `src/instruction/ptx_isa.cpp`：
  - `PtxIsaEntry` 结构体新增 `required_flags`
  - registry JSON 解析读取该字段
  - `candidates(...)` 或匹配循环里增加 “flags 必须包含 required_flags” 的过滤

最小覆盖：
- `setp.lt.s32`（bounds check）
- `setp.ne.s32`（常用）

### 3.2 ExecCore：新增 `MicroOpOp::Setp` 写 `WarpState.p`
改动点：
- `include/gpusim/contracts.h`：新增 `Setp`
- `src/units/exec_core.cpp`：实现 `Setp`

predicate 存储约定：
- `WarpState.p` 是 reg-major：[pred][lane]，当前初始化为 8 个 pred：`8*warp_size`。

### 3.3 predication 生效：在 SIMT runner 注入 guard
目标：把 `InstRecord.pred` 应用到每个 uop.guard。

改动点：
- `src/simt/simt.cpp`：
  - 在取到 `inst` 与生成 `uops` 后、执行 uops 前，计算 pred mask
  - 把 mask 与每个 `uop.guard` 相与

验收：
- bounds-check store：超界 lane 不写。

---

## 4. M4：最小 `bra`（先 uniform）

### 4.1 Parser：label→pc 绑定 + bra operand 改写
目标：把 `bra label` 在解析期改写成 `bra <imm_pc>`。

改动点：
- `src/frontend/parser.cpp`：
  - pass1：扫描 kernel body，识别 `label:`
  - pass2：tokenize 指令；对 `bra` 的 operand 做改写

注意：当前 tokenize 不识别 label 行为指令，会把 `label:` 当 opcode；需要明确跳过/处理。

### 4.2 PC 更新机制：支持 ControlUnit 覆盖 next_pc
改动点：
- `include/gpusim/contracts.h`：`StepResult` 增加 `next_pc: optional<PC>`
- `src/units/control_unit.cpp`：新增 `BRA` uop（`MicroOpOp::Bra`），设置 `next_pc`
- `src/simt/simt.cpp`：commit 阶段若 `next_pc` 存在则采用，否则 `pc += 1`

uniform 约束：
- 初期仅支持“全 warp 同步分支”。
- 若出现 divergence：
  - 推荐直接返回 Diagnostic（例如 `E_DIVERGENCE_UNSUPPORTED`）。

验收：
- 最小 loop：累加/拷贝，验证循环次数与结果。

---

## 5. 快速排错清单（常见错误码）

- `E_DESC_MISS`（simt）：inst_desc 未匹配到，通常是 `operand_kinds` 顺序/类型不一致
- `DESC_AMBIGUOUS`（instruction）：ptx_isa 中存在多个 entry 都能 parse 成功
- `E_LD_GLOBAL_KIND`（mem）：`ld.global` 走错了 operand kind（不是 addr）
- `E_UOP_UNSUPPORTED`（units）：漏实现 MicroOpOp 分支或 inst_desc 写了未支持的 op

---

## 6. 交付物清单（每个里程碑完成的“可见证据”）

- M1：
  - 资产：demo_ptx64 + demo_desc 增量 entries
  - 代码：`FMA`、`%f`/`0f` operand parse、fail-fast
  - 测试：一个 f32 fma kernel 回归
- M2：
  - 代码：`MUL`、`add.u64` mapping
  - 测试：地址算术回归
- M3：
  - 代码：`SETP`、predication guard 生效
  - 测试：bounds-check 回归
- M4：
  - 代码：label→pc、`BRA`、next_pc 机制
  - 测试：loop 回归

回归测试入口（端到端）
- CTest：`gpu-sim-tiny-gpt2-mincov-tests`
  - fixtures：`tests/fixtures/ptx/m1_fma_ldst_f32.ptx`、`tests/fixtures/ptx/m4_bra_loop_u32.ptx`
  - 运行：`ctest --test-dir build -C Release -V -R "^gpu-sim-tiny-gpt2-mincov-tests$"`
