# 04 Memory（实现落地：AddrSpaceManager 与地址空间实现）

参照
- 抽象设计：`doc_design/modules/04_memory.md`
- Contracts：`doc_dev/modules/00_contracts.md`

落地目标
- 为 MemUnit 提供统一内存 API：read/write/atomic/fence/barrier_sync。
- No-cache 基线：所有内存访问均经由 AddrSpaceManager 路由。

落地位置（代码）
- `src/memory/`
  - addr_space_manager
  - global_memory
  - shared_memory
  - local_memory
  - const_memory
  - param_memory

---

## 1. API 落地

AddrSpaceManager
```text
read(space, addr, size, lane_mask, ctx) -> vector<Value>
write(space, addr, size, values, lane_mask, ctx) -> void
atomic(op, space, addr, val, lane_mask, ctx) -> vector<Value>
fence(kind, scope, ctx) -> void
barrier_sync(cta_id, barrier_id, lane_mask, ctx) -> block/unblock
```

lane-wise addr 表示
- `addr` 支持两种形态：
  - 标量地址（广播到所有 active lanes）
  - lane-wise 地址向量（每 lane 独立地址）
- 由 MemUnit 将地址表达转换为统一的内部表示传入 ASM。

---

## 2. strict_memory_checks 行为矩阵

当 strict_memory_checks=true
- 未对齐：产生 `MEM_UNALIGNED` Diagnostic。
- 越界：产生 `MEM_OOB` Diagnostic。
- 向 const/param 写：产生 `MEM_WRITE_TO_READONLY` Diagnostic。

当 strict_memory_checks=false
- 未对齐与越界按实现定义的保守策略处理，但必须上报观测事件（action=mem_check_relaxed）。

---

## 3. Shared/Global/Local/Const/Param 落地

SharedMemory
- 每 CTA 拥有独立 byte-array。
- CTA 生命周期结束时释放。

GlobalMemory
- 单一线性 byte-array。

LocalMemory
- 以 thread_id 为粒度维护区域；布局由 SIMT/ThreadContext 提供。

Const/Param
- 只读；写入一律诊断失败。

---

## 4. Fence/Barrier 落地

Fence
- 以“功能正确且保守”的方式实现顺序与可见性。
- fence 事件必须上报 `MEM`（action=fence, extra={kind,scope}）。

Barrier
- barrier_sync 在 CTA 范围内维护到达计数与等待集合。
- 未满足条件时返回 block，MemUnit 将阻塞上报给 Executor。
