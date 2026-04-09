#include "gpusim/ptx_isa.h"

#include <iostream>
#include <string>
#include <vector>

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

#define EXPECT_EQ(a, b) \
  do { \
    const auto& _a = (a); \
    const auto& _b = (b); \
    if (!(_a == _b)) { \
      fail(std::string("EXPECT_EQ failed: ") + #a + " != " + #b, __FILE__, __LINE__); \
    } \
  } while (0)

#define EXPECT_CONTAINS(haystack, needle) \
  do { \
    const std::string _h = (haystack); \
    const std::string _n = (needle); \
    if (_h.find(_n) == std::string::npos) { \
      fail(std::string("EXPECT_CONTAINS failed: missing '") + _n + "' in '" + _h + "'", __FILE__, __LINE__); \
    } \
  } while (0)

std::optional<gpusim::Diagnostic> map_one(const std::string& isa_json, const gpusim::TokenizedPtxInst& inst, gpusim::InstRecord& out) {
  gpusim::PtxIsaRegistry isa;
  isa.load_json_text(isa_json);

  gpusim::PtxIsaMapper mapper;
  std::vector<gpusim::InstRecord> out_insts;
  auto diag = mapper.map_kernel({inst}, isa, out_insts);
  if (diag) return diag;
  EXPECT_EQ(out_insts.size(), static_cast<std::size_t>(1));
  out = out_insts[0];
  return std::nullopt;
}

} // namespace

int main() {
  using namespace gpusim;

  {
    const std::string isa_json = R"JSON(
    {
      "insts": [
        { "ptx_opcode": "mov", "type_mod": "u32", "operand_kinds": ["reg", "imm"], "ir_op": "mov" },
        { "ptx_opcode": "mov", "type_mod": "u32", "operand_kinds": ["reg", "special"], "ir_op": "mov" },
        { "ptx_opcode": "mov", "type_mod": "u32", "operand_kinds": ["reg", "symbol"], "ir_op": "mov" }
      ]
    }
    )JSON";

    TokenizedPtxInst inst;
    inst.ptx_opcode = "mov";
    inst.mods.type_mod = "u32";
    inst.operand_tokens = {"%r0", "0"};

    InstRecord ir;
    auto diag = map_one(isa_json, inst, ir);
    EXPECT_TRUE(!diag.has_value());
    EXPECT_EQ(ir.opcode, std::string("mov"));
    EXPECT_EQ(ir.operands.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(ir.operands[0].kind, OperandKind::Reg);
    EXPECT_EQ(ir.operands[0].reg_id, 0);
    EXPECT_EQ(ir.operands[1].kind, OperandKind::Imm);
    EXPECT_EQ(ir.operands[1].imm_i64, 0);
  }

  {
    const std::string isa_json = R"JSON(
    {
      "insts": [
        { "ptx_opcode": "mov", "type_mod": "u32", "operand_kinds": ["reg", "special"], "ir_op": "mov" }
      ]
    }
    )JSON";

    TokenizedPtxInst inst;
    inst.ptx_opcode = "mov";
    inst.mods.type_mod = "u32";
    inst.operand_tokens = {"%r0", "%tid.x"};

    InstRecord ir;
    auto diag = map_one(isa_json, inst, ir);
    EXPECT_TRUE(!diag.has_value());
    EXPECT_EQ(ir.opcode, std::string("mov"));
    EXPECT_EQ(ir.operands.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(ir.operands[1].kind, OperandKind::Special);
    EXPECT_EQ(ir.operands[1].special, std::string("tid.x"));
  }

  {
    const std::string isa_json = R"JSON(
    {
      "insts": [
        { "ptx_opcode": "mov", "type_mod": "u32", "operand_kinds": ["reg", "special"], "ir_op": "mov" }
      ]
    }
    )JSON";

    TokenizedPtxInst inst;
    inst.ptx_opcode = "mov";
    inst.mods.type_mod = "u32";
    inst.operand_tokens = {"%r0", "%laneid"};

    InstRecord ir;
    auto diag = map_one(isa_json, inst, ir);
    EXPECT_TRUE(!diag.has_value());
    EXPECT_EQ(ir.operands.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(ir.operands[1].kind, OperandKind::Special);
    EXPECT_EQ(ir.operands[1].special, std::string("laneid"));
  }

  {
    const std::string isa_json = R"JSON(
    {
      "insts": [
        { "ptx_opcode": "mov", "type_mod": "u32", "operand_kinds": ["reg", "special"], "ir_op": "mov" }
      ]
    }
    )JSON";

    TokenizedPtxInst inst;
    inst.ptx_opcode = "mov";
    inst.mods.type_mod = "u32";
    inst.operand_tokens = {"%r0", "%warpid"};

    InstRecord ir;
    auto diag = map_one(isa_json, inst, ir);
    EXPECT_TRUE(!diag.has_value());
    EXPECT_EQ(ir.operands.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(ir.operands[1].kind, OperandKind::Special);
    EXPECT_EQ(ir.operands[1].special, std::string("warpid"));
  }

  {
    const std::string isa_json = R"JSON(
    {
      "insts": [
        { "ptx_opcode": "mov", "type_mod": "u32", "operand_kinds": ["reg", "special"], "ir_op": "mov" }
      ]
    }
    )JSON";

    TokenizedPtxInst inst;
    inst.ptx_opcode = "mov";
    inst.mods.type_mod = "u32";
    inst.operand_tokens = {"%r0", "%tid.x"};

    InstRecord ir;
    auto diag = map_one(isa_json, inst, ir);
    EXPECT_TRUE(!diag.has_value());
    EXPECT_EQ(ir.opcode, std::string("mov"));
    EXPECT_EQ(ir.operands.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(ir.operands[0].kind, OperandKind::Reg);
    EXPECT_EQ(ir.operands[1].kind, OperandKind::Special);
    EXPECT_EQ(ir.operands[1].special, std::string("tid.x"));
  }

  {
    const std::string isa_json = R"JSON(
    {
      "insts": [
        { "ptx_opcode": "mov", "type_mod": "u32", "operand_kinds": ["reg", "imm"], "ir_op": "mov" },
        { "ptx_opcode": "mov", "type_mod": "u32", "operand_kinds": ["reg", "symbol"], "ir_op": "mov" }
      ]
    }
    )JSON";

    TokenizedPtxInst inst;
    inst.ptx_opcode = "mov";
    inst.mods.type_mod = "u32";
    inst.operand_tokens = {"%r0", "foo"};

    InstRecord ir;
    auto diag = map_one(isa_json, inst, ir);
    EXPECT_TRUE(!diag.has_value());
    EXPECT_EQ(ir.opcode, std::string("mov"));
    EXPECT_EQ(ir.operands.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(ir.operands[1].kind, OperandKind::Symbol);
    EXPECT_EQ(ir.operands[1].symbol, std::string("foo"));
  }

  {
    const std::string isa_json = R"JSON(
    {
      "insts": [
        { "ptx_opcode": "mov", "type_mod": "u32", "operand_kinds": ["reg", "imm"], "ir_op": "mov" },
        { "ptx_opcode": "mov", "type_mod": "u32", "operand_kinds": ["reg", "symbol"], "ir_op": "mov" }
      ]
    }
    )JSON";

    TokenizedPtxInst inst;
    inst.ptx_opcode = "mov";
    inst.mods.type_mod = "u32";
    inst.operand_tokens = {"%r0", "%r1"};

    PtxIsaRegistry isa;
    isa.load_json_text(isa_json);

    PtxIsaMapper mapper;
    std::vector<InstRecord> out_insts;
    auto diag = mapper.map_kernel({inst}, isa, out_insts);
    EXPECT_TRUE(diag.has_value());
    if (diag) {
      EXPECT_EQ(diag->code, std::string("OPERAND_FORM_MISMATCH"));
      EXPECT_CONTAINS(diag->message, "mov");
    }
  }

  {
    const std::string isa_json = R"JSON(
    {
      "insts": [
        { "ptx_opcode": "mov", "type_mod": "u32", "operand_kinds": ["reg", "imm"], "ir_op": "mov" },
        { "ptx_opcode": "mov", "type_mod": "u32", "operand_kinds": ["reg", "imm"], "ir_op": "mov_alt" }
      ]
    }
    )JSON";

    TokenizedPtxInst inst;
    inst.ptx_opcode = "mov";
    inst.mods.type_mod = "u32";
    inst.operand_tokens = {"%r0", "0"};

    PtxIsaRegistry isa;
    isa.load_json_text(isa_json);

    PtxIsaMapper mapper;
    std::vector<InstRecord> out_insts;
    auto diag = mapper.map_kernel({inst}, isa, out_insts);

    EXPECT_TRUE(diag.has_value());
    if (diag) {
      EXPECT_EQ(diag->code, std::string("DESC_AMBIGUOUS"));
      EXPECT_CONTAINS(diag->message, "(reg,imm) -> mov.u32");
      EXPECT_CONTAINS(diag->message, "(reg,imm) -> mov_alt.u32");
    }
  }

  if (g_failures) {
    std::cerr << "FAILURES: " << g_failures << "\n";
    return 1;
  }
  std::cout << "OK\n";
  return 0;
}
