# tiny GPT-2 最小可跑覆盖矩阵（PTX 6.4 + sm70）

本文档把“跑通 tiny GPT-2（FP32 推理）”拆解成一张可实现的覆盖矩阵：

PTX `opcode/type_mod/operand_kinds` → `ir_op` → `inst_desc uops` → `MicroOpOp` → Units（ExecCore/MemUnit/ControlUnit）

并据此给出 3～5 个**最小里程碑**（先 `ld.global.f32` + `fma.f32`，再逐步补控制流）。

> 重要：当前实现对 form 的匹配 **只看** `ptx_opcode + type_mod + operand_kinds`（不看 flags；space 也不参与匹配）。
> - `space`（`global/param/...`）只影响后续 `MemUnit` 行为（通过 `uop.attrs.space`）。
> - `flags[]`（例如 `wide/lo/hi/rn/...`）目前会被 Parser 记录，但 mapper/expander/units **基本不使用**。

---

## 0. 术语与现状（对齐到代码）

### operand_kinds（冻结枚举）
来自 `schemas/ptx_isa.schema.json`/`include/gpusim/contracts.h`：
- `reg`：目前只识别 `%rN`（U32）与 `%rdN`（U64）
- `pred`：`%pN`
- `imm`：当前仅支持 `std::stoll(..., base=0)` 可解析的整数 token（不支持 `0f...` 浮点立即数）
- `addr`：`[%rdN]` 或 `[%rdN+imm]`
- `symbol`：符号名（含 `[param_name]` 会被规约为 `symbol`）
- `special`：`%tid.x/%ctaid.x/%laneid/...`

### uops（当前可用集合）
`MicroOpKind`：`EXEC/CTRL/MEM`。

`MicroOpOp` 当前只有：
- `MOV` / `ADD` / `LD` / `ST` / `RET`

对应实现：
- `ExecCore`: `MOV/ADD`（当前按整数语义执行；未实现 FP32 算术）
- `MemUnit`: `LD`（param 走 `symbol`；global 走 `addr`）、`ST`（仅 st.global）
- `ControlUnit`: `RET`

---

## 1) 覆盖矩阵（按 GPT-2 bring-up 优先级）

表格里：
- **PTX form**：`ptx_opcode.type_mod` + operand_kinds（只列 mapper 用到的关键信息）
- **ir_op**：建议保持与现有 demo 同风格（例如 `ld`/`st` 复用）
- **uops**：用 `inst_desc` JSON 里的表示（`kind/op/in/out`）
- **状态**：`已有` / `只差资产(JSON)` / `需代码` / `需 Parser/Mapper 扩展`

### A. 当前已可执行（demo 基线）

| PTX form（space 由 mods 决定） | operand_kinds | ir_op | inst_desc uops | Units | 状态 |
|---|---|---|---|---|---|
| `mov.u32` | `(reg, imm)` | `mov` | `EXEC MOV in:[1] out:[0]` | ExecCore | 已有 |
| `mov.u32` | `(reg, special)` | `mov` | `EXEC MOV in:[1] out:[0]` | ExecCore | 已有 |
| `mov.u32` | `(reg, symbol)` | `mov` | `EXEC MOV in:[1] out:[0]` | ExecCore | 已有 |
| `add.u32` | `(reg, reg, reg)` | `add` | `EXEC ADD in:[1,2] out:[0]` | ExecCore | 已有 |
| `ld.u64`（需 `space=param`） | `(reg, symbol)` | `ld` | `MEM LD in:[1] out:[0]` | MemUnit | 已有（ld.param.u64） |
| `st.u32`（需 `space=global`） | `(addr, reg)` | `st` | `MEM ST in:[0,1] out:[]` | MemUnit | 已有（st.global.u32） |
| `ret` | `()` | `ret` | `CTRL RET in:[] out:[]` | ControlUnit | 已有 |

### B. 里程碑 1：FP32 “能算 + 能读写” 的最小闭环

这一步的目标是让一个最小 GEMM/MLP kernel 能跑起来（哪怕很慢/很丑）：
- 能从全局内存读 `f32`
- 能做 `fma.f32`（或 `mul.f32 + add.f32`）
- 能把 `f32` 写回全局内存

> 关键前提：真实 nvcc 产物通常使用 `%f` 寄存器与 `0f...` 浮点立即数；当前实现都不支持。
> 如果不想先改 Parser，可采用“**受约束 PTX**”策略：用 `%r` 保存 f32 bit-pattern、用 `0x3f800000` 这类整数立即数表示 float 常量。

| PTX form | operand_kinds | ir_op | inst_desc uops（建议） | Units | 状态 |
|---|---|---|---|---|---|
| `ld.f32`（`space=global`） | `(reg, addr)` | `ld` | `MEM LD in:[1] out:[0]` | MemUnit | 只差资产(JSON) |
| `st.f32`（`space=global`） | `(addr, reg)` | `st` | `MEM ST in:[0,1] out:[]` | MemUnit | 只差资产(JSON) |
| `mov.f32` | `(reg, reg)` / `(reg, imm)` | `mov` | `EXEC MOV in:[1] out:[0]` | ExecCore | 需代码（浮点立即数/寄存器类）或走受约束 PTX |
| `add.f32` | `(reg, reg, reg)` | `add` | `EXEC ADD in:[1,2] out:[0]` | ExecCore | 需代码（FP32 add） |
| `mul.f32` | `(reg, reg, reg)` | `mul` | `EXEC MUL in:[1,2] out:[0]` | ExecCore | 需代码（新增 MicroOpOp::Mul） |
| `fma.f32` | `(reg, reg, reg, reg)` | `fma` | `EXEC FMA in:[1,2,3] out:[0]` | ExecCore | 需代码（新增 MicroOpOp::Fma） |

### C. 里程碑 2：地址/索引算术（让 GEMM 访问正确）

典型访存地址形态（不依赖 flags 区分 wide/lo）：
- `mul`：输出写到 `%rd`（U64）即可自然表达 “wide”
- `add.u64`：拼 base + byte_offset

| PTX form | operand_kinds | ir_op | inst_desc uops（建议） | Units | 状态 |
|---|---|---|---|---|---|
| `mov.u64`（`special`→64-bit） | `(reg, special)` | `mov` | `EXEC MOV in:[1] out:[0]` | ExecCore | 只差资产(JSON) |
| `add.u64` | `(reg, reg, reg)` | `add` | `EXEC ADD in:[1,2] out:[0]` | ExecCore | 只差资产(JSON)（ExecCore 已支持 64-bit 加法） |
| `mul.u32`（dst 为 `%r`） | `(reg, reg, reg)` | `mul` | `EXEC MUL in:[1,2] out:[0]` | ExecCore | 需代码 |
| `mul.u32`（dst 为 `%rd`） | `(reg, reg, reg)` | `mul` | `EXEC MUL_WIDE in:[1,2] out:[0]` | ExecCore | 需代码（可用同一 `MUL`，按 dst.type 选择 32/64 输出） |

> 注意：当前 mapper 不看 `flags[]`，所以不要依赖 `.lo/.hi/.wide` 来区分语义；推荐用“输出寄存器宽度”承载差异。

### D. 里程碑 3：谓词生效（predication）

GPT-2 相关 kernel 几乎必然有：
- 越界保护（`if (idx < N)`）
- 循环/分支（后续里程碑）

当前问题：`TokenizedPtxInst.pred` 会进入 `InstRecord.pred`，但 `Expander` 总是 `u.guard = all_lanes`，谓词**不生效**。

| PTX form | operand_kinds | ir_op | inst_desc uops（建议） | Units | 状态 |
|---|---|---|---|---|---|
| `setp.*`（例如 lt） | `(pred, reg, reg)` / `(pred, reg, imm)` | `setp` | `EXEC SETP in:[1,2] out:[0]` | ExecCore | 需代码（新增 pred 写入与比较 op） |
| `@%p bra` 之前的 predication | N/A | N/A | N/A | Expander | 需代码（把 `InstRecord.pred` 编译到 `MicroOp.guard`） |

### E. 里程碑 4：控制流（最小循环/分支）

| PTX form | operand_kinds | ir_op | inst_desc uops（建议） | Units | 状态 |
|---|---|---|---|---|---|
| `bra`（目标 label/pc） | `(symbol)`（或约定立即数 pc） | `bra` | `CTRL BRA in:[0] out:[]` | ControlUnit | 需代码（新增 MicroOpOp::Bra + PC 更新；以及 label→pc 绑定） |

> 建议先做最小可用：只支持 forward/backward branch（循环），并明确“无 divergence”或“全 warp 同步分支”。
> 真正的 warp divergence/reconvergence（SIMT stack）可以作为里程碑 5。

### F. 里程碑 5（可选）：reduction / barrier / warp primitives

为了 softmax / layernorm，你最终大概率会需要以下之一：
- `bar.sync` + shared memory reduction
- `shfl.sync`（warp reduce）
- 原子（更慢但实现简单）

这部分超出本文最小闭环，建议等 M1～M4 稳定后再引入。

---

## 2) 3～5 个最小里程碑（建议落地顺序）

### M1：跑通 `ld.global.f32` + `st.global.f32` + `fma.f32`
- `assets/ptx_isa/demo_ptx64.json` 扩展：新增 `ld.f32 (reg, addr)`、`st.f32 (addr, reg)`、以及 `fma.f32 (reg,reg,reg,reg)`（或 `mul/add`）
- `assets/inst_desc/demo_desc.json` 扩展：为上述 `ir_op/type_mod/operand_kinds` 增加 descriptor
- 代码改动（必须）：
  - 扩展 `MicroOpOp`：至少加 `Mul`/`Fma`（如果选择 fma 路线）
  - `DescriptorRegistry` 的 uop op 字符串解析需支持新 op（并建议改成“未知 op 报错”而非默认 MOV）
  - `ExecCore` 实现 FP32 运算（按 IEEE754 float32）
- 输入 PTX 策略：
  - 短期：允许“受约束 PTX”（用 `%r` 表示 f32 位模式；避免 `0f...` immediates）
  - 中期：扩展 `reg` 解析支持 `%fN`（仍可存入 `r_u32`，但类型标记为 `F32`）

### M2：索引/指针算术最小集合
- 新增/扩展：`add.u64`、`mul`（支持 dst=%rd 作为 wide）
- 让 GEMM 的地址计算完全走寄存器/立即数，不依赖 `cvt.u64.u32` 双 type_mod（因为 Parser 目前只保留一个 type_mod）

### M3：谓词真实生效 + `setp`
- `setp` 写 `warp.p`（lane-wise）
- `Expander` 把 `InstRecord.pred` 应用为 `MicroOp.guard`（按 `%p` 和 negation 生成 lane mask）
- 回归：最小 bounds-check kernel（超界 lane 不写）

### M4：最小控制流（循环）
- `bra` + label 绑定（需要在 Parser/Binder 侧提供 label→pc 映射，或简化为“用立即数 pc”）
- 先定义语义约束：
  - 阶段 1：仅支持“全 warp 同步分支”（无 divergence）
  - 阶段 2：再引入 reconvergence（SIMT stack）

### M5（可选）：softmax/LN 所需 reduction 语义
- 选路线：shared + `bar.sync` 或 `shfl.sync`（推荐后者但实现更复杂）
- 如果先求跑通，可用更慢但直观的“global memory + 多 kernel”规约策略（依赖 runtime/workload 能调度多 kernel）

---

## 3) 用这张矩阵怎么推进（实践建议）

1. 从 tiny GPT-2 的 kernel 列表里挑一个最小算子（例如单个 GEMM / linear layer），把 PTX 手工改写到矩阵允许的 form（先走 M1/M2）。
2. 每补一个 form：先补 `ptx_isa` entry，再补 `inst_desc`，最后补 Units。
3. 每个里程碑都配一个最小单元测试（最好是 `tests/integration/` 级别：跑一个极小 kernel 验证输出）。

---

## 附录 A：建议的 JSON entry 模板（用于 M1/M2 bring-up）

> 这些片段用于把“矩阵”直接落到资产文件里：
> - `assets/ptx_isa/demo_ptx64.json`
> - `assets/inst_desc/demo_desc.json`
>
> 注意：在代码尚未支持 `MUL/FMA` 等 uop op 之前，把它们写进 `inst_desc` 可能会被当前解析逻辑错误地回退成 `MOV`；建议先改成“未知 op 直接报错”。

### A1. `ptx_isa`（PTX → IR）

最小新增（示例）：

```json
{
  "ptx_opcode": "ld",
  "type_mod": "f32",
  "operand_kinds": ["reg", "addr"],
  "ir_op": "ld"
}
```

```json
{
  "ptx_opcode": "st",
  "type_mod": "f32",
  "operand_kinds": ["addr", "reg"],
  "ir_op": "st"
}
```

```json
{
  "ptx_opcode": "fma",
  "type_mod": "f32",
  "operand_kinds": ["reg", "reg", "reg", "reg"],
  "ir_op": "fma"
}
```

以及（地址/索引算术常用）：

```json
{
  "ptx_opcode": "add",
  "type_mod": "u64",
  "operand_kinds": ["reg", "reg", "reg"],
  "ir_op": "add"
}
```

```json
{
  "ptx_opcode": "mov",
  "type_mod": "u64",
  "operand_kinds": ["reg", "special"],
  "ir_op": "mov"
}
```

### A2. `inst_desc`（IR → uops）

`ld.global.f32`：

```json
{
  "opcode": "ld",
  "type_mod": "f32",
  "operand_kinds": ["reg", "addr"],
  "uops": [
    { "kind": "MEM", "op": "LD", "in": [1], "out": [0] }
  ]
}
```

`st.global.f32`：

```json
{
  "opcode": "st",
  "type_mod": "f32",
  "operand_kinds": ["addr", "reg"],
  "uops": [
    { "kind": "MEM", "op": "ST", "in": [0, 1], "out": [] }
  ]
}
```

`fma.f32`（需要新增 uop op，例如 `FMA`）：

```json
{
  "opcode": "fma",
  "type_mod": "f32",
  "operand_kinds": ["reg", "reg", "reg", "reg"],
  "uops": [
    { "kind": "EXEC", "op": "FMA", "in": [1, 2, 3], "out": [0] }
  ]
}
```

`add.u64`（ExecCore 现有 `ADD` 可以复用）：

```json
{
  "opcode": "add",
  "type_mod": "u64",
  "operand_kinds": ["reg", "reg", "reg"],
  "uops": [
    { "kind": "EXEC", "op": "ADD", "in": [1, 2], "out": [0] }
  ]
}
```
