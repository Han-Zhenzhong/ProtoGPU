## Plan: PTX8→IR 映射与解码落地

将执行系统的指令语义维持在“原 inst desc（含 uops）”这一层，但把“PTX 文本长什么样”与“执行语义是什么”彻底解耦。

最终必选模式（不再提供“只有 inst desc 描述 PTX 形态”的模式）：
- PTX ISA map（用户选择/提供）：PTX op 形态 → `ir_op`（也即“原 inst desc 的 opcode key”）+ operand_kinds
- inst desc（框架提供/可替换）：`ir_op` → uops（IR 语义库）

代码侧引入一条独立的“PTX→inst_desc(key)/IR op 映射+解析”处理链：Parser 仅做 token 化（不推断 operand kind），随后按 PTX ISA map 把 PTX 指令直接表达为 `InstRecord`（其 opcode 语义为 `ir_op`/inst_desc opcode key），再用 inst desc 展开 uops。这样不需要把 `InstRecordRaw` 作为 Contracts 暴露出来，只是增加了一步独立处理。

### Steps 6
1. 定义两层配置契约与文件布局：新增 PTX ISA map 的 JSON+schema；明确 inst desc 的语义层级（其 opcode 语义为 `ir_op`，作为 IR semantics 数据集）。
2. 明确 InstRecord 的 IR 化策略：决定 `InstRecord.opcode` 是否直接承载 `ir_op`（推荐），以及是否需要额外保留 `ptx_opcode/type_mod`（可放在 dbg/message 或扩展字段，取决于调试需求）。
3. 改造 Parser 为“只 token 化”：调整 [src/frontend/parser.cpp](src/frontend/parser.cpp) 让指令行不再类型化 operands，不再做 operand kind 推断；同时把 `dbg.file` 传递真实 PTX 文件名。
4. 新增/实现 PTX→IR 映射/解析步骤：实现 `PtxIsaRegistry::candidates()` 与 `PtxIsaMapper`，依据 PTX ISA map 决定 operand_kinds 与 `ir_op`（inst_desc opcode key）并解析 operands。
5. 调整 Runtime/CLI 接线：在 [src/runtime/runtime.cpp](src/runtime/runtime.cpp) 中改为 load PTX ISA map + inst_desc(IR semantics) → parse(tokens) → map_to_inst_records(IR) → execute；在 [src/apps/cli/main.cpp](src/apps/cli/main.cpp) 增加 `--ptx-isa`（必需）并保留/重定义 `--inst-desc` 为 IR semantics（必需）。
6. 增补最小可用资产与用例：提供 `assets` 下默认 `ptx8` 映射与默认 inst_desc 语义库，并在 tests 体系里先补 mapper 的关键用例（symbol/addr 歧义、mismatch、ambiguous）。

### Further Considerations 3
1. `ir_op`/inst_desc opcode key 的一致性：PTX ISA map 的 `ir_op` 必须能在 inst desc 里查到语义定义；缺失时应在映射阶段或展开阶段给出可定位诊断。
2. 解析覆盖范围：imm 是否必须支持 `0x..`/浮点立即数；复杂地址表达式是否直接诊断为不支持。
3. 观测与调试：是否需要在 trace/diag 中显式输出 `(ptx_opcode,type_mod)->ir_op` 的映射结果，方便定位配置问题。
