# Docs（文档索引）

本目录承载 `ProtoGPU` 的所有文档。

## 快速入口

- 使用者文档（怎么跑/怎么配）
  - [doc_user/README.md](doc_user/README.md)
- 构建文档（怎么编译）
  - [doc_build/README.md](doc_build/README.md)
  - [doc_build/build.md](doc_build/build.md)

## 设计与实现

- 抽象设计（模块职责、语义与流程契约）
  - [doc_design/README.md](doc_design/README.md)
  - 总览（架构设计）：[doc_design/arch_design.md](doc_design/arch_design.md)

- 实现级设计（指导代码开发的落点：类型/接口/目录/错误/配置/trace）
  - [doc_dev/README.md](doc_dev/README.md)

## 规格与 Schema

- 规格/图（PUML 等）
  - [doc_spec/README.md](doc_spec/README.md)

- JSON schemas（inst_desc / ptx_isa / workload）
  - [../schemas/README.md](../schemas/README.md)

## 测试与计划

- 测试文档
  - [doc_tests/README.md](doc_tests/README.md)

- 计划文档（roadmap / 设计计划 / 开发计划 / 测试计划）
  - [doc_plan/overall_plan.md](doc_plan/overall_plan.md)

## 相关目录

- scripts（构建/单测/集成测试脚本）
  - [../scripts/README.md](../scripts/README.md)

- tools（生成器/trace viewer 等）
  - [../tools/README.md](../tools/README.md)

---

### 约定

- 文档之间以相对路径互链，默认从仓库根目录打开也可正常跳转。
- 若文本与 PUML 图冲突，以图为准（见各 doc_* README 的说明）。

## License

除非另有说明，本目录内容采用 CC BY 4.0 许可。详见 [LICENSE](LICENSE)。
