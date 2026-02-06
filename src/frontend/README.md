# src/frontend/

PTX 轻量前端：把 PTX 文本转换为 `Module/Function + InstList + Metadata`。

包含模块
- Lexer/Parser（streaming）
- Directive/Metadata Binder
- ModuleBuilder

对外契约
- 输出的 InstRecord 与 label_index 必须能被 Instruction System 与 Executor 直接使用。
