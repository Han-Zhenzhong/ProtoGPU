# 02 Frontend（Parser → Binder → ModuleBuilder）

说明
- 本文只描述抽象逻辑：解析范围、流式输出、绑定与错误诊断。
- 设计基准：与 [doc_design/arch_modules_block.diagram.puml](../arch_modules_block.diagram.puml) 对齐；若文本与图冲突，以图为准。

---

## 职责（Responsibilities）
- 将 PTX 文本以 streaming 方式转为 `Module/Function/InstList` 与必要 metadata。
- 在 Binder 阶段完成符号与 label 的绑定，生成 `label_index` 与布局信息。
- 对输入进行基础一致性校验，并输出统一 `Diagnostic`。

---

## 输入/输出（Inputs/Outputs）

### 输入
- PTX 文本（string 或 stream）。

### 输出
- `Module`
  - `functions[name] -> Function { insts, label_index, layouts }`
  - `globals/consts/metadata`
- 错误：`Diagnostic`（可定位 file/line/column 或 inst_index）。

---

## 内部执行流程（Internal Flow）

### Parser（Streaming）
Parser 将输入转为“指令/指令外声明”的事件流：

```text
ParsedItem := Directive | FunctionBegin | FunctionEnd | TokenizedPtxInst

parse(ptx_stream) -> iterator<ParsedItem>
```

解析范围（与架构文档对齐的基线）
- directives：`.version/.target/.address_size/.entry/.func/.reg/.param/.shared/.global/.const`。
- 指令行：`opcode + modifiers + operands + predication`。

输出要求
- `TokenizedPtxInst` 必须携带 `dbg`（行号/文件）以便错误定位。

### PtxIsaMapper（PTX→IR 映射/解析，必须）
为满足“PTX op → inst desc(opcode key/ir_op) 映射是唯一必选模式”的需求，必须引入 PtxIsaMapper 阶段：

- 输入：`TokenizedPtxInst + PtxIsaRegistry(PTX ISA map)`
- 输出：`InstRecord`（其中 `InstRecord.opcode == ir_op`，operands.kind/type 已确定），或 `Diagnostic`

详见设计文档：[doc_design/modules/02.01_frontend_desc_driven_decode.md](02.01_frontend_desc_driven_decode.md)

### Binder（Directive/Metadata Binder）
Binder 消费 `ParsedItem`，构建 function-local 与 module-level 的布局与符号：

```text
SymbolTable {
  regs: map<name, RegId+Type>
  preds: map<name, PredId>
  params: map<name, {offset,size,type}>
  shared: map<name, {offset,size,align}>
  globals/consts: module-level symbols
  labels: map<label, PC>
}
```

关键规则
- PC/index：按 InstList 的 push 顺序递增，PC 即 index。
- label 绑定：遇到 label 定义时绑定到“下一条指令”的 index。
- operand 绑定：将 `SYMBOL/LABEL` 引用转换为内部 `symbol_id/pc`，避免执行期字符串解析。

基础校验（产生 Diagnostic）
- 操作数种类与空间合法性（reg/pred/imm/addr/symbol）。
- 立即数范围与宽度一致性（由 type_mod 约束）。
- 地址空间声明与操作一致（例如 shared 只能在 shared 空间语义下使用）。

### ModuleBuilder
ModuleBuilder 汇总 Binder 的中间产物，生成最终 `Module`：

```text
Module {
  functions: map<string, Function>
  globals/consts
  metadata
}

Function {
  insts: vector<InstRecord>
  label_index: map<label, PC>
  layouts: { regs, params, shared, local }
}
```

---

## 与其它模块的数据/流程交互（Interfaces + Events）

### 对 Runtime 的交付物
- `Module`：由 Runtime 持有并用于 entry lookup、param packing 以及执行期取指（InstList）。

### 观测点
- Parser/Binder/Builder 在关键点通过 `ObsControl.emit()` 上报：
  - `STREAM`：module_load / parse_begin / parse_end
  - `STREAM`：bind_begin / bind_end
  - `STREAM`：diagnostic（错误或告警）

---

## 不变量与边界条件（Invariants / Error handling）
- `Function.insts` 的顺序与 PC/index 规则稳定，跨模块一致。
- `label_index` 只依赖 Binder 的绑定规则，执行期不做字符串解析。
- 任何错误输出必须带 `SourceLocation`，并可附带 `inst_index`。

---

## 验收用例（Smoke tests / Golden traces）
- 给定 PTX，输出 `Module/Function/InstList/label_index`，并能通过 `inst_index` 精确定位错误行。
- 解析与绑定过程中产出观测事件，Trace 可用于复盘“解析到哪一行失败”。
