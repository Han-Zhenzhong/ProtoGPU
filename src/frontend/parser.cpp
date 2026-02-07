#include "gpusim/frontend.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace gpusim {

namespace {

std::string trim(std::string s) {
  auto not_ws = [](unsigned char ch) { return !std::isspace(ch); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_ws));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_ws).base(), s.end());
  return s;
}

bool starts_with(const std::string& s, const std::string& p) {
  return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

std::vector<std::string> split_ws(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  for (char ch : s) {
    if (std::isspace(static_cast<unsigned char>(ch))) {
      if (!cur.empty()) {
        out.push_back(cur);
        cur.clear();
      }
      continue;
    }
    cur.push_back(ch);
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::string cur;
  for (char ch : s) {
    if (ch == sep) {
      out.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(ch);
    }
  }
  out.push_back(cur);
  return out;
}

std::optional<PredGuard> parse_predicate(std::string& line);

Operand parse_operand(std::string tok) {
  tok = trim(tok);
  if (!tok.empty() && tok.back() == ';') tok.pop_back();
  if (!tok.empty() && tok.back() == ',') tok.pop_back();
  tok = trim(tok);

  Operand o;
  if (tok.empty()) return o;

  // Bracketed address/symbol form: [%rd0], [%rd0+4], [param_name]
  if (tok.size() >= 2 && tok.front() == '[' && tok.back() == ']') {
    auto inner = trim(tok.substr(1, tok.size() - 2));
    if (!inner.empty() && inner.front() == '%') {
      // base reg with optional +imm
      std::string base = inner;
      std::int64_t off = 0;
      auto plus = inner.find('+');
      if (plus != std::string::npos) {
        base = trim(inner.substr(0, plus));
        off = std::stoll(trim(inner.substr(plus + 1)));
      }
      Operand base_op = parse_operand(base);
      if (base_op.kind == OperandKind::Reg) {
        o.kind = OperandKind::Addr;
        o.type = ValueType::U64;
        o.reg_id = base_op.reg_id;
        o.imm_i64 = off;
        return o;
      }
    }

    o.kind = OperandKind::Symbol;
    o.symbol = inner;
    return o;
  }

  if (tok[0] == '%') {
    if (tok.size() >= 2 && tok[1] == 'p') {
      o.kind = OperandKind::Pred;
      if (tok.size() > 2) o.pred_id = std::stoll(tok.substr(2));
      return o;
    }
    // Must check %rd<N> before %r<N> (otherwise "%rd0" matches the %r case and stoll("d0") throws).
    if (tok.size() >= 3 && tok[1] == 'r' && tok[2] == 'd') {
      auto num = tok.substr(3);
      if (!num.empty() && std::all_of(num.begin(), num.end(), [](unsigned char ch) { return std::isdigit(ch); })) {
        o.kind = OperandKind::Reg;
        o.type = ValueType::U64;
        o.reg_id = std::stoll(num);
        return o;
      }
    }
    if (tok.size() >= 2 && tok[1] == 'r') {
      auto num = tok.substr(2);
      if (!num.empty() && std::all_of(num.begin(), num.end(), [](unsigned char ch) { return std::isdigit(ch); })) {
        o.kind = OperandKind::Reg;
        o.type = ValueType::U32;
        o.reg_id = std::stoll(num);
        return o;
      }
    }
  }
  if (tok[0] == '-' || std::isdigit(static_cast<unsigned char>(tok[0]))) {
    o.kind = OperandKind::Imm;
    o.imm_i64 = std::stoll(tok);
    return o;
  }
  o.kind = OperandKind::Symbol;
  o.symbol = tok;
  return o;
}

TokenizedPtxInst tokenize_inst_line(const std::string& raw, const std::string& file_name, std::int64_t line_no) {
  TokenizedPtxInst inst;
  inst.dbg = SourceLocation{ file_name, line_no, 1 };

  std::string line = raw;
  if (auto pos = line.find("//"); pos != std::string::npos) {
    line = line.substr(0, pos);
  }
  line = trim(line);
  if (line.empty()) return inst;

  inst.pred = parse_predicate(line);

  auto ws = split_ws(line);
  if (ws.empty()) return inst;
  std::string opmods = ws[0];
  while (!opmods.empty() && (opmods.back() == ';' || opmods.back() == ',')) opmods.pop_back();
  opmods = trim(opmods);
  if (opmods.empty()) return inst;
  auto parts = split(opmods, '.');
  inst.ptx_opcode = parts[0];
  for (std::size_t k = 1; k < parts.size(); k++) {
    const auto& m = parts[k];
    if (m == "u32" || m == "s32" || m == "u64" || m == "s64" || m == "f32") {
      inst.mods.type_mod = m;
    } else if (m == "global" || m == "shared" || m == "local" || m == "const" || m == "param") {
      inst.mods.space = m;
    } else {
      inst.mods.flags.push_back(m);
    }
  }

  std::string rest;
  if (ws.size() >= 2) {
    auto pos0 = line.find(ws[0]);
    rest = trim(line.substr(pos0 + ws[0].size()));
  }
  if (!rest.empty()) {
    std::string cur;
    for (char ch : rest) {
      if (ch == ',' || ch == ';') {
        auto t = trim(cur);
        if (!t.empty()) inst.operand_tokens.push_back(t);
        cur.clear();
      } else {
        cur.push_back(ch);
      }
    }
    auto t = trim(cur);
    if (!t.empty()) inst.operand_tokens.push_back(t);
  }

  return inst;
}

static std::optional<ParamDesc> parse_param_decl(const std::string& raw) {
  // Example lines (may have trailing , or ):
  //   .param .u64 out_ptr,
  //   .param .u32 n
  auto t = raw;
  if (auto pos = t.find("//"); pos != std::string::npos) t = t.substr(0, pos);
  t = trim(t);
  if (t.find(".param") == std::string::npos) return std::nullopt;
  auto ws = split_ws(t);
  if (ws.size() < 3) return std::nullopt;
  if (ws[0] != ".param") return std::nullopt;

  auto type_tok = ws[1];
  auto name_tok = ws[2];
  while (!name_tok.empty() && (name_tok.back() == ',' || name_tok.back() == ')' || name_tok.back() == ';')) name_tok.pop_back();
  name_tok = trim(name_tok);
  if (name_tok.empty()) return std::nullopt;

  ParamDesc p;
  p.name = name_tok;
  if (type_tok == ".u32") {
    p.type = ParamType::U32;
    p.size = 4;
    p.align = 4;
  } else if (type_tok == ".u64") {
    p.type = ParamType::U64;
    p.size = 8;
    p.align = 8;
  } else {
    return std::nullopt;
  }
  return p;
}

static std::uint32_t align_up_u32(std::uint32_t v, std::uint32_t a) {
  if (a == 0) return v;
  auto r = v % a;
  return r == 0 ? v : (v + (a - r));
}

std::optional<PredGuard> parse_predicate(std::string& line) {
  line = trim(line);
  if (line.empty()) return std::nullopt;
  if (line[0] != '@') return std::nullopt;
  bool neg = false;
  std::size_t i = 1;
  if (i < line.size() && line[i] == '!') {
    neg = true;
    i++;
  }
  if (i >= line.size() || line[i] != '%') return std::nullopt;
  if (i + 2 >= line.size() || line[i + 1] != 'p') return std::nullopt;
  i += 2;
  std::size_t j = i;
  while (j < line.size() && std::isdigit(static_cast<unsigned char>(line[j]))) j++;
  if (j == i) return std::nullopt;
  std::int64_t pid = std::stoll(line.substr(i, j - i));
  line = trim(line.substr(j));
  return PredGuard{ pid, neg };
}

InstRecord parse_inst_line(const std::string& raw, std::int64_t line_no) {
  InstRecord inst;
  inst.dbg = SourceLocation{ "<ptx>", line_no, 1 };

  std::string line = raw;
  if (auto pos = line.find("//"); pos != std::string::npos) {
    line = line.substr(0, pos);
  }
  line = trim(line);
  if (line.empty()) return inst;

  inst.pred = parse_predicate(line);

  auto ws = split_ws(line);
  if (ws.empty()) return inst;
  std::string opmods = ws[0];
  while (!opmods.empty() && (opmods.back() == ';' || opmods.back() == ',')) opmods.pop_back();
  opmods = trim(opmods);
  if (opmods.empty()) return inst;
  auto parts = split(opmods, '.');
  inst.opcode = parts[0];
  for (std::size_t k = 1; k < parts.size(); k++) {
    const auto& m = parts[k];
    if (m == "u32" || m == "s32" || m == "u64" || m == "s64" || m == "f32") {
      inst.mods.type_mod = m;
    } else if (m == "global" || m == "shared" || m == "local" || m == "const" || m == "param") {
      inst.mods.space = m;
    } else {
      inst.mods.flags.push_back(m);
    }
  }

  std::string rest;
  if (ws.size() >= 2) {
    auto pos0 = line.find(ws[0]);
    rest = trim(line.substr(pos0 + ws[0].size()));
  }
  if (!rest.empty()) {
    std::string cur;
    for (char ch : rest) {
      if (ch == ',' || ch == ';') {
        if (!trim(cur).empty()) inst.operands.push_back(parse_operand(cur));
        cur.clear();
      } else {
        cur.push_back(ch);
      }
    }
    if (!trim(cur).empty()) inst.operands.push_back(parse_operand(cur));
  }

  for (auto& o : inst.operands) {
    if (inst.mods.type_mod == "u64" || inst.mods.type_mod == "s64") {
      if (o.kind == OperandKind::Reg && o.type == ValueType::U32) o.type = ValueType::U64;
    }
  }

  return inst;
}

} // namespace

ModuleImage Parser::parse_ptx_text(const std::string& ptx_text) {
  ModuleImage m;

  std::istringstream in(ptx_text);
  std::string line;
  bool in_entry = false;
  bool in_body = false;
  KernelImage cur;
  std::int64_t line_no = 0;

  while (std::getline(in, line)) {
    line_no++;
    std::string t = trim(line);
    if (t.empty()) continue;
    if (starts_with(t, ".visible") && t.find(".entry") != std::string::npos) {
      in_entry = true;
      in_body = false;
      cur = KernelImage{};
      auto pos = t.find(".entry");
      auto after = trim(t.substr(pos + 6));
      auto name_end = after.find('(');
      cur.name = trim(after.substr(0, name_end));
      continue;
    }
    if (in_entry && t == "}") {
      in_entry = false;
      in_body = false;
      m.kernels.push_back(std::move(cur));
      continue;
    }
    if (!in_entry) continue;

    // Parse kernel signature lines until the body begins.
    if (!in_body) {
      if (auto p = parse_param_decl(t)) {
        // Natural alignment layout.
        std::uint32_t end = 0;
        for (const auto& prev : cur.params) {
          end = std::max(end, prev.offset + prev.size);
        }
        p->offset = align_up_u32(end, p->align);
        cur.params.push_back(*p);
      }
      if (!t.empty() && t[0] == '{') {
        in_body = true;
      }
      continue;
    }

    if (starts_with(t, ".reg")) {
      // Example: .reg .u32 %r<3>;
      auto a = split_ws(t);
      if (a.size() >= 3) {
        auto type = a[1];
        auto decl = a[2];
        auto lt = decl.find('<');
        auto gt = decl.find('>');
        if (lt != std::string::npos && gt != std::string::npos && gt > lt + 1) {
          auto count = static_cast<std::uint32_t>(std::stoul(decl.substr(lt + 1, gt - lt - 1)));
          if (type == ".u32") cur.reg_u32_count = std::max(cur.reg_u32_count, count);
          if (type == ".f32") cur.reg_u32_count = std::max(cur.reg_u32_count, count);
          if (type == ".u64") cur.reg_u64_count = std::max(cur.reg_u64_count, count);
        }
      }
      continue;
    }

    if (t[0] == '.' || t[0] == '{') continue;
    auto inst = parse_inst_line(t, line_no);
    if (!inst.opcode.empty()) cur.insts.push_back(std::move(inst));
  }

  return m;
}

ModuleTokens Parser::parse_ptx_text_tokens(const std::string& ptx_text, const std::string& file_name) {
  ModuleTokens m;

  std::istringstream in(ptx_text);
  std::string line;
  bool in_entry = false;
  bool in_body = false;
  KernelTokens cur;
  std::vector<std::pair<std::int64_t, std::string>> body_lines;
  std::int64_t line_no = 0;

  while (std::getline(in, line)) {
    line_no++;
    std::string t = trim(line);
    if (t.empty()) continue;
    if (starts_with(t, ".visible") && t.find(".entry") != std::string::npos) {
      in_entry = true;
      in_body = false;
      cur = KernelTokens{};
      body_lines.clear();
      auto pos = t.find(".entry");
      auto after = trim(t.substr(pos + 6));
      auto name_end = after.find('(');
      cur.name = trim(after.substr(0, name_end));
      continue;
    }
    if (in_entry && t == "}") {
      // Two-pass kernel body processing: bind labels to PC, then tokenize and rewrite bra label -> imm(pc).
      std::unordered_map<std::string, PC> label_to_pc;
      PC pc = 0;

      for (const auto& it : body_lines) {
        std::string s = it.second;
        if (auto pos = s.find("//"); pos != std::string::npos) s = s.substr(0, pos);
        s = trim(s);
        if (s.empty()) continue;

        if (starts_with(s, ".reg")) {
          auto a = split_ws(s);
          if (a.size() >= 3) {
            auto type = a[1];
            auto decl = a[2];
            auto lt = decl.find('<');
            auto gt = decl.find('>');
            if (lt != std::string::npos && gt != std::string::npos && gt > lt + 1) {
              auto count = static_cast<std::uint32_t>(std::stoul(decl.substr(lt + 1, gt - lt - 1)));
              if (type == ".u32") cur.reg_u32_count = std::max(cur.reg_u32_count, count);
              if (type == ".f32") cur.reg_u32_count = std::max(cur.reg_u32_count, count);
              if (type == ".u64") cur.reg_u64_count = std::max(cur.reg_u64_count, count);
            }
          }
          continue;
        }

        if (s[0] == '.' || s[0] == '{') continue;
        if (!s.empty() && s.back() == ':') {
          auto name = trim(s.substr(0, s.size() - 1));
          if (!name.empty()) label_to_pc[name] = pc;
          continue;
        }
        pc += 1;
      }

      for (const auto& it : body_lines) {
        const auto body_line_no = it.first;
        std::string s = it.second;
        if (auto pos = s.find("//"); pos != std::string::npos) s = s.substr(0, pos);
        s = trim(s);
        if (s.empty()) continue;
        if (starts_with(s, ".reg")) continue;
        if (s[0] == '.' || s[0] == '{') continue;
        if (!s.empty() && s.back() == ':') continue;

        auto inst = tokenize_inst_line(s, file_name, body_line_no);
        if (!inst.ptx_opcode.empty() && inst.ptx_opcode == "bra" && inst.operand_tokens.size() == 1) {
          const auto label = trim(inst.operand_tokens[0]);
          auto itpc = label_to_pc.find(label);
          if (itpc != label_to_pc.end()) {
            inst.operand_tokens[0] = std::to_string(itpc->second);
          }
        }
        if (!inst.ptx_opcode.empty()) cur.insts.push_back(std::move(inst));
      }

      in_entry = false;
      in_body = false;
      m.kernels.push_back(std::move(cur));
      continue;
    }
    if (!in_entry) continue;

    if (!in_body) {
      if (auto p = parse_param_decl(t)) {
        std::uint32_t end = 0;
        for (const auto& prev : cur.params) {
          end = std::max(end, prev.offset + prev.size);
        }
        p->offset = align_up_u32(end, p->align);
        cur.params.push_back(*p);
      }
      if (!t.empty() && t[0] == '{') {
        in_body = true;
      }
      continue;
    }

    // Defer all body processing to kernel-end two-pass (needed for forward label references).
    body_lines.emplace_back(line_no, t);
  }

  return m;
}

ModuleImage Parser::parse_ptx_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("Parser: cannot open " + path);
  std::ostringstream ss;
  ss << f.rdbuf();
  return parse_ptx_text(ss.str());
}

ModuleTokens Parser::parse_ptx_file_tokens(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("Parser: cannot open " + path);
  std::ostringstream ss;
  ss << f.rdbuf();
  return parse_ptx_text_tokens(ss.str(), path);
}

} // namespace gpusim
