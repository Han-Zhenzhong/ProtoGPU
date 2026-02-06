# tests/integration/

集成测试。

覆盖范围
- Streaming runtime → engines → SIMT executor → units → memory → observability 的端到端路径。
- 重点验证：event/wait 依赖、copy/compute overlap、atomic/fence/barrier 语义与 trace 一致性。
