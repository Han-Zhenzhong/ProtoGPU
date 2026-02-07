# Design: tiny GPT-2 最小可跑覆盖（PTX 6.4 + sm70）

本文档是对 `docs/doc_spec/tiny_gpt2_minimal_coverage_matrix.md` 的**实现设计**：把覆盖矩阵与里程碑（M1–M4）落到具体模块改动点、接口契约与回归策略。

关联文档：
- Spec（需求/覆盖矩阵）：`docs/doc_spec/tiny_gpt2_minimal_coverage_matrix.md`
- 基线：`docs/doc_spec/ptx64_baseline.md`、`docs/doc_spec/sm70_profile.md`
- 实施计划（高层步骤）：`docs/doc_plan/plan_design/plan-tinyGpt2MinimalCoverage.prompt.md`

---

## 1. 目标与非目标

### 1.1 目标（M1–M4）
- M1：`ld.global.f32` + `st.global.f32` + `fma.f32`（或 `mul+add`）闭环，能跑一个最小 GEMM/MLP kernel。
- M2：最小地址/索引算术（`add.u64` + `mul` wide），保证 GEMM 访存正确。
- M3：`setp` + predication 生效（越界 lane 不写）。
- M4：最小 `bra`（循环/分支），先支持 **uniform control flow**，并为后续 SIMT reconvergence 预留接口。

### 1.2 非目标（本阶段不承诺）
- 完整 PTX 6.4 指令集覆盖与严格验证（flags 的完整语义解释）。
- cycle-accurate SM70。
- 完整 divergence/reconvergence（SIMT stack）实现（仅在 M4 里预留接口）。

---

## 2. 当前实现的关键约束（必须显式处理）

### 2.1 form 匹配机制
- 前端 tokenization 产出：`TokenizedPtxInst{ ptx_opcode, mods{type_mod, space, flags}, operand_tokens, pred }`。
- 映射匹配只看：`ptx_opcode + type_mod + operand_kinds`。
  - `mods.space` **不参与匹配**，仅在后续 `Expander` 里写到 `uop.attrs.space`，影响 `MemUnit`。
  - `mods.flags[]` 目前基本不参与执行语义。

> 结论：不能依赖 `.wide/.lo/.hi/.rn/.ftz/...` 等 flags 来区分 mapping；要么扩展映射规则，要么用其它维度承载差异。

### 2.2 operand 解析限制（当前）
- `reg`：仅 `%rN`（U32）与 `%rdN`（U64）；不支持 `%fN`。
- `imm`：仅整数（`std::stoll` base=0）；不支持 `0f3f800000` 这类浮点立即数。

### 2.3 inst_desc 风险：未知 uop op 会静默降级
- `DescriptorRegistry` 当前 `parse_uop_op` 对未知字符串返回 `MOV`（silent fallback）。

> 结论：bring-up 阶段必须 fail-fast，否则资产/设计错误会变成“错误执行且难以定位”。

---

## 3. 总体设计原则

- **先通路后优化**：优先保证“能映射、能执行、能验证”。
- **fail-fast**：JSON/映射/descriptor 错误要在加载或 lookup 阶段明确报错。
- **保持数据驱动**：尽量用 `ptx_isa` + `inst_desc` 承载“新增指令 form”，代码层只扩展必要的 operand 解析与 MicroOp 执行。
- **逐步引入复杂语义**：控制流先 uniform；predication 先 lane-mask；reconvergence 延后。

---

## 4. 设计决策（本设计选择）

### 4.1 PTX 输入策略
本设计选择：**直接支持 nvcc 风格 `%fN` 与浮点立即数（至少 `0fXXXXXXXX` bit-pattern）**。
- 目标：减少“手改 PTX/受约束 PTX”的工程成本与不确定性。

### 4.2 flags 匹配策略
- 通用规则：暂不让 flags 参与 mapping。
- 例外（可选，但推荐用于 M3 的 setp）：为 `ptx_isa` 增加可选 `required_flags[]`，仅在 entry 声明时参与匹配。
  - 原因：`setp.lt.s32` / `setp.ne.s32` 等需要区分 compare op，否则无法正确表达 bounds-check。

### 4.3 FP32 的存储与执行语义
- `%fN` 寄存器值以 **lane-wise 32-bit payload** 存在 `WarpState.r_u32` 中（与 U32 同 bank），但 operand/uop 的 `ValueType` 标记为 `F32`。
- 执行阶段以 `uop.attrs.type == F32` 决定按 float32 解释/运算（bitcast）。

---

## 5. 里程碑设计细化

下面按 M1–M4 给出具体改动点与验收。

### M1：`ld/st.global.f32` + `fma.f32` 闭环

#### M1.1 映射与 descriptor 资产
- `assets/ptx_isa/demo_ptx64.json`：新增
  - `ld.f32 (reg, addr) -> ir_op=ld`
  - `st.f32 (addr, reg) -> ir_op=st`
  - `fma.f32 (reg, reg, reg, reg) -> ir_op=fma`
- `assets/inst_desc/demo_desc.json`：新增
  - `ld.f32 (reg, addr)`: `MEM LD in:[1] out:[0]`
  - `st.f32 (addr, reg)`: `MEM ST in:[0,1] out:[]`
  - `fma.f32 (...)`: `EXEC FMA in:[1,2,3] out:[0]`

#### M1.2 代码改动点
- Frontend operand parse：
  - `%fN` 解析进 `OperandKind::Reg`，`Operand.type=F32`。
  - `imm` 支持 `0fXXXXXXXX`（8 hex digits），解析为 32-bit bit-pattern，`Operand.type=F32`。
- DescriptorRegistry：
  - `parse_uop_op` 与 `parse_uop_kind` 对未知值 **直接报错**（exception 或返回带定位信息的 Diagnostic），禁止 silent fallback。
- MicroOpOp 扩展：
  - 新增 `Fma`（以及可选 `Mul`）。
- ExecCore：
  - 新增 `FMA` 执行路径：对 inputs 做 `uint32->float` bitcast，执行 `a*b+c`（float32），写回 `uint32` bit-pattern。

#### M1.3 验收（最小回归）
- 新增 integration test kernel：
  - global 读两个 f32，做一次 fma，写回 f32。
  - 以 host 侧初始化输入、比对输出（允许 1 ulp 或相对误差阈值）。

---

### M2：地址/索引算术最小集

#### M2.1 指令子集
- `add.u64`：用于 `base + byte_offset`。
- `mul`：用于 `idx * stride_bytes`（支持 wide 输出到 `%rd`）。

#### M2.2 语义承载方式（避免依赖 flags）
- 不依赖 `.wide/.lo/.hi`：
  - 如果 dst 是 64-bit reg（`%rdN` 或 `%f`? 不涉及），则输出 64-bit 结果。
  - 如果 dst 是 32-bit reg（`%rN`），则输出低 32-bit。

#### M2.3 代码改动点
- MicroOpOp 新增 `Mul`（若 M1 选择 `mul+add` 则已需要）。
- ExecCore：
  - `MUL` 的整数实现：按 `uop.attrs.type` 决定 32/64（`U32/U64/S32/S64`），并按 dst operand.type 决定写回 bank。

#### M2.4 验收
- kernel：计算 address = base + (tid * 4)，读写数组，验证每 lane 访问正确。

---

### M3：`setp` + predication 生效

#### M3.1 setp 的 compare 选择
- 推荐：`ptx_isa` 支持可选 `required_flags`，以区分 `setp.lt` / `setp.ne` 等。
  - 例：`ptx_opcode=setp`，`required_flags=["lt"]`，`type_mod=s32`。
- 最小实现先支持：`lt`（bounds check）与 `ne`（常见 mask）。

#### M3.2 predication 生效机制（guard）
现状：`InstRecord.pred` 存在，但 `Expander` 生成的 `MicroOp.guard` 全 lane。

设计：在 `SimtExecutor` 执行每条 inst 前，计算 `pred_mask` 并与每个 uop.guard 相与。
- 输入：`inst.pred{pred_id,is_negated}` + `warp.p`（lane-wise predicate bank）。
- 输出：`MicroOp.guard`（LaneMask）。

这样不需要让 `Expander` 访问 warp state，保持 `Expander` 纯函数属性。

#### M3.3 代码改动点
- MicroOpOp 新增 `Setp`。
- ExecCore 新增 `SETP`：
  - lane-wise 比较，写入 `warp.p[pred_id][lane]`（`WarpState.p` 的布局需定义清楚，建议 reg-major：[pred][lane]）。
- `PtxIsaEntry`/schema 可选扩展 `required_flags[]`（如果采用）。

#### M3.4 验收
- bounds-check store：
  - `if (tid < N) st.global...`，验证超界 lane 不写。

---

### M4：最小 `bra`（uniform control flow）

#### M4.1 label→pc 绑定设计
为减少执行期复杂度，本阶段在 Parser 内做两遍扫描：
1) pass1：识别 `label:` 行，建立 `label -> pc`（pc = inst index）。
2) pass2：tokenize inst；遇到 `bra label` 时，把 operand token 改写为 `imm(pc)`。

这样：
- `ptx_isa` 中 `bra` form 可采用 `(imm)`；
- ControlUnit 只处理跳转到立即数 pc。

#### M4.2 PC 更新机制
现状：SIMT 主循环在 inst 完成后无条件 `pc += 1`。

设计：为 Control micro-op 提供“覆盖 next pc”的返回通道：
- 在 `StepResult` 增加 `next_pc`（optional）。
- `ControlUnit` 执行 `BRA` 时设置 `next_pc`。
- `SimtExecutor` 在 commit 阶段若存在 `next_pc` 则采用，否则 `pc += 1`。

#### M4.3 uniform 约束与后续扩展点
- 本阶段仅支持 uniform 分支：
  - `@%p bra` 允许，但要求 `%p` 对 warp 内所有 active lanes 值一致（否则报错或进入未定义行为）。
- 为后续 reconvergence 预留：
  - 在 `StepResult`/执行框架里把“分支事件”作为可观察点（event/obs），未来引入 SIMT stack 时可复用。

#### M4.4 验收
- 最小 loop：累加或拷贝，验证循环次数与结果正确。

---

## 6. 需要修改/新增的文件清单（设计级）

### 6.1 资产（data-driven）
- `assets/ptx_isa/demo_ptx64.json`
- `assets/inst_desc/demo_desc.json`

### 6.2 Schema（如采用 required_flags）
- `schemas/ptx_isa.schema.json`：为 entry 增加可选 `required_flags: string[]`。

### 6.3 代码模块
- Frontend：`src/frontend/parser.cpp`（label 扫描；浮点立即数 token 支持取决于实现位置）
- PTX ISA mapping：`src/instruction/ptx_isa.cpp`（`%f` reg、`0f...` imm；可选 required_flags 匹配）
- Descriptor：`src/instruction/descriptor_registry.cpp`（fail-fast；新增 op 字符串）
- Contracts：`include/gpusim/contracts.h`（扩展 MicroOpOp；扩展 StepResult next_pc）
- Units：`src/units/exec_core.cpp`、`src/units/control_unit.cpp`、`src/units/mem_unit.cpp`（FMA/MUL/SETP/BRA）
- SIMT runner：`src/simt/simt.cpp`（predication guard 注入；pc 更新逻辑）

---

## 7. 测试策略

- 每个里程碑至少 1 个可重复的最小回归：
  - M1：f32 load + fma + store（数值对齐）
  - M2：地址算术正确性（每 lane 访问）
  - M3：bounds-check（超界 lane 不写）
  - M4：loop（pc 跳转正确）
- 推荐把这些回归做成 integration tests（走 runtime/CLI 同一路径），避免 unit test 与真实执行链路脱节。

---

## 8. Open Questions（需要你确认的口径）

1) `setp` 最小 compare 集合：仅 `lt` 够不够？是否需要 `le/ne/eq`？
2) `bra` 是否必须支持 predicated branch（`@%p bra`）？如果支持，uniform 约束是否可接受？
3) 浮点立即数：只支持 `0fXXXXXXXX` bit-pattern（nvcc 常见）是否足够？是否也要支持 `1.0` 这类十进制 float token？
