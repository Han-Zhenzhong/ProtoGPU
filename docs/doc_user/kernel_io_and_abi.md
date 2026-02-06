# Kernel I/O + ABI（数据输入 / 参数输入 / 结果输出）

本文从“使用者视角”说明：如何给 PTX 执行提供输入、如何传参数、如何取回执行结果。

对应实现与设计
- 抽象设计：`doc_design/modules/09_kernel_io_and_abi.md`
- 实现落地：`doc_dev/modules/09_kernel_io_and_abi.md`

---

## 1) 数据输入/输出（COPY：H2D/D2H）

当前仓库的最小路径是：
- host 侧维护一个 **HostBuffer**（字节数组）
- device 侧用 **DevicePtr（u64 地址）** 表示 global memory 指针
- 通过 `memcpy_h2d` / `memcpy_d2h`（或将来的 COPY 命令）把数据在 host/device 间移动

注意
- 当前模型是 no-cache 的最简 global memory：用于 bring-up/回归与可观测性验证。

---

## 2) 参数输入（`.param`）

PTX kernel 的参数通常声明在 `.entry` 签名中，例如：

```ptx
.visible .entry write_out(
    .param .u64 out_ptr
)
```

当前最小支持
- 解析 `.param .u32/.u64 <name>` 并生成 layout
- 支持 `ld.param.*` 通过参数名读取（示例：`ld.param.u64 %rd0, [out_ptr];`）

限制（当前实现）
- 仅支持 `.u32/.u64` 两类参数
- `ld.param` 的操作数必须是参数名（`[out_ptr]` 这类 symbol 形式）

---

## 3) 结果输出

结果分两类：

1) **数据结果（显式）**
- kernel 把数据写入 global memory（例如 `st.global.u32 [%rd0], %r0;`）
- host 显式执行 D2H 回读，得到可比较的输出 bytes

2) **控制结果（状态 + 观测）**
- `gpu-sim-cli` 会输出：
  - trace：`out/trace.jsonl`
  - stats：`out/stats.json`
- 若仿真中出现错误，会输出 `Diagnostic(module/code/message/...)` 用于定位

---

## 4) 端到端演示：write_out

示例 PTX：`assets/ptx/write_out.ptx`
- 读取 `.param out_ptr`
- 写一个固定值 `42` 到 `*out_ptr`

运行：

```bash
./build/gpu-sim-cli --ptx assets/ptx/write_out.ptx --io-demo
```

预期：
- 终端打印 `io-demo u32 result: 42`
- trace/stats 文件正常生成
