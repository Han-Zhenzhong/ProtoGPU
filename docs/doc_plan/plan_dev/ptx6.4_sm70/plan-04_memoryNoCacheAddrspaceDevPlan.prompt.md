## Plan: Memory no_cache_addrspace Dev Plan

把 [docs/doc_dev/ptx6.4_sm70/04_memory_no_cache_addrspace.md](../../../doc_dev/ptx6.4_sm70/04_memory_no_cache_addrspace.md) 的 sm70 memory/determinism 基线（selector 校验、addrspace 分流、严格对齐、OOB fail-fast、同址冲突确定性、deterministic 禁并行、可观测性）落成一份可执行 dev plan，并补齐当前测试里缺失的关键诊断分流覆盖。

### Steps 6
1. 冻结 selector 基线与可观测性：在 [src/simt/simt.cpp](../../../../src/simt/simt.cpp) 固化 `memory.model == "no_cache_addrspace"` 的校验与 `simt/E_MEMORY_MODEL` fail-fast 条件（受 `allow_unknown_selectors` 控制），并确保 `RUN_START` 事件 `extra_json` 写入 `memory_model` 便于定位配置漂移。
2. 固化 OOB 两层防御与 Tier‑0 依赖：把 global OOB/未分配 “绝不静默成功” 作为硬约束，明确底层分配范围检查在 [src/memory/memory.cpp](../../../../src/memory/memory.cpp) 的 `AddrSpaceManager::{read_global,write_global}`，执行层翻译为 `units.mem/E_GLOBAL_MISS` 在 [src/units/mem_unit.cpp](../../../../src/units/mem_unit.cpp)，并引用 OOB 专项闸门文档 [docs/doc_dev/ptx6.4_sm70/00.01_fail_fast_memory_oob.md](../../../doc_dev/ptx6.4_sm70/00.01_fail_fast_memory_oob.md)。
3. 明确 addrspace 分流与 operand-kind 诊断：在 [src/units/mem_unit.cpp](../../../../src/units/mem_unit.cpp) 写清 `ld.param` 仅接受 `OperandKind::Symbol`（否则 `E_LD_PARAM_KIND`，miss 为 `E_PARAM_MISS`），以及 `ld/st.global` 仅接受 `OperandKind::Addr`（否则 `E_LD_GLOBAL_KIND`/`E_ST_GLOBAL_KIND`，miss 为 `E_GLOBAL_MISS`），并把对应的 memory model 接口约束链接到 [include/gpusim/memory.h](../../../../include/gpusim/memory.h)（或同等接口定义）。
4. 锁死严格对齐策略：在 [src/units/mem_unit.cpp](../../../../src/units/mem_unit.cpp) 固化“按访问 size 对齐（4/8）”并对 misaligned global read/write 统一产出 `E_GLOBAL_ALIGN`；计划中明确该检查发生在执行层（不是 `AddrSpaceManager`）以避免后续绕开。
5. 固化同址多 lane 写冲突的确定性：以 [src/units/mem_unit.cpp](../../../../src/units/mem_unit.cpp) 的 lane 遍历顺序（`lane=0..warp_size-1`）为冻结承诺，明确“同址多 lane st，后写覆盖先写 → lane id 越大优先级越高”，并把它纳入回归期望（避免未来重构引入非确定性）。
6. 固化 deterministic 禁并行：在 [src/simt/simt.cpp](../../../../src/simt/simt.cpp) 将 `parallel_enabled = cfg.parallel && !cfg.deterministic && sm_count>1` 视为对外承诺；计划中要求新增一条回归证明 `deterministic=true` 时即便配置 `parallel=true` 也不会启 worker 并导致 trace 交错。

### Further Considerations 3
1. 测试缺口：目前主要覆盖 `E_GLOBAL_MISS/E_GLOBAL_ALIGN`；`E_MEMORY_MODEL/E_PARAM_MISS/E_LD_*_KIND/E_ST_*_KIND` 基本无覆盖，建议补少量 targeted tests（不替代 Tier‑0）。
2. “allow_unknown_selectors=true 无 warning” 的可诊断性：当前只能靠 `RUN_START extra_json`；是否要在 plan 中明确这是刻意选择。
3. “已分配但未初始化的 global 读” 目前也会 miss（不返回 0）；需要在 plan 里明确这是冻结语义还是 bring-up 临时策略。
