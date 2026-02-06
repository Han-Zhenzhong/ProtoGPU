## Plan: Refine WorkloadSpec Dev Plan

Update the existing WorkloadSpec dev plan to match current code reality: streaming runtime foundations are still missing, binder can’t select kernels by entry name yet, and there’s no JSON-schema validator wired in. The plan will add explicit prerequisite milestones (runtime streaming core + entry selection), tighten deterministic ID rules (including event-name→EventId), and make trace-field requirements explicit so WorkloadSpec runs are replayable and diagnosable.

### Steps 5
1. Re-scope milestones in `docs/doc_plan/plan_dev/plan-workload_spec_stream_input.md` to add a prerequisite “Runtime+Streaming foundations” before execution/tick work.
2. Add an explicit “Kernel entry selection” milestone referencing binder changes in `src/frontend/binder.cpp` and `include/gpusim/frontend.h` to support selecting by `entry`.
3. Adjust schema-validation steps to match actual JSON infrastructure in `src/common/json.cpp`, `include/gpusim/json.h`, and `schemas/` (decide: lightweight structural validator vs adding a schema engine later).
4. Strengthen deterministic ID assignment requirements (sorted keys + stable CmdId ordering) and explicitly include event-name→EventId mapping, aligned with fields available in `include/gpusim/observability.h`.
5. Make observability requirements concrete: require STREAM lifecycle events to populate `cmd_id/stream_id/event_id` consistently, and point to where those fields exist today in `include/gpusim/contracts.h` and are serialized in `src/observability/observability.cpp`.

### Further Considerations 3
1. Schema approach: keep JSON Schema files as spec only, or implement a minimal validator now?
2. CmdId rule: per-stream increment vs global increment (pick one; require stability).
3. Timeline consistency: do you require a single monotonic timestamp across all commands in one workload run?
