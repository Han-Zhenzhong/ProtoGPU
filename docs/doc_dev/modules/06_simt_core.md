# 06 SIMT Core（实现落地：Contexts/Schedulers/Executor）

参照
- 抽象设计：`doc_design/modules/06_simt_core.md`
- Contracts：`doc_dev/modules/00_contracts.md`
- Kernel Launch（3D grid/block + builtins）：`doc_dev/modules/06.01_launch_grid_block_3d.md`

落地目标
- Executor 的单步边界清晰，阻塞可重试。
- WarpScheduler 的 ready 判定严格依赖上下文与阻塞状态。

落地位置（代码）
- `src/simt/`
  - contexts
  - schedulers
  - executor

---

## 1. Contexts

ThreadContext
- regs/preds 的 lane-wise 存储布局定义在 common/contracts 中，simt 只持有实例。

WarpContext
- pc/active_mask/reconv_stack/barrier_state/done。

CTAContext
- shared memory handle + barrier table + warps。

---

## 2. Schedulers

CTA Scheduler
- 负责创建 CTAContext，并将 warps 注册到 WarpScheduler。

Warp Scheduler
```text
pick_ready_warp() -> optional<WarpId>
```

ready 判定
- warp 未 done。
- barrier_state 未阻塞。
- 若引入 memory 队列阻塞，blocked_reason=MEM 时不可被 pick。

---

## 3. Executor

API
```text
step_warp(sm_state, warp_id) -> StepResult
```

执行顺序（固定）
- fetch InstRecord
- emit FETCH
- lookup InstDesc
- expand uops
- dispatch 至 Units
- emit COMMIT

阻塞语义
- 若任一 uop 返回 blocked，则 step_warp 返回 blocked_reason，并保证下次 step 可从相同 PC 重试。

观测
- FETCH/EXEC/CTRL/MEM/COMMIT 的 action 与字段必须与 contracts 一致。
