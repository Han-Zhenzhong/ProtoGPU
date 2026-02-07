#include "gpusim/units.h"

namespace gpusim {

StepResult ControlUnit::step(const MicroOp& uop, WarpState& warp, ObsControl& obs) {
  StepResult r;
  if (warp.done) {
    r.warp_done = true;
    return r;
  }

  const auto exec_mask = lane_mask_and(warp.active, uop.guard);
  if (exec_mask.bits_lo == 0u) {
    // Predicated-off control op: treat as a no-op.
    r.progressed = true;
    return r;
  }

  switch (uop.op) {
  case MicroOpOp::Bra:
    if (uop.inputs.size() != 1) {
      r.diag = Diagnostic{ "units.ctrl", "E_UOP_ARITY", "BRA expects 1 input (pc)", std::nullopt, std::nullopt, std::nullopt };
      r.blocked_reason = BlockedReason::Error;
      return r;
    }
    if (uop.inputs[0].kind != OperandKind::Imm) {
      r.diag = Diagnostic{ "units.ctrl", "E_OPERAND_KIND", "BRA expects imm(pc) operand", std::nullopt, std::nullopt, std::nullopt };
      r.blocked_reason = BlockedReason::Error;
      return r;
    }
    obs.counter("uop.ctrl.bra");
    r.progressed = true;
    r.next_pc = static_cast<PC>(uop.inputs[0].imm_i64);
    return r;
  case MicroOpOp::Ret:
    warp.done = true;
    obs.counter("uop.ctrl.ret");
    r.progressed = true;
    r.warp_done = true;
    return r;
  default:
    r.blocked_reason = BlockedReason::Error;
    r.diag = Diagnostic{ "units.ctrl", "E_UOP_UNSUPPORTED", "unsupported ctrl uop", std::nullopt, std::nullopt, std::nullopt };
    return r;
  }
}

} // namespace gpusim
