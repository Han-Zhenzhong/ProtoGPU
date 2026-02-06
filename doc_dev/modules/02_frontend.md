# 02 Frontend（实现落地：Parser/Binder/ModuleBuilder）

参照
- 抽象设计：`doc_design/modules/02_frontend.md`
- Contracts：`doc_dev/modules/00_contracts.md`

落地目标
- 提供可被 Runtime 调用的“解析并构建 Module”的实现入口。
- Parser 以 streaming 方式工作；Binder 完成所有符号与 label 绑定。
- 必须实现 PtxIsaMapper：PTX 指令按 PTX ISA map 映射并解析为 IR `InstRecord`（`opcode==ir_op`），再交由 inst desc（IR semantics）展开 uops。

落地位置（代码）
- `src/frontend/`
  - parser
  - binder
  - module_builder

相关实现文档
- `doc_dev/modules/02.01_frontend_desc_driven_decode.md`

---

## 1. 对外入口

模块入口
```text
parse_and_build(ptx_text_or_stream) -> Module
```

输出
- 成功：`Module`（含 `Function.insts` 与 `label_index`）。
- 失败：`Diagnostic`（带 SourceLocation 与 inst_index）。

---

## 2. Parser 落地约束

Streaming 形态
- Parser 输出 `ParsedItem` 流，Binder 逐项消费。

必须携带的 dbg
- 每个指令记录必须携带 `SourceLocation`，并在 Binder 生成 `inst_index` 时保持可追溯。

错误处理
- 解析错误立即返回 Diagnostic，且错误位置指向输入流位置。

---

## 3. Binder 落地约束

符号表结构
- reg/pred/param/shared/global/const/label 分表。
- label 绑定规则：label 指向“下一条指令”的 InstList index。

operand 绑定
- 将 symbol 与 label 引用解析为内部 id/pc。
- 绑定后的 InstRecord 禁止包含未解析的字符串引用（除 debug 字段外）。

基础校验
- operand kinds 与 mods.space 的组合合法。
- 立即数宽度与 type_mod 一致。

---

## 4. Module/Function 数据结构落地

Module
- `functions: map<string, Function>`
- `metadata: version/target/address_size`

Function
- `insts: vector<InstRecord>`
- `label_index: map<string, PC>`
- `layouts: regs/params/shared/local`

约束
- PC 统一为 InstList index。
