# src/units/

> Chinese version: [README.zh-CN.md](README.zh-CN.md)

Basic execution units: implement the concrete semantics of micro-ops.

## Modules

- ExecCore: arithmetic / logic / compare / convert / select (Exec-class micro-ops)
- ControlUnit: branching and PC updates; reconvergence stack
- MemUnit: LD/ST/ATOM/FENCE/BARRIER via the memory subsystem

## External contract

- Accesses registers/predicates/PC and other state only through MicroOp and the execution context.
