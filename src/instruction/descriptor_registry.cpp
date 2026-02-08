#include "gpusim/instruction_desc.h"

#include "gpusim/json.h"

#include <fstream>
#include <unordered_set>
#include <sstream>

namespace gpusim {

static MicroOpOp parse_uop_op(const std::string& s) {
  if (s == "MOV") return MicroOpOp::Mov;
  if (s == "ADD") return MicroOpOp::Add;
  if (s == "MUL") return MicroOpOp::Mul;
  if (s == "FMA") return MicroOpOp::Fma;
  if (s == "SETP") return MicroOpOp::Setp;
  if (s == "LD") return MicroOpOp::Ld;
  if (s == "ST") return MicroOpOp::St;
  if (s == "BRA") return MicroOpOp::Bra;
  if (s == "RET") return MicroOpOp::Ret;
  throw std::runtime_error("DescriptorRegistry: unknown uop op: " + s);
}

static MicroOpKind parse_uop_kind(const std::string& s) {
  if (s == "EXEC") return MicroOpKind::Exec;
  if (s == "CTRL") return MicroOpKind::Control;
  if (s == "MEM") return MicroOpKind::Mem;
  throw std::runtime_error("DescriptorRegistry: unknown uop kind: " + s);
}

static void validate_known_keys(const gpusim::json::Object& obj,
                                const std::unordered_set<std::string>& allowed,
                                const std::string& where) {
  for (const auto& kv : obj) {
    if (allowed.find(kv.first) != allowed.end()) continue;

    std::string allowed_list;
    bool first = true;
    for (const auto& k : allowed) {
      if (!first) allowed_list += ", ";
      allowed_list += k;
      first = false;
    }

    throw std::runtime_error("DescriptorRegistry: unknown key '" + kv.first + "' at " + where +
                             "; allowed keys: [" + allowed_list + "]");
  }
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
  load_json_file(path, DescriptorRegistryLoadOptions{});
}

void DescriptorRegistry::load_json_file(const std::string& path, const DescriptorRegistryLoadOptions& options) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    throw std::runtime_error("DescriptorRegistry: cannot open " + path);
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  load_json_text(ss.str(), options);
}

void DescriptorRegistry::load_json_text(const std::string& text) {
  load_json_text(text, DescriptorRegistryLoadOptions{});
}

void DescriptorRegistry::load_json_text(const std::string& text, const DescriptorRegistryLoadOptions& options) {
  using namespace gpusim::json;
  descs_.clear();

  auto root = parse(text);
  const auto& obj = root.as_object();

  if (options.strict_keys) {
    validate_known_keys(obj, {"insts"}, "root");
  }

  const auto& insts = obj.at("insts").as_array();
  for (std::size_t inst_idx = 0; inst_idx < insts.size(); ++inst_idx) {
    const auto& iv = insts[inst_idx];
    const auto& io = iv.as_object();

    if (options.strict_keys) {
      validate_known_keys(io, {"opcode", "type_mod", "operand_kinds", "uops"},
                          "insts[" + std::to_string(inst_idx) + "]");
    }

    InstDesc d;
    d.opcode = io.at("opcode").as_string();
    d.type_mod = io.at("type_mod").as_string();
    for (const auto& ok : io.at("operand_kinds").as_array()) {
      d.operand_kinds.push_back(ok.as_string());
    }
    const auto& uops = io.at("uops").as_array();
    for (std::size_t uop_idx = 0; uop_idx < uops.size(); ++uop_idx) {
      const auto& uv = uops[uop_idx];
      const auto& uo = uv.as_object();

      if (options.strict_keys) {
        validate_known_keys(uo, {"kind", "op", "in", "out"},
                            "insts[" + std::to_string(inst_idx) + "].uops[" + std::to_string(uop_idx) + "]");
      }

      UopTemplate ut;
      try {
        ut.kind = parse_uop_kind(uo.at("kind").as_string());
        ut.op = parse_uop_op(uo.at("op").as_string());
      } catch (const std::exception& e) {
        throw std::runtime_error("DescriptorRegistry: insts[" + std::to_string(inst_idx) + "].uops[" +
                                 std::to_string(uop_idx) + "]: " + e.what());
      }
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
