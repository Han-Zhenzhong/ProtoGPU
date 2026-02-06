# 05 Units（实现落地：ExecCore/ControlUnit/MemUnit）

参照
- 抽象设计：`doc_design/modules/05_units.md`
- Contracts：`doc_dev/modules/00_contracts.md`

落地目标
- Units 只执行 micro-ops，不进行 lookup/expand。
- 每个 uop 执行必须产生一致的观测事件。

落地位置（代码）
- `src/units/`
  - exec_core
  - control_unit
  - mem_unit

---

## 1. ExecCore

API
```text
run_exec_uop(uop, exec_mask, thread_ctx) -> void
```

落地规则
- 输入读取与输出写回均按 lane 执行。
- 类型与宽度以 `uop.attrs` 为准。

观测
- `EXEC`（action=exec_uop），extra 含 op/type/width。

---

## 2. ControlUnit

API
```text
run_ctrl_uop(uop, exec_mask, warp_ctx, label_index) -> void
```

reconvergence 栈
- 使用 `ReconvEntry { reconv_pc, masks... }` 结构，并保持 push/pop 条件确定。

PC 更新
- 只使用 InstList index。

观测
- `CTRL`（action=ctrl_uop），extra 含 target_pc/stack_depth。

---

## 3. MemUnit

API
```text
run_mem_uop(uop, exec_mask, ctx, memsys) -> MemResult
```

落地规则
- 地址计算统一输出到 ASM 的地址表达。
- 阻塞：barrier 未满足或 memory 队列限制时返回 blocked。

观测
- `MEM`（action=mem_uop），extra 含 space/addr/size。
