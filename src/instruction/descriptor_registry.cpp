#include "gpusim/instruction_desc.h"

#include "gpusim/json.h"

#include <fstream>
#include <sstream>

namespace gpusim {

static MicroOpOp parse_uop_op(const std::string& s) {
  if (s == "MOV") return MicroOpOp::Mov;
  if (s == "ADD") return MicroOpOp::Add;
  if (s == "LD") return MicroOpOp::Ld;
  if (s == "ST") return MicroOpOp::St;
  if (s == "RET") return MicroOpOp::Ret;
  return MicroOpOp::Mov;
}

static MicroOpKind parse_uop_kind(const std::string& s) {
  if (s == "EXEC") return MicroOpKind::Exec;
  if (s == "CTRL") return MicroOpKind::Control;
  if (s == "MEM") return MicroOpKind::Mem;
  return MicroOpKind::Exec;
}

std::string operand_kind_to_string(OperandKind k) {
  switch (k) {
  case OperandKind::Reg: return "reg";
  case OperandKind::Pred: return "pred";
  case OperandKind::Imm: return "imm";
  case OperandKind::Addr: return "addr";
  case OperandKind::Symbol: return "symbol";
  case OperandKind::Special: return "special";
  }
  return "imm";
}

void DescriptorRegistry::load_json_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    throw std::runtime_error("DescriptorRegistry: cannot open " + path);
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  load_json_text(ss.str());
}

void DescriptorRegistry::load_json_text(const std::string& text) {
  using namespace gpusim::json;
  descs_.clear();

  auto root = parse(text);
  const auto& obj = root.as_object();
  const auto& insts = obj.at("insts").as_array();
  for (const auto& iv : insts) {
    const auto& io = iv.as_object();
    InstDesc d;
    d.opcode = io.at("opcode").as_string();
    d.type_mod = io.at("type_mod").as_string();
    for (const auto& ok : io.at("operand_kinds").as_array()) {
      d.operand_kinds.push_back(ok.as_string());
    }
    for (const auto& uv : io.at("uops").as_array()) {
      const auto& uo = uv.as_object();
      UopTemplate ut;
      ut.kind = parse_uop_kind(uo.at("kind").as_string());
      ut.op = parse_uop_op(uo.at("op").as_string());
      for (const auto& idx : uo.at("in").as_array()) {
        ut.in_operand_idx.push_back(static_cast<int>(idx.as_i64()));
      }
      for (const auto& idx : uo.at("out").as_array()) {
        ut.out_operand_idx.push_back(static_cast<int>(idx.as_i64()));
      }
      d.uops.push_back(std::move(ut));
    }
    descs_.push_back(std::move(d));
  }
}

static std::vector<std::string> inst_operand_kinds(const InstRecord& inst) {
  std::vector<std::string> out;
  out.reserve(inst.operands.size());
  for (const auto& op : inst.operands) {
    out.push_back(operand_kind_to_string(op.kind));
  }
  return out;
}

std::optional<InstDesc> DescriptorRegistry::lookup(const InstRecord& inst) const {
  auto kinds = inst_operand_kinds(inst);
  for (const auto& d : descs_) {
    if (d.opcode != inst.opcode) continue;
    if (!d.type_mod.empty() && d.type_mod != inst.mods.type_mod) continue;
    if (d.operand_kinds != kinds) continue;
    return d;
  }
  return std::nullopt;
}

} // namespace gpusim
