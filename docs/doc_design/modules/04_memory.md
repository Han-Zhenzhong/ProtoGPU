# 04 Memory Subsystem（AddrSpaceManager + Global/Shared/Local/Const/Param + Fence）

说明
- 本文只描述抽象逻辑：No-cache 基线、地址空间路由、lane-wise 语义、同步与错误。
- 设计基准：与 [doc_design/arch_modules_block.diagram.puml](../arch_modules_block.diagram.puml) 对齐；若文本与图冲突，以图为准。

---

## 职责（Responsibilities）
- 为 MemUnit 提供统一内存访问契约：`read/write/atomic/fence/barrier_sync`。
- 维护地址空间路由：global/shared/local/const/param。
- 提供可配置的对齐/越界检查策略，并以 `Diagnostic` 报错。

---

## 输入/输出（Inputs/Outputs）
- 输入：MemUnit 发起的 Mem micro-ops（含 `LaneMask`、addr、size、space）。
- 输出：lane-wise 读值、写入确认、atomic old 值、同步结果或 fault。

---

## 内部执行流程（Internal Flow / State Machine）

### AddrSpaceManager 路由
```text
resolve(space, ctx) -> MemoryObject
  global -> GlobalMemory
  shared -> SharedMemory(cta_id)
  local  -> LocalMemory(thread_id)
  const  -> ConstMemory(module)
  param  -> ParamMemory(kernel_launch)
```

### lane-wise 访问语义
对每个 lane i：
- 若 `exec_mask[i]==0`：该 lane 不产生副作用。
- 若越界/未对齐且 strict：该 lane 产生 fault（整体行为由配置定义：全 warp fault 或 lane-wise fault）。

建议的 fault 策略
- 默认 lane-wise fault：fault lane 的结果标记为 fault，其它 lane 正常；同时产生 Diagnostic 与观测事件。

---

## 接口契约（Interfaces）

统一 API（概念）
```text
read(space, addr, size, lane_mask, ctx) -> vector<Value>
write(space, addr, size, values, lane_mask, ctx) -> void
atomic(op, space, addr, val, lane_mask, ctx) -> vector<Value>
fence(kind, scope, ctx) -> void
barrier_sync(cta_id, barrier_id, lane_mask, ctx) -> block/unblock
```

参数语义
- `addr` 可为 lane-wise 地址（常见）或标量地址（广播）；由 MemUnit 传入。
- `size` 以字节为单位，必须与 `attrs.width/type` 一致。
- `ctx` 至少包含 `{ kernel_id, cta_id, warp_id, thread_id }` 以便路由与诊断。

---

## Fence/Consistency（抽象假设）

目标
- 提供功能正确且保守的顺序与可见性。

假设与约束（需在实现中落实）
- 线程内：program order 保序。
- CTA 内：barrier_sync 后 shared 读写对同 CTA 线程可见。
- 跨 CTA/跨 SM：fence/atomic 的可见性规则由配置与实现明确；在缺省情况下采取保守顺序化（保证可复盘一致）。

---

## 可观测点（Obs events + counters）

事件
- `MEM`：ld/st/atom/fence/barrier（action 细分），携带 `space/addr/size/lane_mask`。
- fault：`MEM`（action=mem_fault, extra={reason, lane?}）。

统计
- `ld`, `st`, `atom`, `fence`, `barrier` 计数与字节数。

---

## 不变量与边界条件（Invariants / Error handling）
- No-cache：所有访问走 ASM；业务模块不得绕过 ASM 直接访问底层内存对象。
- Shared 生命周期与 CTA 绑定：CTA 释放时 shared 内存释放。
- Const/Param 为只读：对其 write 产生 fault。

---

## 验收用例（Smoke tests / Golden traces）
- Shared：同 CTA 内写后 barrier_sync，再读能看到更新。
- Global：copy engine 写入后 kernel 读取一致。
- Fence：在配置约束下，fence 前后事件与结果可复盘一致。
