#include "gpusim/instruction_desc.h"

#include <functional>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void fail(const std::string& msg, const char* file, int line) {
  g_failures++;
  std::cerr << file << ":" << line << ": " << msg << "\n";
}

#define EXPECT_TRUE(cond) \
  do { \
    if (!(cond)) fail(std::string("EXPECT_TRUE failed: ") + #cond, __FILE__, __LINE__); \
  } while (0)

#define EXPECT_CONTAINS(haystack, needle) \
  do { \
    const std::string _h = (haystack); \
    const std::string _n = (needle); \
    if (_h.find(_n) == std::string::npos) { \
      fail(std::string("EXPECT_CONTAINS failed: missing '") + _n + "' in '" + _h + "'", __FILE__, __LINE__); \
    } \
  } while (0)

std::string must_throw_message(const std::function<void()>& f) {
  try {
    f();
  } catch (const std::exception& e) {
    return e.what();
  }
  fail("expected exception, but none was thrown", __FILE__, __LINE__);
  return "";
}

void test_non_strict_allows_unknown_keys() {
  using namespace gpusim;

  const std::string json = R"JSON(
  {
    "insts": [
      {
        "opcode": "mov",
        "type_mod": "u32",
        "operand_kinds": ["reg", "imm"],
        "uops": [
          { "kind": "EXEC", "op": "MOV", "in": [1], "out": [0], "extra_uop_key": 123 }
        ],
        "extra_inst_key": "ok"
      }
    ],
    "extra_root_key": true
  }
  )JSON";

  DescriptorRegistry reg;
  bool threw = false;
  try {
    reg.load_json_text(json);
  } catch (...) {
    threw = true;
  }
  EXPECT_TRUE(!threw);
}

void test_strict_rejects_unknown_keys() {
  using namespace gpusim;

  const std::string json = R"JSON(
  {
    "insts": [
      {
        "opcode": "mov",
        "type_mod": "u32",
        "operand_kinds": ["reg", "imm"],
        "uops": [
          { "kind": "EXEC", "op": "MOV", "in": [1], "out": [0] }
        ],
        "extra_inst_key": "boom"
      }
    ]
  }
  )JSON";

  DescriptorRegistryLoadOptions opt;
  opt.strict_keys = true;

  DescriptorRegistry reg;
  const std::string msg = must_throw_message([&]() { reg.load_json_text(json, opt); });
  EXPECT_CONTAINS(msg, "DescriptorRegistry:");
  EXPECT_CONTAINS(msg, "unknown key");
  EXPECT_CONTAINS(msg, "insts[0]");
}

void test_strict_rejects_unknown_root_key() {
  using namespace gpusim;

  const std::string json = R"JSON(
  {
    "insts": [
      {
        "opcode": "mov",
        "type_mod": "u32",
        "operand_kinds": ["reg", "imm"],
        "uops": [
          { "kind": "EXEC", "op": "MOV", "in": [1], "out": [0] }
        ]
      }
    ],
    "extra_root_key": 1
  }
  )JSON";

  DescriptorRegistryLoadOptions opt;
  opt.strict_keys = true;

  DescriptorRegistry reg;
  const std::string msg = must_throw_message([&]() { reg.load_json_text(json, opt); });
  EXPECT_CONTAINS(msg, "unknown key");
  EXPECT_CONTAINS(msg, "root");
}

void test_unknown_uop_has_context_in_message() {
  using namespace gpusim;

  const std::string json = R"JSON(
  {
    "insts": [
      {
        "opcode": "mov",
        "type_mod": "u32",
        "operand_kinds": ["reg", "imm"],
        "uops": [
          { "kind": "EXEC", "op": "__BOGUS_OP__", "in": [1], "out": [0] }
        ]
      }
    ]
  }
  )JSON";

  DescriptorRegistry reg;
  const std::string msg = must_throw_message([&]() { reg.load_json_text(json); });
  EXPECT_CONTAINS(msg, "insts[0]");
  EXPECT_CONTAINS(msg, "uops[0]");
  EXPECT_CONTAINS(msg, "unknown uop op");
}

void test_warp_reduce_add_uop_parses_and_looks_up() {
  using namespace gpusim;

  const std::string json = R"JSON(
  {
    "insts": [
      {
        "opcode": "warp_reduce_add",
        "type_mod": "f32",
        "operand_kinds": ["reg", "reg"],
        "uops": [
          { "kind": "EXEC", "op": "WARP_REDUCE_ADD", "in": [1], "out": [0] }
        ]
      }
    ]
  }
  )JSON";

  DescriptorRegistry reg;
  reg.load_json_text(json);

  InstRecord inst;
  inst.opcode = "warp_reduce_add";
  inst.mods.type_mod = "f32";

  Operand dst;
  dst.kind = OperandKind::Reg;
  dst.type = ValueType::F32;
  dst.reg_id = 0;

  Operand src;
  src.kind = OperandKind::Reg;
  src.type = ValueType::F32;
  src.reg_id = 1;

  inst.operands = {dst, src};

  const auto desc = reg.lookup(inst);
  EXPECT_TRUE(desc.has_value());
  if (desc) {
    EXPECT_TRUE(!desc->uops.empty());
    EXPECT_TRUE(desc->uops[0].op == MicroOpOp::WarpReduceAdd);
  }
}

void test_cvt_uop_parses_and_looks_up() {
  using namespace gpusim;

  const std::string json = R"JSON(
  {
    "insts": [
      {
        "opcode": "cvt",
        "type_mod": "u32",
        "operand_kinds": ["reg", "reg"],
        "uops": [
          { "kind": "EXEC", "op": "CVT", "in": [1], "out": [0] }
        ]
      }
    ]
  }
  )JSON";

  DescriptorRegistry reg;
  reg.load_json_text(json);

  InstRecord inst;
  inst.opcode = "cvt";
  inst.mods.type_mod = "u32";

  Operand dst;
  dst.kind = OperandKind::Reg;
  dst.type = ValueType::F32;
  dst.reg_id = 0;

  Operand src;
  src.kind = OperandKind::Reg;
  src.type = ValueType::U32;
  src.reg_id = 1;

  inst.operands = {dst, src};

  const auto desc = reg.lookup(inst);
  EXPECT_TRUE(desc.has_value());
  if (desc) {
    EXPECT_TRUE(!desc->uops.empty());
    EXPECT_TRUE(desc->uops[0].op == MicroOpOp::Cvt);
  }
}

void test_shfl_uop_parses_and_looks_up() {
  using namespace gpusim;

  const std::string json = R"JSON(
  {
    "insts": [
      {
        "opcode": "shfl",
        "type_mod": "b32",
        "operand_kinds": ["reg", "reg", "reg", "reg", "reg"],
        "uops": [
          { "kind": "EXEC", "op": "SHFL", "in": [1, 2, 3, 4], "out": [0] }
        ]
      }
    ]
  }
  )JSON";

  DescriptorRegistry reg;
  reg.load_json_text(json);

  InstRecord inst;
  inst.opcode = "shfl";
  inst.mods.type_mod = "b32";
  inst.mods.flags = {"sync", "down"};

  Operand dst;
  dst.kind = OperandKind::Reg;
  dst.type = ValueType::F32;
  dst.reg_id = 0;

  Operand src;
  src.kind = OperandKind::Reg;
  src.type = ValueType::F32;
  src.reg_id = 1;

  Operand lane_or_idx;
  lane_or_idx.kind = OperandKind::Reg;
  lane_or_idx.type = ValueType::U32;
  lane_or_idx.reg_id = 2;

  Operand clamp;
  clamp.kind = OperandKind::Reg;
  clamp.type = ValueType::U32;
  clamp.reg_id = 3;

  Operand member;
  member.kind = OperandKind::Reg;
  member.type = ValueType::U32;
  member.reg_id = 4;

  inst.operands = {dst, src, lane_or_idx, clamp, member};

  const auto desc = reg.lookup(inst);
  EXPECT_TRUE(desc.has_value());
  if (desc) {
    EXPECT_TRUE(!desc->uops.empty());
    EXPECT_TRUE(desc->uops[0].op == MicroOpOp::Shfl);
  }
}

} // namespace

int main() {
  test_non_strict_allows_unknown_keys();
  test_strict_rejects_unknown_keys();
  test_strict_rejects_unknown_root_key();
  test_unknown_uop_has_context_in_message();
  test_warp_reduce_add_uop_parses_and_looks_up();
  test_cvt_uop_parses_and_looks_up();
  test_shfl_uop_parses_and_looks_up();

  if (g_failures) {
    std::cerr << "FAILURES: " << g_failures << "\n";
    return 1;
  }
  std::cout << "OK\n";
  return 0;
}
