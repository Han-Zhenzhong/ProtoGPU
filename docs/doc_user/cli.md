# gpu-sim-cli（命令行）

`gpu-sim-cli` 是最小可运行入口：加载 config/descriptor/PTX，执行仿真，并输出 trace/stats。

## 参数

- `--ptx <file>`：PTX 输入文件路径
- `--ptx-isa <file>`：PTX ISA 映射表 JSON 路径（必需：PTX op 形态 → ir_op + operand_kinds）
- `--inst-desc <file>`：指令描述 JSON（inst_desc，IR 语义库：ir_op → uops）
  - 兼容别名：`--desc <file>`
- `--config <file>`：运行配置 JSON 路径
- `--trace <file>`：trace 输出路径（jsonl）
- `--stats <file>`：stats 输出路径（json）
- `--io-demo`：启用 Kernel I/O + ABI 的最小端到端演示（见下文）

## 默认输入

在仓库根目录运行时，默认使用：
- PTX：`assets/ptx/demo_kernel.ptx`
- PTX ISA map：`assets/ptx_isa/demo_ptx8.json`
- 指令描述（inst_desc）：`assets/inst_desc/demo_desc.json`
- 配置：`assets/configs/demo_config.json`

## 常规运行

```bash
./build/gpu-sim-cli \
  --ptx assets/ptx/demo_kernel.ptx \
  --ptx-isa assets/ptx_isa/demo_ptx8.json \
  --inst-desc assets/inst_desc/demo_desc.json \
  --config assets/configs/demo_config.json \
  --trace out/trace.jsonl \
  --stats out/stats.json
```

## PTX op → IR op 映射（`--ptx-isa`）

用户侧只需要关心“PTX8 的指令长什么样，映射到哪个通用 IR opcode”。这一步由 `--ptx-isa` 提供的 PTX ISA map 完成：

- `ptx_opcode`：PTX 指令名（例如 `mov/add/ld/ret`）
- `type_mod`：类型修饰（例如 `u32/u64`；空字符串表示 wildcard）
- `operand_kinds`：该 PTX form 期望的操作数种类序列（例如 `reg/imm/symbol/addr/pred`）
- `ir_op`：映射到的 IR opcode（后续用 `--inst-desc` 查语义并展开 uops）

示例：同一个 PTX opcode `mov.u32` 可能有多种 form，但都可以映射到同一个 IR opcode `mov`：

```json
{
  "ptx_opcode": "mov",
  "type_mod": "u32",
  "operand_kinds": ["reg", "imm"],
  "ir_op": "mov"
}
```

```json
{
  "ptx_opcode": "mov",
  "type_mod": "u32",
  "operand_kinds": ["reg", "symbol"],
  "ir_op": "mov"
}
```

mapper 会按 `operand_kinds` 尝试解析 token：

- `mov.u32 %r0, 0;` → 匹配 `(reg,imm)`
- `mov.u32 %r0, foo;` → 匹配 `(reg,symbol)`

如果同一条 PTX 指令被多条 entry 同时匹配，会报 `DESC_AMBIGUOUS`，并在 message 里列出所有匹配到的候选签名（`(operand_kinds) -> ir_op.type_mod`），便于你把 PTX ISA map 调整到“只有一条能匹配”。

注意：`--ptx-isa` 只决定 PTX→IR 的映射；真正执行语义来自 `--inst-desc`（IR opcode → uops）。因此你在 `--ptx-isa` 里引入新的 `ir_op` 或新的 operand form 时，也需要在 inst_desc 里提供对应语义条目。

## Kernel I/O 演示（`--io-demo`）

用途
- 验证：`.param` 参数输入 → kernel 写 global memory → host 显式 D2H 回读

运行

```bash
./build/gpu-sim-cli --ptx assets/ptx/write_out.ptx --io-demo
```

预期输出
- 终端打印：`io-demo u32 result: 42`
- `out/trace.jsonl` 与 `out/stats.json` 正常生成

## 测试（scripts/）

如需快速回归单元/集成测试，直接使用仓库根目录的脚本入口，见 [doc_user/scripts.md](scripts.md)。
