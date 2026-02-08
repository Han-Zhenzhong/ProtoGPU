# 00 范围与质量闸门（PTX6.4 + sm70）

## 目标

冻结一个“功能级可交付基线”，使外部工程可以依赖：
- PTX 输入：PTX 6.4 冻结子集（见 ../../doc_spec/ptx64_baseline.md）
- 硬件语义边界：sm70 profile（见 ../../doc_spec/sm70_profile.md）
- 回归闸门：tiny GPT-2 bring-up（M1–M4）最小覆盖必须持续通过

## 非目标（明确不承诺）

- 不承诺 cycle-accurate（时序/吞吐/占用率/pipeline/scoreboard 细节）。
- 不承诺 divergent control-flow（M4 仅支持 uniform-only）。
- 不承诺 cache（L1/L2）与复杂一致性模型；memory.model 仅实现 `no_cache_addrspace`。
- 不承诺 PTX 6.4 全量指令集/全量 directive。

## Tier-0：tiny GPT-2 bring-up（M1–M4）

Tier-0 的语义口径与 form 列表以 ../../doc_spec/ptx64_baseline.md 中 "tiny GPT-2 bring-up baseline（M1–M4 级）" 为准。

### 必须满足的工程约束

- 每个 Tier-0 form 必须具备：
  - `assets/ptx_isa/*.json` 可映射（PTX → IR InstRecord）
  - `assets/inst_desc/*.json` 有语义（IR → uops）
  - fixture PTX
  - 回归覆盖（Tier-0 CTest）

- fail-fast（强制）：
  - mapper/descriptor miss → 直接诊断失败（禁止 fallback）。
  - divergence（uniform-only 违反）→ 诊断失败。
  - 未分配/越界 global 访问 → 诊断失败。

### 回归入口

- CTest：`gpu-sim-tiny-gpt2-mincov-tests`
- 示例命令（Windows / Linux 通用）：

```bash
ctest --test-dir build -C Release -V -R "^gpu-sim-tiny-gpt2-mincov-tests$"
```

（注意：具体 build 目录与配置以 docs/doc_build/build.md 为准。）

## 扩展流程（硬性步骤）

当你要扩展“对外交付口径”（新增 form、扩展 profile 行为、或引入输出格式契约）时，必须按以下顺序推进：

1. 更新规格：
   - 更新 ../../doc_spec/ptx64_baseline.md（新增 form 或约束）或 ../../doc_spec/sm70_profile.md（新增可观察行为边界）。
2. 更新资产：
   - `assets/ptx_isa/*.json` 与 `assets/inst_desc/*.json`。
3. 更新实现：
   - Frontend / Instruction System / SIMT / Units / Memory 中的必要变更。
4. 更新回归：
   - fixture + CTest；并确保 Tier-0 持续通过。
5. 更新本目录设计：
   - 将新约束点补进对应设计文档，确保“对外承诺”与代码一致。
