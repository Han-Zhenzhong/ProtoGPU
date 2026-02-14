# src/observability/

> Chinese version: [README.zh-CN.md](README.zh-CN.md)

Observability and replay/debugging support.

## Modules

- ObsControl: unified `emit` entry point; supports filtering/sampling/breakpoints
- TraceBuffer: event storage and export
- Counters: statistics counters and snapshots

## External contract

- Other modules should depend only on the ObsControl `emit` contract, and not directly on TraceBuffer/Counters.
