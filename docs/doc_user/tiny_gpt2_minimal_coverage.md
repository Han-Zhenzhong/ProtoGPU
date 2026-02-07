# tiny GPT-2 最小覆盖（M1–M4）使用说明

本页面向使用者，说明：当前 gpu-sim（PTX 6.4 + sm70 功能级基线）为了 bring-up tiny GPT-2（FP32 推理）已经打通的“最小可运行闭环”，以及如何运行对应回归。

对应实现/设计文档：
- Dev：`docs/doc_dev/tiny_gpt2/minimal_coverage_dev.md`
- Design：`docs/doc_design/tiny_gpt2/minimal_coverage_design.md`
- Spec：`docs/doc_spec/tiny_gpt2_minimal_coverage_matrix.md`

---

## 覆盖内容（目前已经支持）

最小闭环覆盖 M1–M4（按 bring-up 优先级）：

- M1：`ld/st.global.f32` + `fma.f32`（含 `%fN` 与 `0fXXXXXXXX` 浮点立即数）
- M2：地址/索引算术（`mul.u32`、`add.u64`）
- M3：`setp.*` + predication（`@%p` guard 生效；越界 lane 不写）
- M4：`bra`（label→pc 改写 + `next_pc` 提交）；当前仅支持 uniform control-flow（出现 divergence 会诊断报错）

说明
- 目前 form 匹配规则仍是：`ptx_opcode + type_mod + operand_kinds`；flags 通常不参与 mapper 匹配。
- bring-up 阶段强制 fail-fast：inst_desc 中未知 uop op/kind 会在加载阶段报错。

---

## 如何运行回归（推荐）

### 方式 A：通过 scripts（最省心）

从仓库根目录：

Windows（cmd）：

```bat
scripts\run_unit_tests.bat build
scripts\run_integration_tests.bat build
```

Bash（Git Bash / WSL / Linux / macOS）：

```bash
bash scripts/run_unit_tests.sh build
bash scripts/run_integration_tests.sh build
```

脚本行为
- 如果 build 目录或目标不存在，脚本会先调用 `scripts/build.*` 执行 configure+build。
- `run_unit_tests.*` 会包含：`gpu-sim-tiny-gpt2-mincov-tests`。
- `run_integration_tests.*` 在系统可用 `ctest` 时，也会运行：`gpu-sim-tiny-gpt2-mincov-tests`。

### 方式 B：直接跑 CTest

（更适合 CI 或你只想跑这一条）

```bash
ctest --test-dir build -C Release -V -R "^gpu-sim-tiny-gpt2-mincov-tests$"
```

---

## 测试输入与期望

CTest 目标
- `gpu-sim-tiny-gpt2-mincov-tests`

它会跑两个端到端用例（走 Runtime + 真实 PTX/资产）：

1) M1：f32 load + fma + store + predication
- PTX：`tests/fixtures/ptx/m1_fma_ldst_f32.ptx`
- 断言：
  - `i < n` 的 lane 输出满足 `a[i] * b[i] + 1.0f`（宽松误差阈值）
  - `i >= n` 的 lane 保持 sentinel（验证 predication 生效，不写）

2) M4：uniform `bra` loop
- PTX：`tests/fixtures/ptx/m4_bra_loop_u32.ptx`
- 断言：loop 运行 3 次，最后写回 `3`。

使用的默认资产（与 demo 对齐）
- PTX ISA map：`assets/ptx_isa/demo_ptx64.json`
- inst_desc：`assets/inst_desc/demo_desc.json`

---

## 常见失败原因（排错入口）

- `E_DESC_MISS`：inst_desc 未匹配到
  - 通常是 `assets/inst_desc/demo_desc.json` 的 `operand_kinds` 与实际解析不一致
- `DESC_AMBIGUOUS`：ptx_isa 有多个 entry 同时匹配
  - 调整 `assets/ptx_isa/demo_ptx64.json` 让匹配唯一
- `E_DIVERGENCE_UNSUPPORTED`：分支出现 divergence（当前不支持 reconvergence）
  - 先把测试保持在 warp_size=1 或保证 predication 在全 warp 上一致

---

## 下一步（扩展到 tiny GPT-2 真 kernel）

这套最小回归的目的，是提供“能持续验证”的基线，后续扩展指令覆盖时建议：
- 先加一个能复现新指令语义的小 PTX fixture
- 让它进入 `gpu-sim-tiny-gpt2-mincov-tests` 或新增相邻的 mincov tests
- 再推进到更贴近 tiny GPT-2 的 kernel/workload
