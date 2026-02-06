# 03 Instruction System（实现落地：DescriptorRegistry + MicroOpExpander）

参照
- 抽象设计：`doc_design/modules/03_instruction_system.md`
- Contracts：`doc_dev/modules/00_contracts.md`

落地目标
- DescriptorRegistry 只接受 JSON descriptors，并进行 schema 校验。
- lookup 规则 deterministic；expand 输出序列稳定且携带 guard/attrs。

落地位置（代码）
- `src/instruction/`
  - descriptor_registry
  - microop_expander

数据目录约定
- descriptors（运行输入）：`assets/inst_desc/` 或由 CLI 指定目录。
- schema（仓库内固定）：`schemas/inst_desc.schema.json`。

---

## 1. Schema 校验

要求
- 加载 descriptors 时必须 schema 校验：
  - 缺字段/类型错误/未知字段 → 失败并返回 Diagnostic（code=`DESC_SCHEMA_INVALID`）。
- schema 文件路径固定为 `schemas/inst_desc.schema.json`。

---

## 2. lookup API 与索引

API
```text
load_descriptor_dir(path) -> void
lookup(inst_record: InstRecord) -> InstDesc
```

索引结构
- 以 key 维度建立多级索引：opcode → type_mod → mods → operand_kinds。

匹配优先级
- 完全匹配优先于通配。
- 多个匹配结果 → 失败并返回 Diagnostic（code=`DESC_AMBIGUOUS`）。
- 无匹配结果 → 失败并返回 Diagnostic（code=`DESC_NOT_FOUND`）。

---

## 3. Expander API

API
```text
expand(inst_record: InstRecord, inst_desc: InstDesc, warp_active_mask: LaneMask) -> vector<MicroOp>
```

guard 生成
- `MicroOp.guard` 必须体现 predication 与 active mask 的组合规则。

复杂指令拆分
- 内存类：addr_calc（如需要）+ ld/st/atom。
- 同步类：fence/barrier 产生对应 uop，携带 scope/semantics。

观测
- lookup 与 expand 的统计通过 ObsControl 上报。
