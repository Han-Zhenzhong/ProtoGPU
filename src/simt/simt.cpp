#include "gpusim/simt.h"

#include <sstream>

namespace gpusim {

static std::string format_inst_sig(const InstRecord& inst) {
  std::ostringstream oss;
  oss << inst.opcode;
  if (!inst.mods.type_mod.empty()) oss << "." << inst.mods.type_mod;
  oss << " (";
  for (std::size_t i = 0; i < inst.operands.size(); i++) {
    if (i) oss << ",";
    oss << operand_kind_to_string(inst.operands[i].kind);
  }
  oss << ")";
  return oss.str();
}

SimtExecutor::SimtExecutor(SimConfig cfg, DescriptorRegistry registry, ObsControl& obs, AddrSpaceManager& mem)
    : cfg_(cfg), registry_(std::move(registry)), obs_(obs), mem_(mem), mem_unit_(mem_) {}

SimResult SimtExecutor::run(const KernelImage& kernel) {
  SimResult res;
  WarpState warp;
  warp.pc = 0;
  warp.done = false;
  warp.active = lane_mask_all(cfg_.warp_size);
  warp.r_u32.assign(kernel.reg_u32_count, 0);
  warp.r_u64.assign(kernel.reg_u64_count, 0);
  warp.p.assign(8, true);

  std::uint64_t ts = 0;

  while (!warp.done && res.steps < cfg_.max_steps) {
    if (warp.pc < 0 || static_cast<std::size_t>(warp.pc) >= kernel.insts.size()) {
      res.diag = Diagnostic{ "simt", "E_PC_OOB", "PC out of range", std::nullopt, std::nullopt, warp.pc };
      break;
    }

    const auto& inst = kernel.insts[static_cast<std::size_t>(warp.pc)];
    obs_.emit(Event{ ts++, EventCategory::Fetch, "FETCH", std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, warp.pc, warp.active, inst.opcode, std::nullopt, std::nullopt, std::nullopt });
    obs_.counter("inst.fetch");

    auto desc = registry_.lookup(inst);
    if (!desc) {
      res.diag = Diagnostic{ "simt", "E_DESC_MISS", "no inst descriptor for " + format_inst_sig(inst), inst.dbg, std::nullopt, warp.pc };
      break;
    }

    auto uops = expander_.expand(inst, *desc, cfg_.warp_size);
    bool progressed_any = false;
    for (const auto& uop : uops) {
      auto uop_name = [&]() -> std::string {
        switch (uop.op) {
        case MicroOpOp::Mov: return "MOV";
        case MicroOpOp::Add: return "ADD";
        case MicroOpOp::Ld: return "LD";
        case MicroOpOp::St: return "ST";
        case MicroOpOp::Ret: return "RET";
        }
        return "UOP";
      }();
      obs_.emit(Event{ ts++, (uop.kind == MicroOpKind::Exec ? EventCategory::Exec : (uop.kind == MicroOpKind::Control ? EventCategory::Ctrl : EventCategory::Mem)),
                      "UOP", std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                      warp.pc, warp.active, uop_name, std::nullopt, std::nullopt,
                      std::nullopt });
      StepResult sr;
      if (uop.kind == MicroOpKind::Exec) sr = exec_.step(uop, warp, obs_);
      else if (uop.kind == MicroOpKind::Control) sr = ctrl_.step(uop, warp, obs_);
      else sr = mem_unit_.step(uop, warp, obs_);

      if (sr.diag) {
        res.diag = sr.diag;
        return res;
      }
      progressed_any = progressed_any || sr.progressed;
    }

    if (!warp.done) {
      warp.pc += 1;
      obs_.emit(Event{ ts++, EventCategory::Commit, "COMMIT", std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                      std::nullopt, std::nullopt, warp.pc - 1, warp.active, inst.opcode, std::nullopt, std::nullopt, std::nullopt });
      obs_.counter("inst.commit");
    }

    res.steps++;
    (void)progressed_any;
  }

  res.completed = warp.done && !res.diag.has_value();
  return res;
}

} // namespace gpusim
