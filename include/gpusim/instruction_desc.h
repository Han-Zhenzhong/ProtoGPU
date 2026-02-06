#pragma once

#include "gpusim/contracts.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace gpusim {

struct UopTemplate final {
  MicroOpKind kind = MicroOpKind::Exec;
  MicroOpOp op = MicroOpOp::Mov;
  std::vector<int> in_operand_idx;
  std::vector<int> out_operand_idx;
};

struct InstDesc final {
  std::string opcode;
  std::string type_mod;
  std::vector<std::string> operand_kinds;
  std::vector<UopTemplate> uops;
};

class DescriptorRegistry final {
public:
  void load_json_file(const std::string& path);
  void load_json_text(const std::string& text);

  std::optional<InstDesc> lookup(const InstRecord& inst) const;

private:
  std::vector<InstDesc> descs_;
};

class Expander final {
public:
  std::vector<MicroOp> expand(const InstRecord& inst, const InstDesc& desc, std::uint32_t warp_size) const;
};

std::string operand_kind_to_string(OperandKind k);

} // namespace gpusim
