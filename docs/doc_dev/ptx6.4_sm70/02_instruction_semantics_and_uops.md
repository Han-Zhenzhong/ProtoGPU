# 02 Dev：指令语义与 micro-ops（inst_desc 契约落点）

本文对应设计文档 [docs/doc_design/ptx6.4_sm70/02_instruction_semantics_and_uops.md](../../doc_design/ptx6.4_sm70/02_instruction_semantics_and_uops.md)，给出当前代码里 **IR→uops（inst_desc）** 的实现落点、fail-fast 分流点，以及扩展一个新语义时必须同步的检查清单。

单一真源（规格）
- [docs/doc_spec/ptx64_baseline.md](../../doc_spec/ptx64_baseline.md)

---

## 1) 两层资产分离在代码里的落点

- PTX → IR：`assets/ptx_isa/*.json`（实现：`src/instruction/ptx_isa.cpp`）
  - 产物：`InstRecord`（`include/gpusim/contracts.h`）
  - 关键约束：进入 descriptor lookup 前 `InstRecord.opcode` 已经是 IR opcode（而不是原始 PTX opcode）。

- IR → uops：`assets/inst_desc/*.json`（实现：`src/instruction/descriptor_registry.cpp`）
  - 产物：`InstDesc` + `UopTemplate`（`include/gpusim/instruction_desc.h`）
  - expand：`src/instruction/expander.cpp` 生成 `MicroOp`（`include/gpusim/contracts.h`）

---

## 2) micro-op 数据结构（执行器的唯一输入）

定义：`include/gpusim/contracts.h`

- `MicroOp.kind`：`Exec/Control/Mem`
- `MicroOp.op`：`MOV/ADD/MUL/FMA/SETP/LD/ST/BRA/RET`
- `MicroOp.inputs/outputs`：直接复用 `Operand`（reg/pred/imm/addr/symbol/special）
- `MicroOp.attrs`：
  - `type`：来自 `inst.mods.type_mod`（`parse_value_type`）
  - `space`：来自 `inst.mods.space`（expander 内部 `parse_space`）
  - `flags`：来自 `inst.mods.flags`（原样透传）
- `MicroOp.guard`：lane mask（guard 语义见 §5）

执行侧约定（各 unit 都遵守）：
- `exec_mask = warp.active & uop.guard`
- 当 `exec_mask` 为空：uop 语义为 no-op（但仍可能产生观测事件/计数）。

---

## 3) inst_desc JSON 加载（DescriptorRegistry）

实现：`src/instruction/descriptor_registry.cpp`

### 3.1 必须字段与解析路径

`DescriptorRegistry::load_json_text()` 读取 JSON：
- 顶层 `insts: []`
- 每个 entry：
  - `opcode`（IR opcode）
  - `type_mod`（字符串；允许空字符串，见 §4.2）
  - `operand_kinds: ["reg"|"imm"|"addr"|...]`
  - `uops: [{ kind, op, in, out }]`

uop 解析：
- `parse_uop_kind("EXEC"|"CTRL"|"MEM")`：未知值直接 `throw`（加载期 fail-fast）
- `parse_uop_op("MOV"|"ADD"|...)`：未知值直接 `throw`（加载期 fail-fast）

### 3.2 当前实现的“非严格点”（与冻结契约的关系）

设计文档要求“schema 严格、未知字段报错”。当前实现：
- 使用 `obj.at("...")` 强制要求关键字段存在（缺字段会抛异常）。
- 但 **不会** 主动枚举 object keys，因此 **未知字段会被忽略**。

如果要把“未知字段报错”落地：
- 推荐在 `DescriptorRegistry::load_json_text()` 中对 `io` / `uo` 的 key 集合做白名单校验（或引入 JSON schema 校验器，保持与 `schemas/inst_desc.schema.json` 一致）。

---

## 4) descriptor lookup（IR opcode/type_mod/operand_kinds）

实现：`DescriptorRegistry::lookup(const InstRecord&)`

### 4.1 具体匹配逻辑

lookup 会从 `InstRecord.operands[].kind` 推导 `kinds=["reg"|"imm"|...]`，然后线性扫描 `descs_`：
- `d.opcode == inst.opcode`
- `d.type_mod`：
  - 若 `d.type_mod` 非空，则要求 `d.type_mod == inst.mods.type_mod`
- `d.operand_kinds == kinds`

命中后返回 **第一条匹配的** `InstDesc`。

### 4.2 注意点（避免 silent mismatch）

- `type_mod` 允许空字符串等价于“wildcard”。这很方便 bring-up，但会引入“更宽泛的 desc 抢先命中”的风险。
- 当前 lookup 不做“ambiguous 检测”。如果同时存在多条等价可命中条目，会静默选择第一条。

扩展建议（若要更强的 fail-fast）：
- 统计命中条目数：
  - 0 → `E_DESC_MISS`
  - 1 → OK
  - >1 → `E_DESC_AMBIGUOUS`

（如果你要引入这种更强约束，记得把行为变化纳入 Tier‑0 回归的期望。）

---

## 5) expand：从 InstDesc 生成 MicroOp（attrs + operand 选择 + guard 初始化）

实现：`src/instruction/expander.cpp`

expand 行为：
- `u.attrs.type = parse_value_type(inst.mods.type_mod)`
- `u.attrs.space = parse_space(inst.mods.space)`
- `u.attrs.flags = inst.mods.flags`（透传）
- `u.guard = lane_mask_all(warp_size)`（默认全 lane 开）
- `inputs/outputs` 由 `UopTemplate.in/out` 的 operand index 选择 `inst.operands[idx]`

当前实现对 index 的处理：
- `idx` 越界会被 `continue` 忽略（不会 fail-fast）。

如果你想把它变成“资产错误必须立刻报错”：
- 在 expand 内检测越界并返回诊断（或直接抛异常），并把 `inst.sig + desc.sig + idx` 写进 message。

---

## 6) predication → guard 注入（lane-wise 语义）

设计文档口径：predication 转为 `MicroOp.guard`。

当前实现位置：`src/simt/simt.cpp`（注意：在 expand 之后统一处理）
- `auto uops = expander_.expand(inst, *desc, warp_size);`
- 若 `inst.pred` 存在：
  - 从 `warp.p[pred_id][lane]` 构造 `pred_mask`
  - `u.guard = lane_mask_and(u.guard, pred_mask)` 对每个 uop 生效

执行侧落点：
- Exec：`src/units/exec_core.cpp`
- Mem：`src/units/mem_unit.cpp`
- Ctrl：`src/units/control_unit.cpp`

三者都使用同一模式：`exec_mask = lane_mask_and(warp.active, uop.guard)`。

---

## 7) fail-fast 分流点（你应该在哪里看到错误）

加载期（inst_desc JSON）
- 未知 `kind/op`：`DescriptorRegistry` 直接抛异常（加载失败）
- 缺字段：JSON 访问 `at()` 抛异常（加载失败）

运行期（SIMT 执行）
- desc miss：`src/simt/simt.cpp` 生成 `Diagnostic{ module="simt", code="E_DESC_MISS" }`
- predicate 越界：`Diagnostic{ module="simt", code="E_PRED_OOB" }`
- divergent control-flow（M4 uniform-only）：`Diagnostic{ module="simt", code="E_DIVERGENCE_UNSUPPORTED" }`
- 多个控制 uop 同时设置 next_pc：`Diagnostic{ module="simt", code="E_NEXT_PC_CONFLICT" }`

uop 层
- arity 不匹配：各 unit 返回 `E_UOP_ARITY`
- 未支持的 uop：各 unit 返回 `E_UOP_UNSUPPORTED`

---

## 8) 扩展一个新语义（IR→uops）的最小步骤

1. 先确保 mapper 能把 PTX form 映射到稳定的 IR opcode（`assets/ptx_isa/*.json`）。
2. 在 `assets/inst_desc/*.json` 增加对应 `InstDesc`：
   - `opcode/type_mod/operand_kinds` 与运行时的 `InstRecord` 严格一致
   - `uops` 的 `in/out` index 对齐 `InstRecord.operands` 顺序
3. 若需要新的 `MicroOpOp`：
   - 更新 `include/gpusim/contracts.h` 的 `MicroOpOp` 枚举
   - 更新 `src/instruction/descriptor_registry.cpp` 的 `parse_uop_op()`
   - 在对应 unit（exec/mem/control）实现语义并加 fail-fast 分支
4. 把 fixture + 回归并入 Tier‑0（默认推荐），避免“资产支持了但执行语义漂移”。

验收入口（Tier‑0）：

```bash
ctest --test-dir build -C Release -V -R "^gpu-sim-tiny-gpt2-mincov-tests$"
```
