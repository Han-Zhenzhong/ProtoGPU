# src/memory/

No-cache 内存子系统。

包含模块
- AddrSpaceManager：路由 global/shared/local/const/param
- GlobalMemory：线性内存模型
- SharedMemory：按 CTA 私有 byte-array
- Fence/Consistency：fence/volatile/scope 等一致性与可见性规则

对外契约
- MemUnit 通过统一内存访问契约（read/write/atomic/fence/barrier_sync）访问。
