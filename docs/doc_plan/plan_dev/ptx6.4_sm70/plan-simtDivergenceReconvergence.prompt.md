## Plan: M5 SIMT 分歧与合流 Dev Plan

把设计文档里的 “CFG+ipdom 预计算 reconv_pc、SIMT stack split/merge、lane-wise ret、trace/diag 与回归用例”落到当前实现框架中。按 M5.1–M5.4 分阶段交付：先做静态分析与诊断闭环，再接入执行器栈语义，最后修正 ret 语义并补齐可观测与单测，确保不破坏现有 M3/M4 的主循环与 inst 级 commit 约束。

### Steps 6
1. 在 [src/simt](src/simt) 新增 `control_flow_analysis` 产出 `reconv_pc_by_pc`.
2. 在 [src/simt/simt.cpp](src/simt/simt.cpp) 入口预计算并 fail-fast：`E_RECONV_MISS`/`E_RECONV_INVALID`.
3. 扩展 [include/gpusim/units.h](include/gpusim/units.h) 的 `WarpState`：`simt_stack` 与 `exited` 字段.
4. 在 [src/simt/simt.cpp](src/simt/simt.cpp) 增加 stack normalize：空 mask pop、到达 `reconv_pc` merge.
5. 在 [src/simt/simt.cpp](src/simt/simt.cpp) 对 `MicroOpOp::Bra` 实现 split/frames，必要时绕过 `ControlUnit::step`.
6. 在 [src/simt/simt.cpp](src/simt/simt.cpp) 实现 lane-wise `MicroOpOp::Ret`，并更新/新增 [tests/unit/simt_predication_controlflow_tests.cpp](tests/unit/simt_predication_controlflow_tests.cpp).

### Further Considerations 3
1. Join 帧建模：显式 `Join` frame 可避免 `pc==reconv_pc` 死循环.
2. 并行 SM：对同一 `KernelImage` 仅预计算一次 reconv，跨 worker 只读共享.
3. EXIT sentinel：约定 `PC==insts.size()` 等 sentinel，并统一 warp-done 判定与 merge 规则.
