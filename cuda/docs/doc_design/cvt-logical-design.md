# cvt Logical Design

## 1. Goal

Enable ProtoGPU to execute clang-emitted PTX conversion scaffolding used around `warp_reduce_add`, with a minimal and safe `cvt` implementation.

## 2. Functional Contract (v1)

Supported canonical form:

```asm
cvt.rn.f32.u32 dst, src;
```

Behavior per participating lane:
1. Read `src` as `u32`.
2. Convert numerically to IEEE754 `f32`.
3. Write resulting `f32` bit pattern to `dst` register.

Participation remains `warp.active & uop.guard`.
Non-participating lanes are unchanged.

## 3. Parsing and Mapping

### 3.1 Frontend parser
- `%fN` operands are parsed as register operands with `F32` type.

### 3.2 PTX ISA map
- Add mapping for `ptx_opcode: cvt`, `type_mod: u32`, `operand_kinds: [reg, reg]`, `ir_op: cvt`.
- Rationale: current parser stores the last type token as `type_mod` for multi-type opcodes like `cvt.rn.f32.u32`.

### 3.3 Descriptor map
- Add descriptor for opcode `cvt`, type `f32`, operands `[reg, reg]`.
- Add descriptor for opcode `cvt`, type_mod `u32`, operands `[reg, reg]`.
- Expand to one EXEC uop: `CVT` (`in: [1]`, `out: [0]`).

## 4. Execution Semantics

`ExecCore` adds `case MicroOpOp::Cvt`:
1. Validate arity (1 input, 1 output).
2. Validate supported type pair for v1 (`dst=f32`, `src=u32`).
3. For each lane in execution mask, convert `u32` value to `float` and store `f32` bits.
4. Emit counter `uop.exec.cvt.f32.u32`.

## 5. Diagnostics

- Arity mismatch: `E_UOP_ARITY`.
- Unsupported conversion pair: `E_UOP_TYPE`.
- Unknown special operand path remains `E_SPECIAL_UNKNOWN`.

## 6. Compatibility Notes

- PTX flags like `.rn` are parsed but ignored by v1 execution (default round-to-nearest-even behavior of C++ cast is sufficient for the current demo range).
- `cvt` variants not required by current generated PTX are intentionally rejected for now.
