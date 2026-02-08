# 03 SIMT Predication & Control-flow Dev Plan（M3/M4）

目标：把 sm70 profile 的 M3 predication 与 M4 minimal control-flow（uniform-only）冻结为可执行计划：明确 SIMT 编排职责、诊断分流、next_pc 的 inst 级提交点，并用 Tier‑0 gate 锁回归。

单一真源
- 实现落点与语义：[docs/doc_dev/ptx6.4_sm70/03_simt_predication_and_controlflow.md](../../../doc_dev/ptx6.4_sm70/03_simt_predication_and_controlflow.md)
- 规格（sm70 profile）：[docs/doc_spec/sm70_profile.md](../../../doc_spec/sm70_profile.md)

---

## 1) SIMT 主循环阶段（冻结）

`FETCH → desc lookup → expand → pred inject → dispatch → collect next_pc → COMMIT`

落点
- `src/simt/simt.cpp`

---

## 2) M3 predication（InstRecord.pred → MicroOp.guard）

- predication 在 expand 后由 SIMT 统一注入：`u.guard &= pred_mask`
- 越界必须 fail-fast：`simt/E_PRED_OOB`

---

## 3) M4 uniform-only（divergence fail-fast）

- 规则：对 `BRA/RET`，`m = active & guard` 若 `m` 非空且 `m != active` → `simt/E_DIVERGENCE_UNSUPPORTED`
- divergence 检测归属 SIMT（ControlUnit 不负责）

---

## 4) next_pc 通道与 inst 级提交

- ControlUnit 只产生 `StepResult.next_pc`，不直接写 PC
- SIMT 聚合：冲突 → `simt/E_NEXT_PC_CONFLICT`
- COMMIT：若存在 next_pc 则跳转，否则 `pc+1`；并发出 COMMIT 事件

---

## 5) 验收

- 默认 merge gate：`gpu-sim-tiny-gpt2-mincov-tests`

补充清单（用于 code review 对齐 plan-03 的 6 steps）

### A) 冻结 SIMT 编排契约与数据结构

落点
- `include/gpusim/contracts.h`
- `include/gpusim/simt.h`

必须一致的事实
- `exec_mask = warp.active & uop.guard`
- 当 `exec_mask` 为空: uop 语义为 no-op
- 控制流 next_pc 的唯一通道: `StepResult.next_pc`

### B) 固化安全检查与边界

落点
- `src/simt/simt.cpp`

检查点
- `E_WARP_SIZE`: warp_size must be in [1,32]
- `E_PC_OOB`: PC out of range
- partial warp: active mask must disable tail lanes

### C) M3 predication 注入 (expand 之后)

落点
- `src/simt/simt.cpp`

检查点
- predication must be injected after expand
- predicate id OOB must be `simt/E_PRED_OOB`

### D) ControlUnit 最小控制流语义 (BRA/RET)

落点
- `src/units/control_unit.cpp`

检查点
- predicated-off control op is treated as no-op
- BRA arity must be 1 and operand kind must be imm(pc)
- RET sets `warp.done=true`

### E) M4 uniform-only (divergence fail-fast)

落点
- `src/simt/simt.cpp`

检查点
- for BRA/RET: m = active & guard; if m is non-empty and m != active => `simt/E_DIVERGENCE_UNSUPPORTED`

### F) next_pc 聚合与 inst 级提交点 + observability

落点
- `src/simt/simt.cpp`

检查点
- multiple uops set next_pc and conflict => `simt/E_NEXT_PC_CONFLICT`
- commit stage updates pc = next_pc or pc+1

观测注意点
- trace UOP events currently record lane_mask as `warp.active` (not exec_mask)

---

验收

Tier-0 (merge gate):

```bash
ctest --test-dir build -C Release -V -R "^gpu-sim-tiny-gpt2-mincov-tests$"
```

Unit tests (including SIMT targeted tests):

```bash
ctest --test-dir build -C Release -V -R "^gpu-sim-(tests|inst-desc-tests|simt-tests|builtins-tests|config-parse-tests|tiny-gpt2-mincov-tests)$"
```
