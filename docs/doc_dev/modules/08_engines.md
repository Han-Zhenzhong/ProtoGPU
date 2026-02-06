# 08 Engines（实现落地：CopyEngine/ComputeEngine/tick 主循环）

参照
- 抽象设计：`doc_design/modules/08_engines.md`
- Runtime：`doc_dev/modules/07_runtime_streaming.md`
- SIMT：`doc_dev/modules/06_simt_core.md`

落地目标
- 定义 tick 的唯一主循环组织方式与推进顺序。
- CopyEngine/ComputeEngine 完成信号只通过 DependencyTracker。

落地位置（代码）
- `src/runtime/` 与 `src/apps/cli/`（tick 驱动）
- `src/runtime/engines/` 或等价目录（按现有目录结构选择落点并保持一致）

---

## 1. tick 主循环

驱动者
- CLI 负责创建 runtime、加载 module/descriptor/config，并驱动 `tick()` 直到完成或出错。

推进顺序（固定）
1) Runtime：扫描 streams 队首并判定 ready
2) 提交 ready COPY → CopyEngine.submit
3) 提交 ready KERNEL → ComputeEngine.submit
4) CopyEngine.tick（推进 copy）
5) ComputeEngine.tick（推进 kernel：SMModel→WarpScheduler→Executor.step_warp）
6) 收集 completion，调用 DependencyTracker.on_complete
7) 发出必要观测事件（submit/complete）

约束
- 每次 tick 必须递增 `ts`，所有事件的 `ts` 取自该逻辑时间戳。

---

## 2. CopyEngine

API
```text
submit_copy(cmd) -> CmdId
tick(ts) -> void
```

内存交互
- 只作用 GlobalMemory。

完成
- 完成后回调 DepTracker.on_complete(cmd_id)。

观测
- `COPY`：copy_submit/copy_complete。

---

## 3. ComputeEngine

API
```text
submit_kernel(cmd) -> CmdId
tick(ts) -> void
```

kernel 上下文
- submit 时创建 KernelContext 与 CTA work items。
- tick 时推进 SMModel，直到 kernel 完成。

完成
- 完成后回调 DepTracker.on_complete(cmd_id)。

观测
- `STREAM`：kernel_submit/kernel_complete。
