#include "gpusim/units.h"

#include <cstdint>
#include <optional>

namespace gpusim {

namespace {

static std::size_t reg_lane_index(std::size_t reg, std::uint32_t lane, std::uint32_t warp_size) {
  return reg * static_cast<std::size_t>(warp_size) + static_cast<std::size_t>(lane);
}

static std::uint64_t read_reg_lane(const Operand& o, const WarpState& warp, std::uint32_t lane) {
  if (o.kind != OperandKind::Reg) return 0;
  const auto warp_size = warp.active.width;
  if (warp_size == 0) return 0;
  if (lane >= warp_size) return 0;

  const auto reg = (o.reg_id < 0) ? 0u : static_cast<std::size_t>(o.reg_id);
  if (o.type == ValueType::U64 || o.type == ValueType::S64) {
    const auto idx = reg_lane_index(reg, lane, warp_size);
    return idx < warp.r_u64.size() ? warp.r_u64[idx] : 0;
  }
  const auto idx = reg_lane_index(reg, lane, warp_size);
  return idx < warp.r_u32.size() ? static_cast<std::uint64_t>(warp.r_u32[idx]) : 0;
}

static void write_reg_lane(const Operand& o, WarpState& warp, std::uint32_t lane, std::uint64_t v) {
  if (o.kind != OperandKind::Reg) return;
  const auto warp_size = warp.active.width;
  if (warp_size == 0) return;
  if (lane >= warp_size) return;

  const auto reg = (o.reg_id < 0) ? 0u : static_cast<std::size_t>(o.reg_id);
  if (o.type == ValueType::U64 || o.type == ValueType::S64) {
    const auto idx = reg_lane_index(reg, lane, warp_size);
    if (idx >= warp.r_u64.size()) warp.r_u64.resize(idx + 1);
    warp.r_u64[idx] = v;
    return;
  }
  const auto idx = reg_lane_index(reg, lane, warp_size);
  if (idx >= warp.r_u32.size()) warp.r_u32.resize(idx + 1);
  warp.r_u32[idx] = static_cast<std::uint32_t>(v);
}

static std::optional<std::uint64_t> read_special_u32(const std::string& name,
                                                     const WarpState& warp,
                                                     std::uint32_t lane) {
  const auto bx = warp.launch.block_dim.x;
  const auto by = warp.launch.block_dim.y;
  const auto bz = warp.launch.block_dim.z;
  if (bx == 0 || by == 0 || bz == 0) return 0u;

  const std::uint64_t thread_linear = static_cast<std::uint64_t>(warp.lane_base_thread_linear) + lane;
  const std::uint64_t bx64 = bx;
  const std::uint64_t by64 = by;
  const std::uint64_t bxy64 = bx64 * by64;

  const std::uint32_t tid_x = static_cast<std::uint32_t>(thread_linear % bx64);
  const std::uint32_t tid_y = static_cast<std::uint32_t>((thread_linear / bx64) % by64);
  const std::uint32_t tid_z = static_cast<std::uint32_t>(thread_linear / (bxy64 == 0 ? 1 : bxy64));

  if (name == "tid.x") return tid_x;
  if (name == "tid.y") return tid_y;
  if (name == "tid.z") return tid_z;

  if (name == "ntid.x") return warp.launch.block_dim.x;
  if (name == "ntid.y") return warp.launch.block_dim.y;
  if (name == "ntid.z") return warp.launch.block_dim.z;

  if (name == "ctaid.x") return warp.ctaid.x;
  if (name == "ctaid.y") return warp.ctaid.y;
  if (name == "ctaid.z") return warp.ctaid.z;

  if (name == "nctaid.x") return warp.launch.grid_dim.x;
  if (name == "nctaid.y") return warp.launch.grid_dim.y;
  if (name == "nctaid.z") return warp.launch.grid_dim.z;

  if (name == "laneid") return lane;
  if (name == "warpid") return warp.warpid;

  return std::nullopt;
}

static std::optional<std::uint64_t> read_operand_lane(const Operand& o,
                                                      const WarpState& warp,
                                                      std::uint32_t lane) {
  if (o.kind == OperandKind::Imm) return static_cast<std::uint64_t>(o.imm_i64);
  if (o.kind == OperandKind::Reg) return read_reg_lane(o, warp, lane);
  if (o.kind == OperandKind::Special) return read_special_u32(o.special, warp, lane);
  return 0u;
}

} // namespace

StepResult ExecCore::step(const MicroOp& uop, WarpState& warp, ObsControl& obs) {
  StepResult r;
  if (warp.done) {
    r.warp_done = true;
    return r;
  }

  const auto exec_mask = lane_mask_and(warp.active, uop.guard);

  switch (uop.op) {
  case MicroOpOp::Mov: {
    if (uop.outputs.size() != 1 || uop.inputs.size() != 1) {
      r.diag = Diagnostic{ "units.exec", "E_UOP_ARITY", "MOV expects 1 in, 1 out", std::nullopt, std::nullopt, std::nullopt };
      r.blocked_reason = BlockedReason::Error;
      return r;
    }
    for (std::uint32_t lane = 0; lane < exec_mask.width && lane < 32; lane++) {
      if (!lane_mask_test(exec_mask, lane)) continue;
      auto v = read_operand_lane(uop.inputs[0], warp, lane);
      if (!v.has_value()) {
        r.diag = Diagnostic{ "units.exec", "E_SPECIAL_UNKNOWN", "unknown special operand: " + uop.inputs[0].special, std::nullopt, std::nullopt, std::nullopt };
        r.blocked_reason = BlockedReason::Error;
        return r;
      }
      write_reg_lane(uop.outputs[0], warp, lane, *v);
    }
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
    for (std::uint32_t lane = 0; lane < exec_mask.width && lane < 32; lane++) {
      if (!lane_mask_test(exec_mask, lane)) continue;
      auto a = read_operand_lane(uop.inputs[0], warp, lane);
      auto b = read_operand_lane(uop.inputs[1], warp, lane);
      if (!a.has_value() || !b.has_value()) {
        r.diag = Diagnostic{ "units.exec", "E_SPECIAL_UNKNOWN", "unknown special operand", std::nullopt, std::nullopt, std::nullopt };
        r.blocked_reason = BlockedReason::Error;
        return r;
      }
      write_reg_lane(uop.outputs[0], warp, lane, (*a) + (*b));
    }
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
