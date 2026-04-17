#include "gpusim/units.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
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
  const auto log_builtins = []() -> bool {
    if (const char* v = std::getenv("GPUSIM_LOG_BUILTINS")) {
      return v && *v != '\0';
    }
    return false;
  }();

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

  if (log_builtins && lane == 0 && (warp.warpid == 0 || warp.warpid == 1)) {
    static std::uint32_t logged_mask = 0;
    const std::uint32_t bit = (warp.warpid == 0) ? 1u : 2u;
    // Log once per warp for the first few builtins we care about.
    if ((logged_mask & bit) == 0) {
      if (name == "ctaid.x" || name == "ntid.x" || name == "tid.x") {
        std::fprintf(stderr,
                     "[gpu-sim] builtins(warp=%u lane0 base=%u) block=(%u,%u,%u) grid=(%u,%u,%u) ctaid=(%u,%u,%u) tid=(%u,%u,%u) req=%s\n",
                     static_cast<unsigned>(warp.warpid),
                     static_cast<unsigned>(warp.lane_base_thread_linear),
                     static_cast<unsigned>(warp.launch.block_dim.x),
                     static_cast<unsigned>(warp.launch.block_dim.y),
                     static_cast<unsigned>(warp.launch.block_dim.z),
                     static_cast<unsigned>(warp.launch.grid_dim.x),
                     static_cast<unsigned>(warp.launch.grid_dim.y),
                     static_cast<unsigned>(warp.launch.grid_dim.z),
                     static_cast<unsigned>(warp.ctaid.x),
                     static_cast<unsigned>(warp.ctaid.y),
                     static_cast<unsigned>(warp.ctaid.z),
                     static_cast<unsigned>(tid_x),
                     static_cast<unsigned>(tid_y),
                     static_cast<unsigned>(tid_z),
                     name.c_str());
        logged_mask |= bit;
      }
    }
  }

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

static float f32_from_bits(std::uint32_t bits) {
  float f = 0.0f;
  std::memcpy(&f, &bits, sizeof(float));
  return f;
}

static std::uint32_t f32_to_bits(float f) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &f, sizeof(float));
  return bits;
}

static bool has_flag(const std::vector<std::string>& flags, const char* name) {
  for (const auto& f : flags) {
    if (f == name) return true;
  }
  return false;
}

static std::uint32_t shfl_width_from_clamp(std::uint32_t clamp) {
  // CUDA PTX shfl wrappers encode width as: c = ((32 - width) << 8) | 31.
  const std::uint32_t width = 32u - ((clamp >> 8) & 0x1Fu);
  if (width == 0u || width > 32u) return 32u;
  return width;
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
  case MicroOpOp::Cvt: {
    if (uop.outputs.size() != 1 || uop.inputs.size() != 1) {
      r.diag = Diagnostic{ "units.exec", "E_UOP_ARITY", "CVT expects 1 in, 1 out", std::nullopt, std::nullopt, std::nullopt };
      r.blocked_reason = BlockedReason::Error;
      return r;
    }

    if (uop.outputs[0].type != ValueType::F32 || uop.inputs[0].type != ValueType::U32) {
      r.diag = Diagnostic{ "units.exec", "E_UOP_TYPE", "CVT only supports u32->f32 in this milestone", std::nullopt, std::nullopt, std::nullopt };
      r.blocked_reason = BlockedReason::Error;
      return r;
    }

    for (std::uint32_t lane = 0; lane < exec_mask.width && lane < 32; lane++) {
      if (!lane_mask_test(exec_mask, lane)) continue;
      auto src_bits = read_operand_lane(uop.inputs[0], warp, lane);
      if (!src_bits.has_value()) {
        r.diag = Diagnostic{ "units.exec", "E_SPECIAL_UNKNOWN", "unknown special operand", std::nullopt, std::nullopt, std::nullopt };
        r.blocked_reason = BlockedReason::Error;
        return r;
      }

      const auto src_u32 = static_cast<std::uint32_t>(*src_bits);
      const float out_f32 = static_cast<float>(src_u32);
      write_reg_lane(uop.outputs[0], warp, lane, static_cast<std::uint64_t>(f32_to_bits(out_f32)));
    }

    obs.counter("uop.exec.cvt.f32.u32");
    r.progressed = true;
    return r;
  }
  case MicroOpOp::Add: {
    if (uop.outputs.size() != 1 || uop.inputs.size() != 2) {
      r.diag = Diagnostic{ "units.exec", "E_UOP_ARITY", "ADD expects 2 in, 1 out", std::nullopt, std::nullopt, std::nullopt };
      r.blocked_reason = BlockedReason::Error;
      return r;
    }

    if (uop.attrs.type == ValueType::F32) {
      for (std::uint32_t lane = 0; lane < exec_mask.width && lane < 32; lane++) {
        if (!lane_mask_test(exec_mask, lane)) continue;
        auto a_bits = read_operand_lane(uop.inputs[0], warp, lane);
        auto b_bits = read_operand_lane(uop.inputs[1], warp, lane);
        if (!a_bits.has_value() || !b_bits.has_value()) {
          r.diag = Diagnostic{ "units.exec", "E_SPECIAL_UNKNOWN", "unknown special operand", std::nullopt, std::nullopt, std::nullopt };
          r.blocked_reason = BlockedReason::Error;
          return r;
        }
        const float a = f32_from_bits(static_cast<std::uint32_t>(*a_bits));
        const float b = f32_from_bits(static_cast<std::uint32_t>(*b_bits));
        const float out = a + b;
        write_reg_lane(uop.outputs[0], warp, lane, static_cast<std::uint64_t>(f32_to_bits(out)));
      }
      obs.counter("uop.exec.add.f32");
      r.progressed = true;
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
  case MicroOpOp::Mul: {
    if (uop.outputs.size() != 1 || uop.inputs.size() != 2) {
      r.diag = Diagnostic{ "units.exec", "E_UOP_ARITY", "MUL expects 2 in, 1 out", std::nullopt, std::nullopt, std::nullopt };
      r.blocked_reason = BlockedReason::Error;
      return r;
    }

    if (uop.attrs.type == ValueType::F32) {
      for (std::uint32_t lane = 0; lane < exec_mask.width && lane < 32; lane++) {
        if (!lane_mask_test(exec_mask, lane)) continue;
        auto a_bits = read_operand_lane(uop.inputs[0], warp, lane);
        auto b_bits = read_operand_lane(uop.inputs[1], warp, lane);
        if (!a_bits.has_value() || !b_bits.has_value()) {
          r.diag = Diagnostic{ "units.exec", "E_SPECIAL_UNKNOWN", "unknown special operand", std::nullopt, std::nullopt, std::nullopt };
          r.blocked_reason = BlockedReason::Error;
          return r;
        }
        const float a = f32_from_bits(static_cast<std::uint32_t>(*a_bits));
        const float b = f32_from_bits(static_cast<std::uint32_t>(*b_bits));
        const float out = a * b;
        write_reg_lane(uop.outputs[0], warp, lane, static_cast<std::uint64_t>(f32_to_bits(out)));
      }
      obs.counter("uop.exec.mul.f32");
      r.progressed = true;
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

      const bool signed_mul = (uop.attrs.type == ValueType::S32 || uop.attrs.type == ValueType::S64);
      const bool wide_out = (uop.outputs[0].type == ValueType::U64 || uop.outputs[0].type == ValueType::S64);

      if (signed_mul) {
        const std::int64_t aa = static_cast<std::int32_t>(static_cast<std::uint32_t>(*a));
        const std::int64_t bb = static_cast<std::int32_t>(static_cast<std::uint32_t>(*b));
        const std::int64_t prod = aa * bb;
        if (wide_out) write_reg_lane(uop.outputs[0], warp, lane, static_cast<std::uint64_t>(prod));
        else write_reg_lane(uop.outputs[0], warp, lane, static_cast<std::uint64_t>(static_cast<std::uint32_t>(prod)));
      } else {
        const std::uint64_t aa = static_cast<std::uint32_t>(*a);
        const std::uint64_t bb = static_cast<std::uint32_t>(*b);
        const std::uint64_t prod = aa * bb;
        if (wide_out) write_reg_lane(uop.outputs[0], warp, lane, prod);
        else write_reg_lane(uop.outputs[0], warp, lane, static_cast<std::uint32_t>(prod));
      }
    }

    obs.counter("uop.exec.mul");
    r.progressed = true;
    return r;
  }
  case MicroOpOp::Shl: {
    if (uop.outputs.size() != 1 || uop.inputs.size() != 2) {
      r.diag = Diagnostic{ "units.exec", "E_UOP_ARITY", "SHL expects 2 in, 1 out", std::nullopt, std::nullopt, std::nullopt };
      r.blocked_reason = BlockedReason::Error;
      return r;
    }

    const bool is_64 = (uop.attrs.type == ValueType::U64 || uop.attrs.type == ValueType::S64 ||
                        uop.outputs[0].type == ValueType::U64 || uop.outputs[0].type == ValueType::S64);
    const std::uint32_t shift_mask = is_64 ? 63u : 31u;

    for (std::uint32_t lane = 0; lane < exec_mask.width && lane < 32; lane++) {
      if (!lane_mask_test(exec_mask, lane)) continue;
      auto a = read_operand_lane(uop.inputs[0], warp, lane);
      auto b = read_operand_lane(uop.inputs[1], warp, lane);
      if (!a.has_value() || !b.has_value()) {
        r.diag = Diagnostic{ "units.exec", "E_SPECIAL_UNKNOWN", "unknown special operand", std::nullopt, std::nullopt, std::nullopt };
        r.blocked_reason = BlockedReason::Error;
        return r;
      }

      const std::uint32_t sh = static_cast<std::uint32_t>(*b) & shift_mask;
      if (is_64) {
        const std::uint64_t out = static_cast<std::uint64_t>(*a) << sh;
        write_reg_lane(uop.outputs[0], warp, lane, out);
      } else {
        const std::uint32_t out = static_cast<std::uint32_t>(*a) << sh;
        write_reg_lane(uop.outputs[0], warp, lane, static_cast<std::uint64_t>(out));
      }
    }

    obs.counter("uop.exec.shl");
    r.progressed = true;
    return r;
  }
  case MicroOpOp::Shfl: {
    if (uop.outputs.size() != 1 || uop.inputs.size() != 4) {
      r.diag = Diagnostic{ "units.exec", "E_UOP_ARITY", "SHFL expects 4 in, 1 out", std::nullopt, std::nullopt, std::nullopt };
      r.blocked_reason = BlockedReason::Error;
      return r;
    }

    const bool do_down = has_flag(uop.attrs.flags, "down");
    const bool do_idx = has_flag(uop.attrs.flags, "idx");
    if (static_cast<int>(do_down) + static_cast<int>(do_idx) != 1) {
      r.diag = Diagnostic{ "units.exec", "E_SHFL_MODE", "SHFL requires exactly one of .down or .idx", std::nullopt, std::nullopt, std::nullopt };
      r.blocked_reason = BlockedReason::Error;
      return r;
    }

    for (std::uint32_t lane = 0; lane < exec_mask.width && lane < 32; lane++) {
      if (!lane_mask_test(exec_mask, lane)) continue;

      auto src_self = read_operand_lane(uop.inputs[0], warp, lane);
      auto lane_param = read_operand_lane(uop.inputs[1], warp, lane);
      auto clamp_param = read_operand_lane(uop.inputs[2], warp, lane);
      auto member_param = read_operand_lane(uop.inputs[3], warp, lane);
      if (!src_self.has_value() || !lane_param.has_value() || !clamp_param.has_value() || !member_param.has_value()) {
        r.diag = Diagnostic{ "units.exec", "E_SPECIAL_UNKNOWN", "unknown special operand", std::nullopt, std::nullopt, std::nullopt };
        r.blocked_reason = BlockedReason::Error;
        return r;
      }

      const auto lane_u32 = static_cast<std::uint32_t>(lane);
      const auto lane_or_idx = static_cast<std::uint32_t>(*lane_param);
      const auto clamp = static_cast<std::uint32_t>(*clamp_param);
      const auto member_mask = static_cast<std::uint32_t>(*member_param);

      const auto width = shfl_width_from_clamp(clamp);
      const auto seg_base = lane_u32 - (lane_u32 % width);

      std::uint32_t src_lane = lane_u32;
      if (do_down) {
        const auto delta = lane_or_idx & 0x1Fu;
        const auto lane_in_seg = lane_u32 - seg_base;
        if (lane_in_seg + delta < width) {
          src_lane = lane_u32 + delta;
        }
      } else {
        src_lane = seg_base + ((lane_or_idx & 0x1Fu) % width);
      }

      std::uint64_t out_bits = *src_self;
      const bool lane_in_member = ((member_mask >> lane_u32) & 1u) != 0u;
      const bool src_in_range = src_lane < exec_mask.width && src_lane < 32u;
      const bool src_in_member = src_in_range && (((member_mask >> src_lane) & 1u) != 0u);
      if (lane_in_member && src_in_member) {
        auto src_bits = read_operand_lane(uop.inputs[0], warp, src_lane);
        if (!src_bits.has_value()) {
          r.diag = Diagnostic{ "units.exec", "E_SPECIAL_UNKNOWN", "unknown special operand", std::nullopt, std::nullopt, std::nullopt };
          r.blocked_reason = BlockedReason::Error;
          return r;
        }
        out_bits = *src_bits;
      }

      write_reg_lane(uop.outputs[0], warp, lane, out_bits);
    }

    obs.counter(do_down ? "uop.exec.shfl.down" : "uop.exec.shfl.idx");
    r.progressed = true;
    return r;
  }
  case MicroOpOp::Fma: {
    if (uop.outputs.size() != 1 || uop.inputs.size() != 3) {
      r.diag = Diagnostic{ "units.exec", "E_UOP_ARITY", "FMA expects 3 in, 1 out", std::nullopt, std::nullopt, std::nullopt };
      r.blocked_reason = BlockedReason::Error;
      return r;
    }
    if (uop.attrs.type != ValueType::F32) {
      r.diag = Diagnostic{ "units.exec", "E_UOP_TYPE", "FMA only supports f32 in this milestone", std::nullopt, std::nullopt, std::nullopt };
      r.blocked_reason = BlockedReason::Error;
      return r;
    }

    for (std::uint32_t lane = 0; lane < exec_mask.width && lane < 32; lane++) {
      if (!lane_mask_test(exec_mask, lane)) continue;
      auto a_bits = read_operand_lane(uop.inputs[0], warp, lane);
      auto b_bits = read_operand_lane(uop.inputs[1], warp, lane);
      auto c_bits = read_operand_lane(uop.inputs[2], warp, lane);
      if (!a_bits.has_value() || !b_bits.has_value() || !c_bits.has_value()) {
        r.diag = Diagnostic{ "units.exec", "E_SPECIAL_UNKNOWN", "unknown special operand", std::nullopt, std::nullopt, std::nullopt };
        r.blocked_reason = BlockedReason::Error;
        return r;
      }

      const float a = f32_from_bits(static_cast<std::uint32_t>(*a_bits));
      const float b = f32_from_bits(static_cast<std::uint32_t>(*b_bits));
      const float c = f32_from_bits(static_cast<std::uint32_t>(*c_bits));
      const float out = std::fma(a, b, c);
      write_reg_lane(uop.outputs[0], warp, lane, static_cast<std::uint64_t>(f32_to_bits(out)));
    }
    obs.counter("uop.exec.fma.f32");
    r.progressed = true;
    return r;
  }
  case MicroOpOp::WarpReduceAdd: {
    if (uop.outputs.size() != 1 || uop.inputs.size() != 1) {
      r.diag = Diagnostic{ "units.exec", "E_UOP_ARITY", "WARP_REDUCE_ADD expects 1 in, 1 out", std::nullopt, std::nullopt, std::nullopt };
      r.blocked_reason = BlockedReason::Error;
      return r;
    }
    if (uop.attrs.type != ValueType::F32) {
      r.diag = Diagnostic{ "units.exec", "E_UOP_TYPE", "WARP_REDUCE_ADD only supports f32 in this milestone", std::nullopt, std::nullopt, std::nullopt };
      r.blocked_reason = BlockedReason::Error;
      return r;
    }

    float sum = 0.0f;
    for (std::uint32_t lane = 0; lane < exec_mask.width && lane < 32; lane++) {
      if (!lane_mask_test(exec_mask, lane)) continue;
      auto v_bits = read_operand_lane(uop.inputs[0], warp, lane);
      if (!v_bits.has_value()) {
        r.diag = Diagnostic{ "units.exec", "E_SPECIAL_UNKNOWN", "unknown special operand", std::nullopt, std::nullopt, std::nullopt };
        r.blocked_reason = BlockedReason::Error;
        return r;
      }
      sum += f32_from_bits(static_cast<std::uint32_t>(*v_bits));
    }

    const auto sum_bits = static_cast<std::uint64_t>(f32_to_bits(sum));
    for (std::uint32_t lane = 0; lane < exec_mask.width && lane < 32; lane++) {
      if (!lane_mask_test(exec_mask, lane)) continue;
      write_reg_lane(uop.outputs[0], warp, lane, sum_bits);
    }

    obs.counter("uop.exec.warp_reduce_add.f32");
    r.progressed = true;
    return r;
  }
  case MicroOpOp::Setp: {
    if (uop.outputs.size() != 1 || uop.inputs.size() != 2) {
      r.diag = Diagnostic{ "units.exec", "E_UOP_ARITY", "SETP expects 2 in, 1 out", std::nullopt, std::nullopt, std::nullopt };
      r.blocked_reason = BlockedReason::Error;
      return r;
    }
    if (uop.outputs[0].kind != OperandKind::Pred || uop.outputs[0].pred_id < 0) {
      r.diag = Diagnostic{ "units.exec", "E_OPERAND_KIND", "SETP output must be a predicate", std::nullopt, std::nullopt, std::nullopt };
      r.blocked_reason = BlockedReason::Error;
      return r;
    }

    const bool do_lt = has_flag(uop.attrs.flags, "lt");
    const bool do_le = has_flag(uop.attrs.flags, "le");
    const bool do_gt = has_flag(uop.attrs.flags, "gt");
    const bool do_ge = has_flag(uop.attrs.flags, "ge");
    const bool do_eq = has_flag(uop.attrs.flags, "eq");
    const bool do_ne = has_flag(uop.attrs.flags, "ne");

    int op_count = (do_lt ? 1 : 0) + (do_le ? 1 : 0) + (do_gt ? 1 : 0) + (do_ge ? 1 : 0) + (do_eq ? 1 : 0) + (do_ne ? 1 : 0);
    if (op_count == 0) {
      // Default for bring-up: lt
    } else if (op_count > 1) {
      r.diag = Diagnostic{ "units.exec", "E_SETP_FLAGS", "SETP has multiple compare flags", std::nullopt, std::nullopt, std::nullopt };
      r.blocked_reason = BlockedReason::Error;
      return r;
    }

    const auto warp_size = warp.active.width;
    const auto pid = static_cast<std::size_t>(uop.outputs[0].pred_id);
    const auto base = pid * static_cast<std::size_t>(warp_size);
    if (warp_size == 0 || warp_size > 32) {
      r.diag = Diagnostic{ "units.exec", "E_WARP_SIZE", "invalid warp_size", std::nullopt, std::nullopt, std::nullopt };
      r.blocked_reason = BlockedReason::Error;
      return r;
    }
    if (base + warp_size > warp.p.size()) {
      r.diag = Diagnostic{ "units.exec", "E_PRED_OOB", "predicate id out of range", std::nullopt, std::nullopt, std::nullopt };
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

      bool pred = false;
      if (uop.attrs.type == ValueType::S32 || uop.attrs.type == ValueType::S64) {
        const auto aa = static_cast<std::int32_t>(static_cast<std::uint32_t>(*a));
        const auto bb = static_cast<std::int32_t>(static_cast<std::uint32_t>(*b));
        if (do_le) pred = aa <= bb;
        else if (do_gt) pred = aa > bb;
        else if (do_ge) pred = aa >= bb;
        else if (do_eq) pred = aa == bb;
        else if (do_ne) pred = aa != bb;
        else pred = aa < bb;
      } else {
        const auto aa = static_cast<std::uint32_t>(*a);
        const auto bb = static_cast<std::uint32_t>(*b);
        if (do_le) pred = aa <= bb;
        else if (do_gt) pred = aa > bb;
        else if (do_ge) pred = aa >= bb;
        else if (do_eq) pred = aa == bb;
        else if (do_ne) pred = aa != bb;
        else pred = aa < bb;
      }
      warp.p[base + lane] = pred ? 1 : 0;
    }
    obs.counter("uop.exec.setp");
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
