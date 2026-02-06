#include "gpusim/units.h"

namespace gpusim {

StepResult ControlUnit::step(const MicroOp& uop, WarpState& warp, ObsControl& obs) {
  StepResult r;
  if (warp.done) {
    r.warp_done = true;
    return r;
  }

  switch (uop.op) {
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
