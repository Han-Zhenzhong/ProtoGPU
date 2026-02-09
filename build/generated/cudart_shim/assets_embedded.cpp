// Auto-generated. Do not edit.
#include <cstdint>

namespace gpusim_cudart_shim::embedded {
static const char kConfigJson[] = R"GPUSIM_CFG(
{
  "sim": {
    "warp_size": 32,
    "max_steps": 100000
  },
  "observability": {
    "enabled": true,
    "trace_capacity": 65536
  }
}

)GPUSIM_CFG";

static const char kPtxIsaJson[] = R"GPUSIM_ISA(
{
  "insts": [
    {
      "ptx_opcode": "mov",
      "type_mod": "u32",
      "operand_kinds": ["reg", "imm"],
      "ir_op": "mov"
    },
    {
      "ptx_opcode": "mov",
      "type_mod": "u32",
      "operand_kinds": ["reg", "special"],
      "ir_op": "mov"
    },
    {
      "ptx_opcode": "mov",
      "type_mod": "u32",
      "operand_kinds": ["reg", "symbol"],
      "ir_op": "mov"
    },
    {
      "ptx_opcode": "mov",
      "type_mod": "u32",
      "operand_kinds": ["reg", "reg"],
      "ir_op": "mov"
    },
    {
      "ptx_opcode": "mov",
      "type_mod": "u64",
      "operand_kinds": ["reg", "reg"],
      "ir_op": "mov"
    },
    {
      "ptx_opcode": "mov",
      "type_mod": "u64",
      "operand_kinds": ["reg", "symbol"],
      "ir_op": "mov"
    },
    {
      "ptx_opcode": "mov",
      "type_mod": "u64",
      "operand_kinds": ["reg", "imm"],
      "ir_op": "mov"
    },
    {
      "ptx_opcode": "cvta",
      "type_mod": "u64",
      "operand_kinds": ["reg", "reg"],
      "ir_op": "mov"
    },
    {
      "ptx_opcode": "add",
      "type_mod": "u32",
      "operand_kinds": ["reg", "reg", "reg"],
      "ir_op": "add"
    },
    {
      "ptx_opcode": "add",
      "type_mod": "s32",
      "operand_kinds": ["reg", "reg", "reg"],
      "ir_op": "add"
    },
    {
      "ptx_opcode": "add",
      "type_mod": "u64",
      "operand_kinds": ["reg", "reg", "reg"],
      "ir_op": "add"
    },
    {
      "ptx_opcode": "add",
      "type_mod": "s64",
      "operand_kinds": ["reg", "reg", "reg"],
      "ir_op": "add"
    },
    {
      "ptx_opcode": "ld",
      "type_mod": "u64",
      "operand_kinds": ["reg", "symbol"],
      "ir_op": "ld"
    },
    {
      "ptx_opcode": "ld",
      "type_mod": "u32",
      "operand_kinds": ["reg", "symbol"],
      "ir_op": "ld"
    },
    {
      "ptx_opcode": "ld",
      "type_mod": "f32",
      "operand_kinds": ["reg", "addr"],
      "ir_op": "ld"
    },
    {
      "ptx_opcode": "ld",
      "type_mod": "u32",
      "operand_kinds": ["reg", "addr"],
      "ir_op": "ld"
    },
    {
      "ptx_opcode": "ld",
      "type_mod": "u64",
      "operand_kinds": ["reg", "addr"],
      "ir_op": "ld"
    },
    {
      "ptx_opcode": "st",
      "type_mod": "u32",
      "operand_kinds": ["addr", "reg"],
      "ir_op": "st"
    },
    {
      "ptx_opcode": "st",
      "type_mod": "f32",
      "operand_kinds": ["addr", "reg"],
      "ir_op": "st"
    },
    {
      "ptx_opcode": "st",
      "type_mod": "u64",
      "operand_kinds": ["addr", "reg"],
      "ir_op": "st"
    },
    {
      "ptx_opcode": "mul",
      "type_mod": "u32",
      "operand_kinds": ["reg", "reg", "reg"],
      "ir_op": "mul"
    },
    {
      "ptx_opcode": "mul",
      "type_mod": "s32",
      "operand_kinds": ["reg", "reg", "reg"],
      "ir_op": "mul"
    },
    {
      "ptx_opcode": "mul",
      "type_mod": "u32",
      "operand_kinds": ["reg", "reg", "imm"],
      "ir_op": "mul"
    },
    {
      "ptx_opcode": "fma",
      "type_mod": "f32",
      "operand_kinds": ["reg", "reg", "reg", "reg"],
      "ir_op": "fma"
    },
    {
      "ptx_opcode": "fma",
      "type_mod": "f32",
      "operand_kinds": ["reg", "reg", "reg", "imm"],
      "ir_op": "fma"
    },
    {
      "ptx_opcode": "setp",
      "type_mod": "s32",
      "operand_kinds": ["pred", "reg", "reg"],
      "ir_op": "setp"
    },
    {
      "ptx_opcode": "setp",
      "type_mod": "u32",
      "operand_kinds": ["pred", "reg", "reg"],
      "ir_op": "setp"
    },
    {
      "ptx_opcode": "shl",
      "type_mod": "",
      "operand_kinds": ["reg", "reg", "imm"],
      "ir_op": "shl"
    },
    {
      "ptx_opcode": "bra",
      "type_mod": "",
      "operand_kinds": ["imm"],
      "ir_op": "bra"
    },
    {
      "ptx_opcode": "ret",
      "type_mod": "",
      "operand_kinds": [],
      "ir_op": "ret"
    }
  ]
}

)GPUSIM_ISA";

static const char kInstDescJson[] = R"GPUSIM_DESC(
{
  "insts": [
    {
      "opcode": "mov",
      "type_mod": "u32",
      "operand_kinds": ["reg", "imm"],
      "uops": [
        { "kind": "EXEC", "op": "MOV", "in": [1], "out": [0] }
      ]
    },
    {
      "opcode": "mov",
      "type_mod": "u32",
      "operand_kinds": ["reg", "special"],
      "uops": [
        { "kind": "EXEC", "op": "MOV", "in": [1], "out": [0] }
      ]
    },
    {
      "opcode": "mov",
      "type_mod": "u32",
      "operand_kinds": ["reg", "reg"],
      "uops": [
        { "kind": "EXEC", "op": "MOV", "in": [1], "out": [0] }
      ]
    },
    {
      "opcode": "mov",
      "type_mod": "u64",
      "operand_kinds": ["reg", "reg"],
      "uops": [
        { "kind": "EXEC", "op": "MOV", "in": [1], "out": [0] }
      ]
    },
    {
      "opcode": "mov",
      "type_mod": "u64",
      "operand_kinds": ["reg", "symbol"],
      "uops": [
        { "kind": "EXEC", "op": "MOV", "in": [1], "out": [0] }
      ]
    },
    {
      "opcode": "mov",
      "type_mod": "u64",
      "operand_kinds": ["reg", "imm"],
      "uops": [
        { "kind": "EXEC", "op": "MOV", "in": [1], "out": [0] }
      ]
    },
    {
      "opcode": "add",
      "type_mod": "u32",
      "operand_kinds": ["reg", "reg", "reg"],
      "uops": [
        { "kind": "EXEC", "op": "ADD", "in": [1, 2], "out": [0] }
      ]
    },
    {
      "opcode": "add",
      "type_mod": "s32",
      "operand_kinds": ["reg", "reg", "reg"],
      "uops": [
        { "kind": "EXEC", "op": "ADD", "in": [1, 2], "out": [0] }
      ]
    },
    {
      "opcode": "add",
      "type_mod": "u64",
      "operand_kinds": ["reg", "reg", "reg"],
      "uops": [
        { "kind": "EXEC", "op": "ADD", "in": [1, 2], "out": [0] }
      ]
    },
    {
      "opcode": "add",
      "type_mod": "s64",
      "operand_kinds": ["reg", "reg", "reg"],
      "uops": [
        { "kind": "EXEC", "op": "ADD", "in": [1, 2], "out": [0] }
      ]
    },
    {
      "opcode": "ret",
      "type_mod": "",
      "operand_kinds": [],
      "uops": [
        { "kind": "CTRL", "op": "RET", "in": [], "out": [] }
      ]
    },
    {
      "opcode": "ld",
      "type_mod": "u64",
      "operand_kinds": ["reg", "symbol"],
      "uops": [
        { "kind": "MEM", "op": "LD", "in": [1], "out": [0] }
      ]
    },
    {
      "opcode": "ld",
      "type_mod": "u32",
      "operand_kinds": ["reg", "symbol"],
      "uops": [
        { "kind": "MEM", "op": "LD", "in": [1], "out": [0] }
      ]
    },
    {
      "opcode": "ld",
      "type_mod": "f32",
      "operand_kinds": ["reg", "addr"],
      "uops": [
        { "kind": "MEM", "op": "LD", "in": [1], "out": [0] }
      ]
    },
    {
      "opcode": "ld",
      "type_mod": "u32",
      "operand_kinds": ["reg", "addr"],
      "uops": [
        { "kind": "MEM", "op": "LD", "in": [1], "out": [0] }
      ]
    },
    {
      "opcode": "ld",
      "type_mod": "u64",
      "operand_kinds": ["reg", "addr"],
      "uops": [
        { "kind": "MEM", "op": "LD", "in": [1], "out": [0] }
      ]
    },
    {
      "opcode": "st",
      "type_mod": "u32",
      "operand_kinds": ["addr", "reg"],
      "uops": [
        { "kind": "MEM", "op": "ST", "in": [0, 1], "out": [] }
      ]
    },
    {
      "opcode": "st",
      "type_mod": "f32",
      "operand_kinds": ["addr", "reg"],
      "uops": [
        { "kind": "MEM", "op": "ST", "in": [0, 1], "out": [] }
      ]
    },
    {
      "opcode": "st",
      "type_mod": "u64",
      "operand_kinds": ["addr", "reg"],
      "uops": [
        { "kind": "MEM", "op": "ST", "in": [0, 1], "out": [] }
      ]
    },
    {
      "opcode": "mul",
      "type_mod": "u32",
      "operand_kinds": ["reg", "reg", "reg"],
      "uops": [
        { "kind": "EXEC", "op": "MUL", "in": [1, 2], "out": [0] }
      ]
    },
    {
      "opcode": "mul",
      "type_mod": "s32",
      "operand_kinds": ["reg", "reg", "reg"],
      "uops": [
        { "kind": "EXEC", "op": "MUL", "in": [1, 2], "out": [0] }
      ]
    },
    {
      "opcode": "mul",
      "type_mod": "u32",
      "operand_kinds": ["reg", "reg", "imm"],
      "uops": [
        { "kind": "EXEC", "op": "MUL", "in": [1, 2], "out": [0] }
      ]
    },
    {
      "opcode": "fma",
      "type_mod": "f32",
      "operand_kinds": ["reg", "reg", "reg", "reg"],
      "uops": [
        { "kind": "EXEC", "op": "FMA", "in": [1, 2, 3], "out": [0] }
      ]
    },
    {
      "opcode": "fma",
      "type_mod": "f32",
      "operand_kinds": ["reg", "reg", "reg", "imm"],
      "uops": [
        { "kind": "EXEC", "op": "FMA", "in": [1, 2, 3], "out": [0] }
      ]
    },
    {
      "opcode": "setp",
      "type_mod": "s32",
      "operand_kinds": ["pred", "reg", "reg"],
      "uops": [
        { "kind": "EXEC", "op": "SETP", "in": [1, 2], "out": [0] }
      ]
    },
    {
      "opcode": "setp",
      "type_mod": "u32",
      "operand_kinds": ["pred", "reg", "reg"],
      "uops": [
        { "kind": "EXEC", "op": "SETP", "in": [1, 2], "out": [0] }
      ]
    },
    {
      "opcode": "shl",
      "type_mod": "",
      "operand_kinds": ["reg", "reg", "imm"],
      "uops": [
        { "kind": "EXEC", "op": "SHL", "in": [1, 2], "out": [0] }
      ]
    },
    {
      "opcode": "bra",
      "type_mod": "",
      "operand_kinds": ["imm"],
      "uops": [
        { "kind": "CTRL", "op": "BRA", "in": [0], "out": [] }
      ]
    }
  ]
}

)GPUSIM_DESC";

const char* config_json() { return kConfigJson; }
const char* ptx_isa_json() { return kPtxIsaJson; }
const char* inst_desc_json() { return kInstDescJson; }
} // namespace gpusim_cudart_shim::embedded
