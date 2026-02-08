# 00 Tier‑0 Gate Dev Plan（PTX6.4 + sm70）

目标：把 Tier‑0 闸门工作流落成“可执行的开发/评审清单”，确保每次新增/修改一个可执行 PTX form 都形成“四件套”闭环，且 `gpu-sim-tiny-gpt2-mincov-tests` 始终稳定通过。

单一真源
- Tier‑0 工作流（流程 + fail-fast + 回溯地图）：[docs/doc_dev/ptx6.4_sm70/00_tier0_gate_workflow.md](../../../doc_dev/ptx6.4_sm70/00_tier0_gate_workflow.md)
- Tier‑0 forms 与冻结约束（M1–M4）：[docs/doc_spec/ptx64_baseline.md](../../../doc_spec/ptx64_baseline.md)

---

## 1) Merge gate（唯一回归入口）

- 唯一 merge gate：`gpu-sim-tiny-gpt2-mincov-tests`
- 运行命令：

```bash
ctest --test-dir build -C Release -V -R "^gpu-sim-tiny-gpt2-mincov-tests$"
```

规则
- 任何 form/语义/执行路径的变更，都必须满足：Tier‑0 通过或明确说明例外（见 §5）。

---

## 2) 冻结接口（bring-up 期间的匹配键）

bring-up 冻结 matcher：
- `ptx_opcode + type_mod + operand_kinds`

约束
- bring-up 阶段不要让 `flags/space` 参与匹配（除非走“规格 + schema + 回归”的完整流程，并在 Tier‑0 或明确的附加测试中锁定行为）。

常见回溯点
- PTX→IR mapper：`src/instruction/ptx_isa.cpp`
- IR→uops descriptor lookup：`src/instruction/descriptor_registry.cpp`

---

## 3) 交付定义：一个 form 的“四件套”（强制）

每新增/修改一个可执行 PTX form，必须同时具备：
1. `assets/ptx_isa/*.json` entry（PTX → IR InstRecord）
2. `assets/inst_desc/*.json` entry（IR → uops 语义）
3. PTX fixture：`tests/fixtures/ptx/*.ptx`
4. 回归覆盖：优先并入 `gpu-sim-tiny-gpt2-mincov-tests`

否则该 form 视为“未交付”。

---

## 4) Fail-fast 清单（code review 必过项）

把下列规则当作“行为契约”：一旦被触发，必须以诊断失败结束（禁止静默回退/静默成功）。

### 4.1 Unknown selectors

- 条件：`allow_unknown_selectors=false` 时，遇到未知 selector 必须 fail-fast。
- 诊断：`E_MEMORY_MODEL` / `E_CTA_SCHED` / `E_WARP_SCHED`（以实现为准）
- 落点：`src/simt/simt.cpp`

### 4.2 Unknown uop op/kind（加载期失败）

- 条件：inst_desc JSON 中出现未知 `uop.kind` 或 `uop.op`，必须在 JSON load 阶段失败。
- 落点：`src/instruction/descriptor_registry.cpp`

### 4.3 Divergence unsupported（M4 uniform-only）

- 条件：uniform-only 违反必须 fail-fast。
- 落点：`src/simt/simt.cpp`

### 4.4 Global OOB / unallocated

- 条件：未分配/越界 global 访问必须诊断失败。
  - 禁止静默读 0
  - 禁止静默写成功
- 两层防御：
  - 内存模型层：`src/memory/memory.cpp`
  - 执行层翻译：`src/units/mem_unit.cpp`

---

## 5) 新增/修改一个 form 的推荐步骤（最小阻力路径）

1. 先改 fixture：用你希望支持的 PTX 写法写出 `tests/fixtures/ptx/*.ptx`。
2. 让 mapper 能命中：补齐/修改 `assets/ptx_isa/*.json`。
3. 让 descriptor 能命中：补齐/修改 `assets/inst_desc/*.json`。
4. 让执行通过：补齐 units（Exec/Mem/Control）与 SIMT 编排行为。
5. 把 fixture 纳入 Tier‑0：优先并入 `gpu-sim-tiny-gpt2-mincov-tests`。

如果暂时不能并入 Tier‑0（例外）
- 必须同时：
  - 新增一个独立 CTest（清晰命名、解释覆盖范围与不进入 Tier‑0 的原因）
  - 在相关 dev doc/plan 中记录该例外与解除条件（何时回归进 Tier‑0）

---

## 6) 失败回溯：从症状到落点（快速定位）

### A. mapper/descriptor miss

现象
- `out.sim.completed=false`，且 `diag.module` 常见为 `instruction`/`simt`/`mapper`（取决于返回点）。

优先检查
- fixture 是否符合冻结匹配键：`ptx_opcode + type_mod + operand_kinds`
- `imm`：`0fXXXXXXXX` 是否被解析为 `operand_kind=imm`
- `addr`：是否使用 `[%rdN]` 或 `[%rdN+imm]`

典型落点
- `src/instruction/ptx_isa.cpp`
- `src/instruction/descriptor_registry.cpp`

### B. 执行器未实现 / uop 不支持

现象
- `diag.code` 类似 `E_UOP_UNSUPPORTED`、`E_UOP_ARITY`。

落点
- Exec：`src/units/exec_core.cpp`
- Mem：`src/units/mem_unit.cpp`
- Control：`src/units/control_unit.cpp`

### C. 控制流 divergence

现象
- M4 触发分歧必须 fail-fast。

落点
- `src/simt/simt.cpp`
