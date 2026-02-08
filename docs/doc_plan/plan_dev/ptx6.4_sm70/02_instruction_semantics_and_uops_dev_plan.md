# 02 Instruction Semantics & Micro-ops Dev Plan（inst_desc）

目标：冻结 IR→uops（inst_desc）的资产契约与 fail-fast 分流点，使“新增一个语义”时能同步更新 assets/registry/expander/units，并且错误在正确阶段暴露（加载期 vs 运行期），同时保持 Tier‑0 gate 稳定。

单一真源
- 实现落点与检查清单：[docs/doc_dev/ptx6.4_sm70/02_instruction_semantics_and_uops.md](../../../doc_dev/ptx6.4_sm70/02_instruction_semantics_and_uops.md)
- Tier‑0 forms 与冻结约束（M1–M4）：[docs/doc_spec/ptx64_baseline.md](../../../doc_spec/ptx64_baseline.md)

---

## 1) 冻结 IR→uops 执行契约（执行器唯一输入）

- MicroOp/Operand 是执行器唯一输入：`include/gpusim/contracts.h`
- 执行侧统一约定：`exec_mask = warp.active & uop.guard`
- `exec_mask` 为空：该 uop 语义为 no-op（允许产生观测事件/计数，但不得产生寄存器/内存副作用）

落点（执行侧）
- Exec：`src/units/exec_core.cpp`
- Mem：`src/units/mem_unit.cpp`
- Ctrl：`src/units/control_unit.cpp`

---

## 2) inst_desc 资产与 schema（bring-up 决策点）

资产
- IR → uops：`assets/inst_desc/*.json`
- schema：`schemas/inst_desc.schema.json`

约束（计划内要明确并在 review 中检查）
- 必填字段：`opcode/type_mod/operand_kinds/uops` 与 uop 的 `kind/op/in/out`
- `type_mod==""` 的 wildcard 策略：允许 bring-up 更快，但必须意识到“更泛 desc 抢先命中”的风险
- bring-up 期严格度决策：是否要开启“未知字段报错”
	- 当前默认：非严格（未知 JSON key 会被忽略）
	- 可选增强：在 loader 启用 strict-key 校验（未知 key 直接加载期 fail-fast）

---

## 3) 加载期 fail-fast（DescriptorRegistry）

加载入口
- `src/instruction/descriptor_registry.cpp`
	- `DescriptorRegistry::load_json_text()`
	- `DescriptorRegistry::load_json_file()`

加载期必须 fail-fast 的错误（资产错误必须阻断执行）
- 缺失关键字段（`at()` 访问失败抛异常）
- 未知 `uop.kind` / `uop.op`（解析函数直接抛异常）
-（可选）未知 JSON key（strict-key 模式下抛异常）

错误信息建议格式（便于定位到具体 desc/uop）
- 必须包含：`insts[i].uops[j]` 上下文（已实现）
- 建议包含：opcode/type_mod/operand_kinds（后续增强点）

落点
- `src/instruction/descriptor_registry.cpp`

---

## 4) descriptor lookup（opcode/type_mod/operand_kinds）

匹配键（冻结）
- `opcode + (type_mod optional) + operand_kinds`
- `operand_kinds` 是严格序列（顺序必须一致）
- `type_mod==""` 等价 wildcard；`type_mod!=""` 则必须与 `inst.mods.type_mod` 严格相等

可选增强（若启用必须纳入回归期望）
- ambiguous 检测：
	- 0 命中：运行期 `E_DESC_MISS`
	- 1 命中：OK
	- >1 命中：新增 `E_DESC_AMBIGUOUS`（或加载期直接拒绝重复/等价条目）

落点
- `src/instruction/descriptor_registry.cpp`：`DescriptorRegistry::lookup()`

---

## 5) expand（InstDesc → MicroOp[]）

固定语义（必须与实现一致）
- `attrs.type = parse_value_type(inst.mods.type_mod)`
- `attrs.space = parse_space(inst.mods.space)`
- `attrs.flags = inst.mods.flags`（透传）
- `guard = lane_mask_all(warp_size)`（默认全 lane 打开，后续由 SIMT 注入 predication guard）

边界行为（bring-up 现状与可选增强）
- `UopTemplate.in/out` 索引越界：当前实现静默忽略（continue）
	- 可选增强：升级为 fail-fast（诊断或异常），并输出 `inst.sig + desc.sig + idx`

落点
- `src/instruction/expander.cpp`

---

## 6) SIMT wiring 与运行期 fail-fast 分流

predication → guard 注入
- 发生在 SIMT（expand 之后统一处理）：`src/simt/simt.cpp`
- 行为：`u.guard = u.guard & pred_mask`（对每个 uop 生效）

运行期 fail-fast 分流点（必须能从症状回溯到代码点）
- `E_DESC_MISS`：desc miss
- `E_PRED_OOB`：predicate 越界
- `E_DIVERGENCE_UNSUPPORTED`：uniform-only 模式不允许 divergence
- `E_NEXT_PC_CONFLICT`：多个控制 uop 同时设置 next_pc

落点
- `src/simt/simt.cpp`

执行侧 uop fail-fast
- `E_UOP_ARITY` / `E_UOP_UNSUPPORTED`
	- Exec：`src/units/exec_core.cpp`
	- Mem：`src/units/mem_unit.cpp`
	- Ctrl：`src/units/control_unit.cpp`

---

## 7) “新增一个语义（IR→uops）”最小交付清单

每新增/修改一个可执行 form，默认要求形成“四件套”（与 Tier‑0 gate 对齐）
1. `assets/ptx_isa/*.json`（PTX → IR InstRecord）
2. `assets/inst_desc/*.json`（IR → uops InstDesc）
3. `tests/fixtures/ptx/*.ptx`（锁定写法）
4. 回归覆盖：优先纳入 Tier‑0（或明确记录例外与解除条件）

若新增了 MicroOpOp（执行语义扩展）
- 更新 `include/gpusim/contracts.h` 的 `MicroOpOp`
- 更新 `src/instruction/descriptor_registry.cpp` 的 `parse_uop_op()`
- 在对应 unit 增加执行语义与 fail-fast 分支

---

## 8) 验收（测试入口）

- 默认 merge gate：`gpu-sim-tiny-gpt2-mincov-tests`

只跑 Tier‑0：

```bash
ctest --test-dir build -C Release -V -R "^gpu-sim-tiny-gpt2-mincov-tests$"
```

单元测试（包含 inst_desc loader 相关回归）：

```bash
ctest --test-dir build -C Release -V -R "^gpu-sim-(tests|inst-desc-tests|builtins-tests|config-parse-tests|tiny-gpt2-mincov-tests)$"
```
