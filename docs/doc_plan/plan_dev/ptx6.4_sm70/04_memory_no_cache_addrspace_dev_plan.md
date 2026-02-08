# 04 Memory no_cache_addrspace Dev Plan（determinism baseline）

目标：冻结 sm70 profile 的 memory/determinism 基线：selector 校验、addrspace 分流、严格对齐、global OOB fail-fast、同址多 lane 冲突确定性、deterministic 禁并行，并把关键诊断分流纳入回归。

单一真源
- 实现落点与边界：[docs/doc_dev/ptx6.4_sm70/04_memory_no_cache_addrspace.md](../../../doc_dev/ptx6.4_sm70/04_memory_no_cache_addrspace.md)
- 规格（sm70 profile）：[docs/doc_spec/sm70_profile.md](../../../doc_spec/sm70_profile.md)

---

## 1) selector 冻结与 fail-fast

- 仅支持：`memory.model == "no_cache_addrspace"`
- `allow_unknown_selectors=false` 且 selector 不匹配 → `simt/E_MEMORY_MODEL`

落点
- `src/simt/simt.cpp`

---

## 2) addrspace 分流（param vs global）

- `ld.param`：仅 `OperandKind::Symbol`
  - kind 错误：`units.mem/E_LD_PARAM_KIND`
  - miss：`units.mem/E_PARAM_MISS`
- `ld/st.global`：仅 `OperandKind::Addr`
  - kind 错误：`units.mem/E_LD_GLOBAL_KIND` / `units.mem/E_ST_GLOBAL_KIND`
  - miss：`units.mem/E_GLOBAL_MISS`

落点
- `src/units/mem_unit.cpp`

---

## 3) 严格对齐（bring-up 决策）

- global ld/st：若 `addr % size != 0` → `units.mem/E_GLOBAL_ALIGN`

落点
- `src/units/mem_unit.cpp`

---

## 4) global OOB/未分配（强制 fail-fast）

- 禁止静默读 0 / 静默写成功
- 依赖两层防御：`AddrSpaceManager` allocation-range + MemUnit 转译

参考
- [docs/doc_dev/ptx6.4_sm70/00.01_fail_fast_memory_oob.md](../../../doc_dev/ptx6.4_sm70/00.01_fail_fast_memory_oob.md)

---

## 5) 同址多 lane 写冲突（确定性覆盖）

- `st.global` lane 遍历顺序固定为 `0..warp_size-1`，后写覆盖先写

落点
- `src/units/mem_unit.cpp`

---

## 6) determinism 禁并行

- `deterministic=true` 必须强制禁用并行 worker（即便 `parallel=true`）

落点
- `src/simt/simt.cpp`

验收
- 默认 merge gate：`gpu-sim-tiny-gpt2-mincov-tests`
