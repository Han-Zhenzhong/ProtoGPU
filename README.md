# gpu-sim

本仓库以“设计与规划先行”的方式组织 GPU/PTX VM 的设计与开发材料。

## 文档目录说明

- doc_build/
  - 构建与发布相关文档（例如：如何构建文档站点、文档生成脚本说明、产物目录约定）。

- doc_design/
  - 架构与模块设计文档（抽象逻辑层）。
  - 包含总体架构说明与模块关系图（PlantUML），用于定义模块边界、职责、流程与接口契约。
  - 典型内容：架构设计、模块分层、关键数据结构/契约、端到端时序图。

- doc_dev/
  - 面向“指导代码开发”的设计与规范（实现层）。
  - 典型内容：代码目录结构约定、模块落地方案、类/文件拆分、编码规范、调试与开发流程。

- doc_plan/
  - 规划与路线图（计划层）。
  - 用于记录设计或开发计划、任务拆解与验收点等。
  - 子目录约定：
    - doc_plan/plan_design/：抽象逻辑设计阶段的计划与拆解（模块设计顺序、对齐清单等）。
    - doc_plan/plan_dev/：代码开发阶段的计划与拆解。
    - doc_plan/plan_test/：测试设计与测试实现阶段的计划与拆解。

- doc_spec/
  - 所对标的硬件的确定性描述，simulator总体按照这些描述实现，以保证simulator的实际有效价值。

- doc_tests/
  - 测试设计与测试用例相关文档，也包含具体指导测试代码开发的文档。
  - 典型内容：测试策略、golden traces 约定、回归用例清单、验证方法与覆盖范围说明。

- doc_user/
  - 面向使用者的文档（User Guide）。
  - 典型内容：如何运行/使用工具、CLI/配置说明、示例、FAQ。

## 约定（简要）

- 架构与模块关系以 doc_design 下的 PUML 图为设计基准；若文字与图冲突，以图为准。
- “抽象逻辑设计”优先写在 doc_design / doc_plan；“指导代码开发”落到 doc_dev。

## 代码与测试目录结构

- src/
  - 功能代码根目录，模块边界与依赖方向按 doc_design 中的 PUML 图组织。
  - 子目录与 PUML package 对齐：common/frontend/instruction/runtime/simt/units/memory/observability/apps。

- tests/
  - 测试代码根目录。
  - unit/：单模块测试。
  - integration/：端到端集成路径测试（runtime→engines→simt→units→memory→observability）。
  - golden/：golden traces 与期望结果。
  - fixtures/：测试输入（PTX、descriptor JSON、configs）。

- assets/
  - 运行与回归用的输入数据资源。
  - ptx/：示例 PTX。
  - inst_desc/：指令描述 JSON 数据集。

- schemas/
  - 结构化数据的 schema 定义（例如 inst_desc.schema.json）。

- tools/
  - 辅助工具（例如 trace viewer、数据生成器等）。

- scripts/
  - 本地辅助脚本入口（build/run/test/format 等）。
