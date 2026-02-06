#include "gpusim/ptx_isa.h"

#include "gpusim/json.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace gpusim {

namespace {

std::string to_lower(std::string s) {
  for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return s;
}

std::string trim(std::string s) {
  auto not_ws = [](unsigned char ch) { return !std::isspace(ch); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_ws));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_ws).base(), s.end());
  return s;
}

bool parse_i64_auto_base(const std::string& s, std::int64_t& out) {
  try {
    std::size_t idx = 0;
    out = std::stoll(s, &idx, 0);
    return idx == s.size();
  } catch (...) {
    return false;
  }
}

bool parse_u64_dec(const std::string& s, std::int64_t& out) {
  if (s.empty()) return false;
  if (!std::all_of(s.begin(), s.end(), [](unsigned char ch) { return std::isdigit(ch); })) return false;
  out = std::stoll(s);
  return true;
}

std::string join_csv(const std::vector<std::string>& xs) {
  std::string out;
  for (std::size_t i = 0; i < xs.size(); i++) {
    if (i) out += ",";
    out += xs[i];
  }
  return out;
}

std::string fmt_sig(const PtxIsaEntry& e) {
  std::string out;
  out += "(" + join_csv(e.operand_kinds) + ")";
  out += " -> " + e.ir_op;
  if (!e.type_mod.empty()) out += "." + e.type_mod;
  else out += ".*";
  return out;
}

std::optional<Operand> parse_operand_by_kind(const std::string& kind,
                                            std::string tok,
                                            const std::string& type_mod,
                                            std::optional<Diagnostic>& diag,
                                            const std::optional<SourceLocation>& loc) {
  tok = trim(tok);
  if (!tok.empty() && tok.back() == ';') tok.pop_back();
  tok = trim(tok);
  if (tok.empty()) {
    diag = Diagnostic{ "frontend", "OPERAND_PARSE_FAIL", "empty operand token", loc, std::nullopt, std::nullopt };
    return std::nullopt;
  }

  Operand o;
  if (kind == "reg") {
    if (tok.rfind("%rd", 0) == 0) {
      std::int64_t id = -1;
      if (!parse_u64_dec(tok.substr(3), id)) {
        diag = Diagnostic{ "frontend", "OPERAND_PARSE_FAIL", "invalid %rd register: " + tok, loc, std::nullopt, std::nullopt };
        return std::nullopt;
      }
      o.kind = OperandKind::Reg;
      o.type = ValueType::U64;
      o.reg_id = id;
    } else if (tok.rfind("%r", 0) == 0) {
      std::int64_t id = -1;
      if (!parse_u64_dec(tok.substr(2), id)) {
        diag = Diagnostic{ "frontend", "OPERAND_PARSE_FAIL", "invalid %r register: " + tok, loc, std::nullopt, std::nullopt };
        return std::nullopt;
      }
      o.kind = OperandKind::Reg;
      o.type = ValueType::U32;
      o.reg_id = id;
    } else {
      return std::nullopt; // kind mismatch
    }

    if ((type_mod == "u64" || type_mod == "s64") && o.type == ValueType::U32) {
      o.type = ValueType::U64;
    }
    return o;
  }

  if (kind == "pred") {
    if (tok.rfind("%p", 0) != 0) return std::nullopt;
    std::int64_t id = -1;
    if (!parse_u64_dec(tok.substr(2), id)) {
      diag = Diagnostic{ "frontend", "OPERAND_PARSE_FAIL", "invalid %p predicate: " + tok, loc, std::nullopt, std::nullopt };
      return std::nullopt;
    }
    o.kind = OperandKind::Pred;
    o.pred_id = id;
    return o;
  }

  if (kind == "imm") {
    std::int64_t v = 0;
    if (!parse_i64_auto_base(tok, v)) return std::nullopt;
    o.kind = OperandKind::Imm;
    o.imm_i64 = v;
    return o;
  }

  if (kind == "addr") {
    if (tok.size() < 2 || tok.front() != '[' || tok.back() != ']') return std::nullopt;
    auto inner = trim(tok.substr(1, tok.size() - 2));

    std::string base = inner;
    std::int64_t off = 0;
    auto plus = inner.find('+');
    if (plus != std::string::npos) {
      base = trim(inner.substr(0, plus));
      auto off_s = trim(inner.substr(plus + 1));
      if (!parse_i64_auto_base(off_s, off)) {
        diag = Diagnostic{ "frontend", "OPERAND_PARSE_FAIL", "invalid addr offset: " + off_s, loc, std::nullopt, std::nullopt };
        return std::nullopt;
      }
    }

    std::optional<Diagnostic> nested_diag;
    auto base_op = parse_operand_by_kind("reg", base, "u64", nested_diag, loc);
    if (!base_op || base_op->kind != OperandKind::Reg) return std::nullopt;

    o.kind = OperandKind::Addr;
    o.type = ValueType::U64;
    o.reg_id = base_op->reg_id;
    o.imm_i64 = off;
    return o;
  }

  if (kind == "symbol") {
    if (!tok.empty() && tok.front() == '%') return std::nullopt;

    // Symbols must not overlap with immediates (e.g. "0", "-1", "0x10").
    // This is required to disambiguate forms like: mov.u32 (reg, imm) vs (reg, symbol).
    {
      std::int64_t v = 0;
      if (parse_i64_auto_base(tok, v)) return std::nullopt;
    }

    if (tok.size() >= 2 && tok.front() == '[' && tok.back() == ']') {
      auto inner = trim(tok.substr(1, tok.size() - 2));
      // Bracketed register form is address-like; do not treat as a symbol.
      if (!inner.empty() && inner.front() == '%') return std::nullopt;
      tok = inner;
    }
    if (tok.empty()) {
      diag = Diagnostic{ "frontend", "OPERAND_PARSE_FAIL", "empty symbol", loc, std::nullopt, std::nullopt };
      return std::nullopt;
    }
    o.kind = OperandKind::Symbol;
    o.symbol = tok;
    return o;
  }

  if (kind == "special") {
    if (tok.empty() || tok.front() != '%') return std::nullopt;
    // Let other kinds handle registers/preds.
    if (tok.rfind("%r", 0) == 0 || tok.rfind("%rd", 0) == 0 || tok.rfind("%p", 0) == 0) return std::nullopt;

    const auto dot = tok.find('.');
    if (dot == std::string::npos) return std::nullopt;

    const auto name = to_lower(tok.substr(1));
    const bool ok =
        name == "tid.x" || name == "tid.y" || name == "tid.z" ||
        name == "ntid.x" || name == "ntid.y" || name == "ntid.z" ||
        name == "ctaid.x" || name == "ctaid.y" || name == "ctaid.z" ||
        name == "nctaid.x" || name == "nctaid.y" || name == "nctaid.z" ||
        name == "laneid" || name == "warpid";
    if (!ok) {
      diag = Diagnostic{ "frontend", "OPERAND_PARSE_FAIL", "unknown special operand: " + tok, loc, std::nullopt, std::nullopt };
      return std::nullopt;
    }
    o.kind = OperandKind::Special;
    o.type = ValueType::U32;
    o.special = name;
    (void)type_mod;
    return o;
  }

  diag = Diagnostic{ "frontend", "OPERAND_PARSE_FAIL", "unknown operand kind: " + kind, loc, std::nullopt, std::nullopt };
  return std::nullopt;
}

} // namespace

void PtxIsaRegistry::load_json_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("PtxIsaRegistry: cannot open " + path);
  std::ostringstream ss;
  ss << f.rdbuf();
  load_json_text(ss.str());
}

void PtxIsaRegistry::load_json_text(const std::string& text) {
  using namespace gpusim::json;
  entries_.clear();

  auto root = parse(text);
  const auto& obj = root.as_object();
  const auto& insts = obj.at("insts").as_array();
  for (const auto& iv : insts) {
    const auto& io = iv.as_object();
    PtxIsaEntry e;
    e.ptx_opcode = to_lower(io.at("ptx_opcode").as_string());
    e.type_mod = to_lower(io.at("type_mod").as_string());
    for (const auto& ok : io.at("operand_kinds").as_array()) {
      e.operand_kinds.push_back(to_lower(ok.as_string()));
    }
    e.ir_op = io.at("ir_op").as_string();
    entries_.push_back(std::move(e));
  }
}

std::vector<const PtxIsaEntry*> PtxIsaRegistry::candidates(const std::string& ptx_opcode,
                                                          const std::string& type_mod,
                                                          std::size_t operand_count) const {
  const auto op = to_lower(ptx_opcode);
  const auto tm = to_lower(type_mod);
  std::vector<const PtxIsaEntry*> exact;
  std::vector<const PtxIsaEntry*> wildcard;
  for (const auto& e : entries_) {
    if (e.ptx_opcode != op) continue;
    if (e.operand_kinds.size() != operand_count) continue;
    if (!e.type_mod.empty() && e.type_mod != tm) continue;
    if (e.type_mod.empty()) wildcard.push_back(&e);
    else exact.push_back(&e);
  }
  std::vector<const PtxIsaEntry*> out;
  out.reserve(exact.size() + wildcard.size());
  out.insert(out.end(), exact.begin(), exact.end());
  out.insert(out.end(), wildcard.begin(), wildcard.end());
  return out;
}

std::optional<Diagnostic> PtxIsaMapper::map_kernel(const std::vector<TokenizedPtxInst>& insts,
                                                  const PtxIsaRegistry& isa,
                                                  std::vector<InstRecord>& out) const {
  out.clear();
  out.reserve(insts.size());
  for (std::size_t i = 0; i < insts.size(); i++) {
    InstRecord inst;
    if (auto d = map_one(insts[i], isa, inst)) {
      auto dd = *d;
      dd.inst_index = static_cast<std::int64_t>(i);
      return dd;
    }
    out.push_back(std::move(inst));
  }
  return std::nullopt;
}

std::optional<Diagnostic> PtxIsaMapper::map_one(const TokenizedPtxInst& inst,
                                               const PtxIsaRegistry& isa,
                                               InstRecord& out) const {
  out = InstRecord{};
  out.dbg = inst.dbg;
  out.pred = inst.pred;
  out.mods = inst.mods;

  const auto cands = isa.candidates(inst.ptx_opcode, inst.mods.type_mod, inst.operand_tokens.size());
  if (cands.empty()) {
    return Diagnostic{ "instruction", "DESC_NOT_FOUND", "no PTX ISA map entry for " + inst.ptx_opcode, inst.dbg, std::nullopt, std::nullopt };
  }

  struct Match final {
    const PtxIsaEntry* entry = nullptr;
    std::vector<Operand> operands;
  };

  std::vector<Match> matches;
  std::optional<Diagnostic> last_parse_diag;

  for (const auto* e : cands) {
    Match m;
    m.entry = e;
    bool ok = true;
    std::optional<Diagnostic> parse_diag;
    for (std::size_t i = 0; i < e->operand_kinds.size(); i++) {
      auto op = parse_operand_by_kind(e->operand_kinds[i], inst.operand_tokens[i], inst.mods.type_mod, parse_diag, inst.dbg);
      if (!op) {
        ok = false;
        break;
      }
      m.operands.push_back(*op);
    }
    if (ok) {
      matches.push_back(std::move(m));
    } else if (parse_diag) {
      last_parse_diag = parse_diag;
    }
  }

  if (matches.empty()) {
    if (last_parse_diag) return *last_parse_diag;
    return Diagnostic{ "instruction", "OPERAND_FORM_MISMATCH", "operands do not match any PTX ISA form for " + inst.ptx_opcode, inst.dbg, std::nullopt, std::nullopt };
  }

  if (matches.size() > 1) {
    std::string msg = "ambiguous PTX ISA mapping for " + inst.ptx_opcode;
    if (!inst.mods.type_mod.empty()) msg += "." + inst.mods.type_mod;
    msg += ": ";
    for (std::size_t i = 0; i < matches.size(); i++) {
      if (i) msg += "; ";
      msg += fmt_sig(*matches[i].entry);
    }
    return Diagnostic{ "instruction", "DESC_AMBIGUOUS", msg, inst.dbg, std::nullopt, std::nullopt };
  }

  out.opcode = matches[0].entry->ir_op;
  out.operands = std::move(matches[0].operands);
  return std::nullopt;
}

} // namespace gpusim
