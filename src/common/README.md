# src/common/

> Chinese version: [README.zh-CN.md](README.zh-CN.md)

Cross-module contract layer.

## Scope

- Core types: InstRecord, MicroOp, Value, LaneMask, AddrSpace
- Identifiers and handles: IDs for kernel/cta/warp/thread/stream/event/cmd, etc.
- Unified error and diagnostic structures
- Global config structures (e.g., hardware spec, observability config)

## Constraints

- This directory should only contain definitions that are general-purpose and have no business-logic dependencies, to avoid reverse-dependencies on other modules.
