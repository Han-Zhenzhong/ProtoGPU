#include "gpusim/instruction_desc.h"

namespace gpusim {

static AddrSpace parse_space(const std::string& s) {
  if (s == "global") return AddrSpace::Global;
  if (s == "shared") return AddrSpace::Shared;
  if (s == "local") return AddrSpace::Local;
  if (s == "const") return AddrSpace::Const;
  if (s == "param") return AddrSpace::Param;
  return AddrSpace::Global;
}

std::vector<MicroOp> Expander::expand(const InstRecord& inst, const InstDesc& desc, std::uint32_t warp_size) const {
  std::vector<MicroOp> out;
  out.reserve(desc.uops.size());
  const auto ty = parse_value_type(inst.mods.type_mod);
  const auto sp = parse_space(inst.mods.space);

  for (const auto& t : desc.uops) {
    MicroOp u;
    u.kind = t.kind;
    u.op = t.op;
    u.attrs.type = ty;
    u.attrs.space = sp;
    u.attrs.flags = inst.mods.flags;
    u.guard = lane_mask_all(warp_size);
    for (int idx : t.in_operand_idx) {
      if (idx < 0 || static_cast<std::size_t>(idx) >= inst.operands.size()) continue;
      u.inputs.push_back(inst.operands[static_cast<std::size_t>(idx)]);
    }
    for (int idx : t.out_operand_idx) {
      if (idx < 0 || static_cast<std::size_t>(idx) >= inst.operands.size()) continue;
      u.outputs.push_back(inst.operands[static_cast<std::size_t>(idx)]);
    }
    out.push_back(std::move(u));
  }

  return out;
}

} // namespace gpusim
