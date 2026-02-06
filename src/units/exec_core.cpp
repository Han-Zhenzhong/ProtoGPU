#include "gpusim/units.h"

#include <cstdint>

namespace gpusim {

static std::uint64_t read_reg(const Operand& o, const WarpState& warp) {
  if (o.kind != OperandKind::Reg) return 0;
  if (o.type == ValueType::U64 || o.type == ValueType::S64) {
    auto idx = static_cast<std::size_t>(o.reg_id);
    return idx < warp.r_u64.size() ? warp.r_u64[idx] : 0;
  }
  auto idx = static_cast<std::size_t>(o.reg_id);
  return idx < warp.r_u32.size() ? warp.r_u32[idx] : 0;
}

static void write_reg(const Operand& o, WarpState& warp, std::uint64_t v) {
  if (o.kind != OperandKind::Reg) return;
  if (o.type == ValueType::U64 || o.type == ValueType::S64) {
    auto idx = static_cast<std::size_t>(o.reg_id);
    if (idx >= warp.r_u64.size()) warp.r_u64.resize(idx + 1);
    warp.r_u64[idx] = v;
    return;
  }
  auto idx = static_cast<std::size_t>(o.reg_id);
  if (idx >= warp.r_u32.size()) warp.r_u32.resize(idx + 1);
  warp.r_u32[idx] = static_cast<std::uint32_t>(v);
}

static std::uint64_t read_imm_or_reg(const Operand& o, const WarpState& warp) {
  if (o.kind == OperandKind::Imm) return static_cast<std::uint64_t>(o.imm_i64);
  if (o.kind == OperandKind::Reg) return read_reg(o, warp);
  return 0;
}

StepResult ExecCore::step(const MicroOp& uop, WarpState& warp, ObsControl& obs) {
  StepResult r;
  if (warp.done) {
    r.warp_done = true;
    return r;
  }

  switch (uop.op) {
  case MicroOpOp::Mov: {
    if (uop.outputs.size() != 1 || uop.inputs.size() != 1) {
      r.diag = Diagnostic{ "units.exec", "E_UOP_ARITY", "MOV expects 1 in, 1 out", std::nullopt, std::nullopt, std::nullopt };
      r.blocked_reason = BlockedReason::Error;
      return r;
    }
    auto v = read_imm_or_reg(uop.inputs[0], warp);
    write_reg(uop.outputs[0], warp, v);
    obs.counter("uop.exec.mov");
    r.progressed = true;
    return r;
  }
  case MicroOpOp::Add: {
    if (uop.outputs.size() != 1 || uop.inputs.size() != 2) {
      r.diag = Diagnostic{ "units.exec", "E_UOP_ARITY", "ADD expects 2 in, 1 out", std::nullopt, std::nullopt, std::nullopt };
      r.blocked_reason = BlockedReason::Error;
      return r;
    }
    auto a = read_imm_or_reg(uop.inputs[0], warp);
    auto b = read_imm_or_reg(uop.inputs[1], warp);
    write_reg(uop.outputs[0], warp, a + b);
    obs.counter("uop.exec.add");
    r.progressed = true;
    return r;
  }
  default:
    r.blocked_reason = BlockedReason::Error;
    r.diag = Diagnostic{ "units.exec", "E_UOP_UNSUPPORTED", "unsupported exec uop", std::nullopt, std::nullopt, std::nullopt };
    return r;
  }
}

} // namespace gpusim
