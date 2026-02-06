# src/units/

基础执行单元：执行 micro-op 的具体语义。

包含模块
- ExecCore：算术/逻辑/比较/转换/选择等 Exec 类 micro-op
- ControlUnit：分支与 PC 更新、reconvergence stack
- MemUnit：LD/ST/ATOM/FENCE/BARRIER，通过 memory 子系统访问

对外契约
- 只通过 MicroOp 与上下文读写寄存器/谓词/PC 等状态。
