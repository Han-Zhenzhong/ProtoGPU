# 03 Instruction System（DescriptorRegistry + MicroOpExpander）

说明
- 本文只描述抽象逻辑：JSON descriptor、匹配规则、类型规则与 micro-op 展开。
- 设计基准：与 [doc_design/arch_modules_block.diagram.puml](../arch_modules_block.diagram.puml) 对齐；若文本与图冲突，以图为准。

---

## 职责（Responsibilities）
- 从 JSON 指令描述文件构建 `InstDesc` 查询服务（DescriptorRegistry）。
- 将 `InstRecord + InstDesc` 展开为有序 `MicroOp[]`（MicroOpExpander）。
- 固化：匹配优先级、类型推断/强制、predication guard 与复杂指令拆分规则。

---

## 输入/输出（Inputs/Outputs）
- 输入：
  - `InstRecord`（来自 Frontend/Binder）
  - JSON descriptors（外部资产）
- 输出：
  - `InstDesc`（lookup 结果）
  - `MicroOp[]`（expand 结果，含 `guard` 与 `attrs`）

---

## 内部执行流程（Internal Flow）

### DescriptorRegistry.load
```text
load(json_files)
  -> validate against schema
  -> normalize keys (canonical opcode/mod ordering)
  -> build index structures
```

Schema 约束（概念）
- `key`：`opcode/type_mod/mods/operand_kinds`。
- `value`：语义 flags + uop_templates。
- 必填字段完整；禁止未知字段（避免 silent mismatch）。

### DescriptorRegistry.lookup
匹配输入 `InstRecord` 的顺序：
1) opcode canonicalization（例如大小写与别名归一）
2) type_mod 匹配（完全匹配优先，其次显式通配）
3) mods 匹配（space/rounding/sat/volatile 等）
4) operand_kinds 匹配（dst/src/addr 的形态）

输出
- 唯一 `InstDesc`，否则产生 `Diagnostic`（unknown/ambiguous descriptor）。

### MicroOpExpander.expand
```text
expand(inst, desc)
  -> derive guard from inst.pred and current warp mask
  -> normalize operands (types, widths)
  -> emit uops according to templates
  -> attach attrs (type/space/flags) and guard to each uop
```

展开规则基线
- predication：`@p` / `@!p` 转为 `MicroOp.guard`。
- mem 指令：拆分为 `ADDR_CALC`（如需要）+ `LD/ST/ATOM`。
- cvt/pack：必要时拆为 `CVT` + `CLAMP` + `WRITEBACK`。
- barrier/fence：产生 `BAR` / `FENCE` uop，并携带 scope/semantics flags。

---

## 与其它模块的数据/流程交互（Interfaces + Events）

接口（概念契约）
```text
DescriptorRegistry.load(files) -> void
DescriptorRegistry.lookup(inst: InstRecord) -> InstDesc
MicroOpExpander.expand(inst: InstRecord, desc: InstDesc) -> vector<MicroOp>
```

观测点
- lookup 命中/未命中/歧义：`FETCH` 或 `EXEC` 类事件（action=desc_hit/desc_miss/desc_ambiguous）。
- expand 输出数量与类别分布：`EXEC`（action=expand, extra={uop_count, kinds...}）。

---

## 不变量与边界条件（Invariants / Error handling）
- Descriptor 文件格式固定为 JSON；加载时必须 schema 校验通过。
- lookup 必须 deterministic：同一 `InstRecord` 在同一 descriptor 集合下返回同一 `InstDesc`。
- expand 输出的 uop 序列顺序稳定，满足 Units 的执行期假设。

---

## 验收用例（Smoke tests / Golden traces）
- 对纳入范围的指令类别（算术/逻辑/比较/转换/分支/访存/atomic/fence/barrier）均可 lookup 并展开为稳定 uops。
- 对未知指令或不一致 mods 产生明确 Diagnostic 与观测事件。
