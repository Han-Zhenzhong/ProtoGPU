# 04 Dev：Memory no_cache_addrspace（实现落点）

本文对应设计文档 [docs/doc_design/ptx6.4_sm70/04_memory_no_cache_addrspace.md](../../doc_design/ptx6.4_sm70/04_memory_no_cache_addrspace.md)。目标是把 sm70 profile 的 memory/determinism 基线落到当前代码：**selector 校验**、**addrspace 区分**、**严格对齐**、**OOB fail-fast**、**同址多 lane 冲突的确定性**，以及 **deterministic 禁并行**。

单一真源（规格）
- [docs/doc_spec/sm70_profile.md](../../doc_spec/sm70_profile.md)

---

## 1) memory.model selector（冻结基线：no_cache_addrspace）

实现位置：`src/simt/simt.cpp`

- 当前仅实现：`cfg_.memory_model == "no_cache_addrspace"`。
- 当 `allow_unknown_selectors == false` 且 `memory_model` 非该值：直接 fail-fast
  - `Diagnostic{ module="simt", code="E_MEMORY_MODEL" }`
- 当 `allow_unknown_selectors == true`：当前实现会继续执行（回退到现有内存行为），但**没有“warning”通道**，因此不会产生非致命诊断。

可观察性补充：
- `RUN_START` 事件会把 `memory_model` 写进 `extra_json`（便于离线定位配置漂移），但这不是 warning。

---

## 2) addrspace 与访问路径（param vs global）

执行侧入口：`src/units/mem_unit.cpp`（`MemUnit::step`）

### 2.1 param：ld.param（symbol）

- 路径：`MemUnit` → `IMemoryModel::read_param_symbol(name,size)`
- 限制：只支持 `OperandKind::Symbol`（否则 `E_LD_PARAM_KIND`）
- 错误：symbol 不存在/越界 → `E_PARAM_MISS`

param backing store：`AddrSpaceManager`
- layout：`AddrSpaceManager::set_param_layout(layout)`
- blob：`AddrSpaceManager::set_param_blob(blob)`
- read：`AddrSpaceManager::read_param_symbol()`

### 2.2 global：ld/st.global（addr）

- `ld.global`：要求 `OperandKind::Addr`（否则 `E_LD_GLOBAL_KIND`）
  - `mem_.read_global(addr, size)` miss → `E_GLOBAL_MISS`

- `st.global`：要求 `OperandKind::Addr`（否则 `E_ST_GLOBAL_KIND`）
  - `mem_.write_global(addr, bytes)` false → `E_GLOBAL_MISS`

global backing store：`AddrSpaceManager`
- alloc：`AddrSpaceManager::alloc_global(bytes, align)` 记录 allocation ranges
- read：范围合法 + 每字节存在，否则 `nullopt`
- write：范围合法才允许写（否则 false）

---

## 3) 对齐策略：严格模式（bring-up 选择）

实现位置：`src/units/mem_unit.cpp`

- 访问 size：由 `uop.attrs.type` 推导（`F32/U32/S32` → 4；`U64/S64` → 8）。
- 对齐规则：若 `addr % size != 0` → fail-fast：
  - global read：`E_GLOBAL_ALIGN` + message `global read is misaligned`
  - global write：`E_GLOBAL_ALIGN` + message `global write is misaligned`

注意
- 该策略是“执行期严格对齐”。底层内存模型（`AddrSpaceManager`）不做对齐检查。

---

## 4) 越界/未分配 global：强制 fail-fast

这是 Tier‑0 必须锁死的边界，详细实现与回归见：
- [docs/doc_dev/ptx6.4_sm70/00.01_fail_fast_memory_oob.md](00.01_fail_fast_memory_oob.md)

实现要点（两层防御）：
- 内存模型层：`AddrSpaceManager::{read_global,write_global}` 先做 allocation-range 检查
- 执行层：`MemUnit` 将 miss 转换为 `Diagnostic{ module="units.mem", code="E_GLOBAL_MISS" }`

当前诊断字段的限制（与设计文档的“更强定位信息”之间的差距）：
- `MemUnit` 的 `Diagnostic` 目前不带 `inst_index`（它拿不到 PC），也不带 lane id。
- 实际定位可依赖：trace 里的 `FETCH(pc, opcode)` 与 `UOP` 事件（同一次 run 内可关联）。

---

## 5) 同地址多 lane 冲突（同一条 st 内）：确定性覆盖

设计口径：同一 warp、同一条 `st` 若多个 lane 写同一地址，应采用确定性策略。

当前实现（`src/units/mem_unit.cpp`）：
- `st.global` 对 lane 的遍历顺序是 `lane = 0..warp_size-1`（只对 `exec_mask` 中的 lane 生效）。
- 每个 lane 依次调用 `mem_.write_global(addr, bytes)`。

因此：
- 若多个 lane 写同址，写入顺序是 lane id 升序，后写覆盖先写，最终结果等价于“lane id 越大优先级越高”。
- 这满足“确定性覆盖（lane id 从小到大覆盖）”的冻结承诺。

回归建议
- 如果后续把该行为作为对外承诺强化，建议补一条 fixture+测试明确断言该覆盖顺序（避免未来重构变成非确定性）。

---

## 6) Determinism 与并行（附录 C）

实现位置：`src/simt/simt.cpp`

- 并行启用条件：
  - `parallel_enabled = cfg_.parallel && !cfg_.deterministic && cfg_.sm_count > 1`
- 因此当 `deterministic == true` 时，即便 `parallel == true` 也会强制走单线程路径（等价于“禁用并行 worker”）。

可复现承诺（当前实现可观察到的效果）：
- deterministic=true：CTA 执行不并行；trace 事件不会跨 SM worker 交错。
- deterministic=false：允许多 worker 并发；trace 事件顺序可能交错（功能结果仍应保持一致，除非引入未定义行为/竞态语义）。

---

## 7) 验收入口（Tier‑0）

```bash
ctest --test-dir build -C Release -V -R "^gpu-sim-tiny-gpt2-mincov-tests$"
```
