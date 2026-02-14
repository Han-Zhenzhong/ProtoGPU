# src/common/

跨模块契约层（Contracts）。

内容范围
- 核心类型：InstRecord、MicroOp、Value、LaneMask、AddrSpace
- 标识与句柄：kernel/cta/warp/thread/stream/event/cmd 等 ID
- 统一错误与诊断信息结构
- 全局配置结构（例如硬件规格、观测配置）

约束
- 该目录应只包含“通用且无业务依赖”的定义，避免反向依赖其它模块。
