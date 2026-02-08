# 06 Dev：对外 API 与资产打包（实现现状与扩展点）

本文对应设计文档 [docs/doc_design/ptx6.4_sm70/06_public_api_and_assets_packaging.md](../../doc_design/ptx6.4_sm70/06_public_api_and_assets_packaging.md)。它把“外部工程可稳定依赖的 API + 资产加载方式”落到当前代码：现有哪些 public header、哪些入口已经支持 in-memory（JSON text / PTX text），以及还缺哪些 Runtime 层封装。

---

## 1) 当前对外 API 的真实入口（public headers）

最小对外调用面主要是：

- `include/gpusim/runtime.h`
  - `AppConfig` / `load_app_config_json_*`
  - `Runtime`（host/device buffer、PTX kernel run、workload run）
  - `RunOutputs { SimResult sim }`

- `include/gpusim/contracts.h`
  - `Diagnostic`（对外错误契约的一部分）
  - `Event` / `LaneMask` 等（对外 trace 的承载结构）

- `include/gpusim/observability.h`
  - `ObsControl` + `event_to_json_line()` / `stats_to_json()`（当前 CLI 的输出格式）

注意
- 设计文档里提到的 `include/gpusim/workload.h` 在当前仓库里**不存在**；workload 能力通过 `Runtime::run_workload(path)` 暴露，并由 `src/runtime/workload.cpp` 实现解析与调度。

---

## 2) Runtime：文件路径模式（当前稳定可用）

实现：`include/gpusim/runtime.h` + `src/runtime/runtime.cpp`

当前对外最稳定的调用方式是“全文件路径输入”：

- PTX：`Runtime::run_ptx_kernel*_launch(ptx_path, ptx_isa_path, inst_desc_path, ...)`
- 资产：
  - `PtxIsaRegistry::load_json_file(ptx_isa_path)`
  - `DescriptorRegistry::load_json_file(inst_desc_path)`

这条路径的优点：
- 不要求调用方理解内部对象生命周期（registry/descriptor/expander）。
- 出错统一体现在 `RunOutputs.sim.diag`（或抛异常：例如 `memcpy_h2d` 写入失败）。

当前限制（与“外部工程不依赖 repo 路径结构”的关系）：
- `Runtime`/CLI 不会自动寻找 repo 内 assets；调用方必须显式提供资产路径。
- CLI 的默认参数（见 `src/apps/cli/main.cpp`）使用 repo 内 `assets/...`，这只是 demo 默认值，不是对外承诺。

---

## 3) 已经存在的 in-memory 能力（但 Runtime 还没封装）

### 3.1 PTX text（前端）

`include/gpusim/frontend.h` 里 `Parser` 已经支持：
- `parse_ptx_text_tokens(ptx_text, file_name)`

因此“PTX 文本来自网络/内嵌资源”的场景在前端层是可实现的。

### 3.2 JSON assets text（ptx_isa / inst_desc）

两套 registry 都支持从内存字符串加载：

- `include/gpusim/ptx_isa.h`
  - `PtxIsaRegistry::load_json_text(text)`

- `include/gpusim/instruction_desc.h`
  - `DescriptorRegistry::load_json_text(text)`

因此“资产打包成单个可执行文件资源”在底层已具备最关键的装载接口。

---

## 4) 仍缺的“对外 API 收敛”（按设计建议的最小改动）

当前 gap：Runtime 只接收 file paths；外部工程若要走 in-memory，需要自己：
- 调 Parser（text tokens）
- 手工构建 `KernelImage`
- 手工 `load_json_text()` 两个 registry
- 构造 `SimtExecutor` 并跑

按设计文档建议的最小封装方式（概念上）：

- 给 `Runtime` 增加 overload（不破坏现有 API）：
  - `run_ptx_kernel_text_launch(ptx_text, ptx_isa_json_text, inst_desc_json_text, ...)`
  - 或增加 `PtxIsaSource/InstDescSource` 的变体类型（FilePath vs InMemoryJson）。

落点建议（实现路径）：
- `src/runtime/runtime.cpp`：抽出一个内部 helper，接收 `KernelTokens + PtxIsaRegistry + DescriptorRegistry`。
- 复用现有：`PtxIsaMapper::map_kernel()`、`DescriptorRegistry::lookup()`、`SimtExecutor::run()`。

兼容性建议：
- 不引入新的全局单例；让 assets 的 ownership 随 call 生命周期走，避免隐式缓存引起“资产漂移”。

---

## 5) profile/config 的对外输入（现状）

对外配置入口：
- `load_app_config_json_file()` / `load_app_config_json_text()`（`src/runtime/runtime.cpp`）

关键字段：
- `sim.*`：warp_size/max_steps/sm_count/parallel/deterministic/cta_scheduler/warp_scheduler/allow_unknown_selectors
- `memory.model` 或 `arch.components.memory_model`：写入 `SimConfig.memory_model`
- `arch.profile`：当前仅识别 `baseline_no_cache`（会覆盖 selectors）

重要行为：
- `deterministic=true` 会强制禁用并行 worker（见 `src/simt/simt.cpp`），符合 profile 附录 C 的“可复现边界”。

---

## 6) workload：对外“脚本化运行”入口

对外入口：`Runtime::run_workload(workload_json_path)`。

实现：`src/runtime/workload.cpp`：
- 解析 workload JSON
- 负责 buffers/args/entry/launch 选择
- 对 schema/ref 错误用 `Diagnostic{module="runtime", code=E_WORKLOAD_SCHEMA/E_WORKLOAD_REF/...}` fail-fast

契约建议
- 如果未来要提供 in-memory workload：可对 `run_workload` 增加 `run_workload_text(json_text)` 的 overload（解析器已经有 `slurp()`，替换为参数输入即可）。

---

## 7) 与 Tier‑0 的关系（回归闸门）

对外 API 的最小可用验收仍然是 Tier‑0（tiny GPT‑2 M1–M4）端到端可跑：

```bash
ctest --test-dir build -C Release -V -R "^gpu-sim-tiny-gpt2-mincov-tests$"
```

当你引入新的对外 API（尤其是 in-memory assets/text overload）时，建议：
- 保持现有 file-path API 行为不变
- 在 Tier‑0 中新增一个小型“in-memory 路径”用例（最小 kernel + 最小 assets text），防止未来把 text loader 弄坏
