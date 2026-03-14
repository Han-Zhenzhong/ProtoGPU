# `warp_reduce_add` Instruction Specification

## 1. Overview

`warp_reduce_add` is a warp-level collective instruction that performs an addition reduction across the active lanes of a warp.

It is intended as an ISA-level abstraction for a common CUDA/PTX programming pattern currently implemented using a sequence of lane-exchange and arithmetic instructions such as `shfl.down.sync` plus `add`.

The instruction enables explicit representation of warp-local reduction semantics for ISA prototyping, compiler experimentation, and functional validation.

---

## 2. Purpose

The instruction computes the sum of one source register value contributed by each active lane in the current warp, and writes the reduction result back to a destination register.

This instruction is designed for:

- warp-level collective computation
- ISA prototyping and semantic validation
- compiler lowering experiments for common reduction patterns
- functional exploration of GPU instruction extensions

It is **not** intended to model cycle timing, pipeline usage, or hardware implementation cost.

---

## 3. Instruction Form

## 3.1 Assembly Syntax

```asm
warp_reduce_add dst, src;
````

## 3.2 In-place Form

```asm
warp_reduce_add r0, r0;
```

This form is expected to be the most common usage.

---

## 4. Operands

* `dst`
  Destination register in each lane.

* `src`
  Source register in each lane. Each active lane contributes its local `src` value to the warp-wide sum.

---

## 5. Operand Types

The instruction may be defined for the following types:

* `u32`
* `s32`
* `f32`

Optional future extensions:

* `u64`
* `s64`
* `f64`
* `f16`
* `bf16`

For an initial prototype, `f32` is recommended.

---

## 6. Execution Scope

The instruction operates on the **current warp** only.

* Scope: warp-local
* No inter-warp communication
* No block-wide or grid-wide reduction
* No memory access is performed

---

## 7. Active Lane Semantics

Only **active lanes** in the current warp participate in the reduction.

Let:

* `W` be the set of lanes in the current warp
* `A ⊆ W` be the set of active lanes at the point of execution
* `src[i]` be the source operand value of lane `i`

Then the reduction result is:

```text
SUM = Σ src[i], for all i in A
```

Inactive lanes do not contribute to the sum.

---

## 8. Result Semantics

Two semantic variants are possible.

## 8.1 Broadcast Variant

The reduction result is written to `dst` in **every active lane**.

For each lane `i ∈ A`:

```text
dst[i] = SUM
```

For each lane `i ∉ A`:

```text
dst[i] = unchanged
```

This variant is recommended for the initial prototype because it is simple, explicit, and easy to validate.

## 8.2 Leader-only Variant

The reduction result is written only to lane 0 or to the lowest active lane.

Example:

```text
dst[leader] = SUM
other active lanes = unchanged or undefined
```

This variant is closer to some optimized reduction idioms, but is less convenient for first-stage semantic prototyping.

### Recommended semantic choice

This specification adopts the **Broadcast Variant**.

---

## 9. Formal Semantics

For each warp instance:

### Inputs

* active mask `A`
* per-lane source register value `src[i]`

### Computation

```text
SUM = 0
for each lane i in warp:
    if lane i is active:
        SUM = SUM + src[i]
```

### Writeback

```text
for each lane i in warp:
    if lane i is active:
        dst[i] = SUM
    else:
        dst[i] = dst[i]   // unchanged
```

---

## 10. Pseudocode Semantics

```cpp
sum = 0;
for each lane i in current_warp:
    if active(i):
        sum += src[i];

for each lane i in current_warp:
    if active(i):
        dst[i] = sum;
```

---

## 11. Example

Assume a warp of 8 lanes for illustration:

```text
lane0: src = 1
lane1: src = 2
lane2: src = 3
lane3: src = 4
lane4: src = 5
lane5: src = 6
lane6: src = 7
lane7: src = 8
```

All lanes active.

Then:

```asm
warp_reduce_add r0, r0;
```

Produces:

```text
SUM = 1+2+3+4+5+6+7+8 = 36
```

Final state:

```text
lane0: r0 = 36
lane1: r0 = 36
lane2: r0 = 36
lane3: r0 = 36
lane4: r0 = 36
lane5: r0 = 36
lane6: r0 = 36
lane7: r0 = 36
```

---

## 12. Relationship to Existing Warp Reduction Code

A warp-local sum is commonly implemented in CUDA using shuffle-based tree reduction:

```cpp
x += __shfl_down_sync(mask, x, 16);
x += __shfl_down_sync(mask, x, 8);
x += __shfl_down_sync(mask, x, 4);
x += __shfl_down_sync(mask, x, 2);
x += __shfl_down_sync(mask, x, 1);
```

`warp_reduce_add` provides an ISA-level abstraction for this pattern.

Conceptually:

```text
shfl + add + shfl + add + ...
```

becomes:

```asm
warp_reduce_add x, x;
```

---

## 13. Intended Lowering in the Semantic Execution Framework

Within the semantic GPU execution framework, `warp_reduce_add` may be expanded into existing IR operations or micro-operations.

Example conceptual lowering:

```text
warp_reduce_add dst, src
    -> collect active lane values
    -> perform reduction in warp scope
    -> broadcast result to active lanes
    -> write back dst
```

Possible internal expansion:

```text
reduce_add.warp dst, src
```

or

```text
read_lane_values
accumulate_sum
broadcast_sum
write_dst
```

The exact internal representation is implementation-defined.

---

## 14. Side Effects

* No memory access
* No synchronization beyond warp-local collective semantics
* No modification of predicate state unless explicitly defined by implementation
* No effect on inactive lanes except preservation of prior register values

---

## 15. Control Flow Constraints

All participating lanes are expected to execute the instruction in converged warp context.

If some lanes in the warp do not reach the instruction due to divergence, the result is defined only over the active lanes executing the instruction.

This specification assumes standard SIMT active-mask semantics.

---

## 16. Exception and Error Behavior

For the semantic prototype:

* no exception is raised for partial warp participation
* no trap behavior is defined
* no undefined hardware timing behavior is modeled

Type mismatch, invalid register class, or malformed instruction encoding are handled by the front-end parser or decoder.

---

## 17. Determinism

Given:

* identical active mask
* identical source values
* identical operand type

the instruction result is deterministic.

---

## 18. Compiler Mapping

The instruction is intended to be generated from warp-level reduction patterns in compiler or PTX transformation passes.

Typical source-level mapping target:

* warp-local sum reduction
* reduction idioms implemented using shuffle chains
* subgroup add reduction patterns

Example transformation:

```text
shuffle-down reduction sequence
    -> warp_reduce_add
```

---

## 19. Non-Goals

This specification does **not** define:

* cycle-level latency
* throughput
* pipeline occupancy
* hardware crossbar design
* warp scheduler interaction
* power or area impact

These are outside the scope of the semantic execution framework.

---

## 20. Rationale

`warp_reduce_add` is chosen as a prototype instruction because:

* it represents a common GPU programming pattern
* it has clear and useful warp-level semantics
* it is visible from the compiler/programming-model perspective
* it can be mapped to existing IR operations
* it is suitable for functional validation without requiring a detailed microarchitectural model

This makes it a strong case-study instruction for demonstrating rapid GPU ISA prototyping.

---

## 21. Summary

`warp_reduce_add` is a warp-scope collective instruction that:

* reads one value from each active lane
* computes their sum
* writes the same reduction result to all active lanes

It provides a compact ISA representation for a common shuffle-based reduction idiom and serves as an effective demonstration target for semantic GPU ISA prototyping.
