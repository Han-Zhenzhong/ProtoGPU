# 06 Public API & Assets Packaging Dev Plan

目标：收敛对外稳定 API 面与资产加载方式：保持现有 file-path Runtime API 不破坏，同时把底层已存在的 in-memory loaders（PTX text + JSON text）封装为 Runtime overload，并用 Tier‑0 + 最小 in-memory 用例锁回归。

单一真源
- 实现现状与扩展点：[docs/doc_dev/ptx6.4_sm70/06_public_api_and_assets_packaging.md](../../../doc_dev/ptx6.4_sm70/06_public_api_and_assets_packaging.md)

---

## 1) 冻结 public headers（外部可依赖面）

- `include/gpusim/runtime.h`
- `include/gpusim/contracts.h`
- `include/gpusim/observability.h`

说明
- repo 当前不存在 `include/gpusim/workload.h`；workload 入口为 `Runtime::run_workload(path)`。

---

## 2) 保持现有 file-path API 行为不变

- 资产路径必须显式传入（Runtime 不自动搜索 repo assets）
- 诊断统一体现在 `RunOutputs.sim.diag`（或明确抛异常的路径）

落点
- `src/runtime/runtime.cpp`

---

## 3) 抽取内部 helper（先重构，后加新 API）

- 在 `src/runtime/runtime.cpp` 抽一个内部 helper：输入 `KernelTokens + registries + config (+ args)`，复用现有 mapper/descriptor/simt 执行链路。
- 让现有 `run_ptx_kernel*_launch` 系列统一走 helper（行为不变）。

---

## 4) 增加 in-memory overload（增量，不破坏）

底层能力已存在
- PTX text tokens：`include/gpusim/frontend.h` `parse_ptx_text_tokens()`
- PTX ISA JSON text：`include/gpusim/ptx_isa.h` `PtxIsaRegistry::load_json_text()`
- inst_desc JSON text：`include/gpusim/instruction_desc.h` `DescriptorRegistry::load_json_text()`

计划交付
- 新增 `Runtime::run_ptx_kernel_text_launch(...)`（或 Source 变体类型）
- 禁止隐式全局缓存；assets ownership 随 call 生命周期走

---

## 5) workload（可选增强）

- 现状：`Runtime::run_workload(path)` 在 `src/runtime/workload.cpp` 实现
- 可选：新增 `run_workload_text(json_text)` overload（仅做薄封装）

---

## 6) 验收

- 默认 merge gate：`gpu-sim-tiny-gpt2-mincov-tests`
- 新增最小 in-memory 用例（最小 PTX + 最小 assets text），确保未来不会把 text loader/overload 弄坏
