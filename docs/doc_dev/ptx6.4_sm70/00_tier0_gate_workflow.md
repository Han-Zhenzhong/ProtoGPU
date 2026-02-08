# 00 Tier‑0 闸门工作流（PTX6.4 + sm70）

Tier‑0 = tiny GPT‑2 bring-up（M1–M4）。它的核心不是“功能多”，而是“缺口必须立刻暴露、且每次扩展都不破坏既有行为”。

单一真源
- Tier‑0 forms 与约束：见 [docs/doc_spec/ptx64_baseline.md](../../doc_spec/ptx64_baseline.md) 中 "tiny GPT-2 bring-up baseline（M1–M4 级）"。

---

## 1) 最小闭环：一个 form 的四件套（强制）

每新增/修改一个可执行 PTX form，必须同时具备：
1. `assets/ptx_isa/*.json` entry（PTX → IR InstRecord）
2. `assets/inst_desc/*.json` entry（IR → uops 语义）
3. PTX fixture（`tests/fixtures/ptx/*.ptx`）
4. 回归覆盖（推荐：并入 `gpu-sim-tiny-gpt2-mincov-tests`）

否则该 form 视为“未交付”。

---

## 2) 改动点地图（从症状回溯到落点）

### A. mapper/descriptor miss

- 现象：`out.sim.completed=false`，`diag.module` 常见为 `instruction`/`simt`/`mapper`（具体取决于返回点）。
- 优先检查：
  - PTX fixture 写法是否与冻结匹配键一致：`ptx_opcode + type_mod + operand_kinds`
  - 对 `imm`：`0fXXXXXXXX` 是否走 `operand_kind=imm`
  - 对 `addr`：是否使用 `[%rdN]` 或 `[%rdN+imm]`
- 典型落点：
  - mapper：`src/instruction/ptx_isa.cpp`
  - descriptor lookup：`src/instruction/descriptor_registry.cpp`

### B. 执行器未实现 / uop 不支持

- 现象：`diag.code` 类似 `E_UOP_UNSUPPORTED`、`E_UOP_ARITY`。
- 落点：
  - Exec：`src/units/exec_core.cpp`
  - Mem：`src/units/mem_unit.cpp`
  - Control：`src/units/control_unit.cpp`

### C. 控制流 divergence

- 现象：M4 触发分歧必须 fail-fast。
- 落点：`src/simt/simt.cpp`（uniform-only 检测/next_pc 提交）。

---

## 3) Tier‑0 回归运行

- CTest 名称：`gpu-sim-tiny-gpt2-mincov-tests`
- 示例：

```bash
ctest --test-dir build -C Release -V -R "^gpu-sim-tiny-gpt2-mincov-tests$"
```

脚本入口（如果你希望“一键跑一组”）：
- `scripts/run_unit_tests.*`
- `scripts/run_integration_tests.*`

---

## 4) Fail-fast 规则（实现级检查清单）

在 code review 里按下面逐项过：

- **Unknown selectors**：
  - `allow_unknown_selectors=false` 时未知 selector 必须返回诊断失败（`E_MEMORY_MODEL`/`E_CTA_SCHED`/`E_WARP_SCHED`）。
  - 落点：`src/simt/simt.cpp`。

- **Unknown uop op/kind**：
  - JSON load 阶段必须失败（而不是运行到一半才发现）。
  - 落点：`src/instruction/descriptor_registry.cpp`。

- **Divergence unsupported**：
  - uniform-only 违反必须诊断失败。
  - 落点：`src/simt/simt.cpp`。

- **Global OOB / unallocated**：
  - 未分配/越界 global 访问必须诊断失败（禁止静默读 0、禁止静默写成功）。
  - 落点：`src/memory/memory.cpp` + `src/units/mem_unit.cpp`。

---

## 5) 新增 form 的推荐步骤（最小阻力路径）

1. 先改 fixture（写出你想支持的“PTX 写法”）。
2. 让 mapper 能命中：补 `assets/ptx_isa/*.json`。
3. 让 descriptor 能命中：补 `assets/inst_desc/*.json`。
4. 让执行通过：补 Units/Control/Mem。
5. 把 fixture 纳入 Tier‑0 回归（或新增一个 CTest，并解释为什么不并入 Tier‑0）。

注意
- 不要在 bring-up 阶段让 `flags/space` 参与匹配（除非走规格+schema+回归的完整流程）。
