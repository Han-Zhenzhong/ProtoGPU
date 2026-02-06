#pragma once

#include "gpusim/contracts.h"
#include "gpusim/ptx_tokens.h"

#include <optional>
#include <string>
#include <vector>

namespace gpusim {

struct PtxIsaEntry final {
  std::string ptx_opcode;
  std::string type_mod; // exact match, or "" wildcard
  std::vector<std::string> operand_kinds;
  std::string ir_op; // inst desc opcode key
};

class PtxIsaRegistry final {
public:
  void load_json_file(const std::string& path);
  void load_json_text(const std::string& text);

  // Deterministic candidate enumeration for mapping/parse.
  // Ordering: exact type_mod matches first, then wildcard (entry.type_mod=="").
  std::vector<const PtxIsaEntry*> candidates(const std::string& ptx_opcode, const std::string& type_mod, std::size_t operand_count) const;

private:
  std::vector<PtxIsaEntry> entries_;
};

class PtxIsaMapper final {
public:
  // Maps + parses tokenized PTX into IR InstRecord (InstRecord.opcode == ir_op).
  // On failure returns Diagnostic.
  std::optional<Diagnostic> map_kernel(const std::vector<TokenizedPtxInst>& insts,
                                      const PtxIsaRegistry& isa,
                                      std::vector<InstRecord>& out) const;

private:
  std::optional<Diagnostic> map_one(const TokenizedPtxInst& inst,
                                   const PtxIsaRegistry& isa,
                                   InstRecord& out) const;
};

} // namespace gpusim
