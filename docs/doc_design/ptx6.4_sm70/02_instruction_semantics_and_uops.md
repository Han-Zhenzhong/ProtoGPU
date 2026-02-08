# 02 指令语义与 micro-ops（inst_desc 冻结契约）

本设计在 ../modules/03_instruction_system.md 的基础上，给出“PTX6.4 + sm70（功能级）”冻结子集在 IR 与 micro-op 层的**必须满足的契约**。

## 1. 两层资产分离（必须）

- PTX → IR：`assets/ptx_isa/*.json`
- IR → uops：`assets/inst_desc/*.json`

冻结原因
- 防止把 PTX 文本细节（mods/space/写法差异）耦合进执行器。
- 让“可交付口径”以资产+回归为中心演进。

## 2. IR op 命名与 key 规则

冻结口径
- `InstRecord.opcode` 在进入 descriptor lookup 前，应是 IR 级 opcode（由 ptx_isa map 决定）。
- descriptor lookup 的 key 必须至少包含：
  - `ir_op`（=InstRecord.opcode）
  - `type_mod`
  - `operand_kinds`

说明
- bring-up 阶段不要求把所有 PTX flags 映射成 descriptor key 的一部分；flags 应作为 attrs 透传给 uops（或被忽略），但不能影响“命中哪个语义”。

## 3. micro-op contracts（最小集合）

冻结子集（Tier-0/M1–M4）至少需要以下 uop 能力：
- Exec：`Mul/Fma/Setp`
- Mem：`Ld/St`（param/global 的子集）
- Control：`Bra/Ret`（M4，uniform-only）

关键约束
- 每个 uop 必须携带：
  - `kind`（Exec/Mem/Control）
  - `op`（枚举）
  - `attrs`（type/space/flags）
  - `guard`（由 predication 注入的 lane mask）

## 4. predication guard（冻结语义）

- `@%p` / `@!%p` 必须在 expand 阶段转为 `MicroOp.guard`。
- `exec_mask = warp.active_mask & uop.guard`。
- 若 `exec_mask` 全空：该 uop 是 no-op（但仍允许发出观测事件以便调试）。

## 5. fail-fast（强制）

加载阶段（DescriptorRegistry.load）
- schema 校验必须严格；不认识字段应报错（避免 silent mismatch）。
- unknown uop kind/op 必须直接报错（禁止“加载成功但执行期走默认分支”）。

lookup/expand 阶段
- miss/ambiguous 必须报错并返回可定位诊断。

## 6. Tier-0 form 与资产落点（默认）

默认资产文件
- PTX ISA map：`assets/ptx_isa/demo_ptx64.json`
- inst_desc：`assets/inst_desc/demo_desc.json`

Tier-0/M1–M4 的 form 列表以 ../../doc_spec/ptx64_baseline.md 为准；每次扩展以该列表为唯一真源。
