#include "gpusim/units.h"

namespace gpusim {

static std::uint64_t read_reg64(const Operand& o, const WarpState& warp) {
  if (o.kind != OperandKind::Reg) return 0;
  if (o.type == ValueType::U64 || o.type == ValueType::S64) {
    auto idx = static_cast<std::size_t>(o.reg_id);
    return idx < warp.r_u64.size() ? warp.r_u64[idx] : 0;
  }
  auto idx = static_cast<std::size_t>(o.reg_id);
  return idx < warp.r_u32.size() ? warp.r_u32[idx] : 0;
}

static void write_reg64(const Operand& o, WarpState& warp, std::uint64_t v) {
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
  if (o.kind == OperandKind::Reg) return read_reg64(o, warp);
  return 0;
}

static std::uint64_t eval_addr(const Operand& o, const WarpState& warp) {
  // OperandKind::Addr stores base reg id + imm_i64 offset
  std::uint64_t base = 0;
  if (o.reg_id >= 0) {
    Operand base_reg;
    base_reg.kind = OperandKind::Reg;
    base_reg.type = ValueType::U64;
    base_reg.reg_id = o.reg_id;
    base = read_reg64(base_reg, warp);
  }
  return base + static_cast<std::uint64_t>(o.imm_i64);
}

static std::vector<std::uint8_t> pack_le(std::uint64_t v, std::uint64_t size) {
  std::vector<std::uint8_t> out;
  out.reserve(static_cast<std::size_t>(size));
  for (std::uint64_t i = 0; i < size; i++) {
    out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu));
  }
  return out;
}

static std::uint64_t unpack_le(const std::vector<std::uint8_t>& bytes) {
  std::uint64_t v = 0;
  for (std::size_t i = 0; i < bytes.size() && i < 8; i++) {
    v |= (static_cast<std::uint64_t>(bytes[i]) << (8 * i));
  }
  return v;
}

static std::uint64_t type_size(ValueType t) {
  switch (t) {
  case ValueType::U32:
  case ValueType::S32:
  case ValueType::F32: return 4;
  case ValueType::U64:
  case ValueType::S64: return 8;
  }
  return 4;
}

StepResult MemUnit::step(const MicroOp& uop, WarpState& warp, ObsControl& obs) {
  StepResult r;

  if (warp.done) {
    r.warp_done = true;
    return r;
  }

  switch (uop.op) {
  case MicroOpOp::Ld: {
    if (uop.outputs.size() != 1 || uop.inputs.size() != 1) {
      r.blocked_reason = BlockedReason::Error;
      r.diag = Diagnostic{ "units.mem", "E_UOP_ARITY", "LD expects 1 in, 1 out", std::nullopt, std::nullopt, std::nullopt };
      return r;
    }
    const auto& src = uop.inputs[0];
    auto sz = type_size(uop.attrs.type);
    if (uop.attrs.space == AddrSpace::Param) {
      if (src.kind != OperandKind::Symbol) {
        r.blocked_reason = BlockedReason::Error;
        r.diag = Diagnostic{ "units.mem", "E_LD_PARAM_KIND", "ld.param expects symbol source", std::nullopt, std::nullopt, std::nullopt };
        return r;
      }
      auto bytes = mem_.read_param_symbol(src.symbol, sz);
      if (!bytes) {
        r.blocked_reason = BlockedReason::Error;
        r.diag = Diagnostic{ "units.mem", "E_PARAM_MISS", "param read failed for " + src.symbol, std::nullopt, std::nullopt, std::nullopt };
        return r;
      }
      auto v = unpack_le(*bytes);
      write_reg64(uop.outputs[0], warp, v);
      obs.counter("uop.mem.ld.param");
      r.progressed = true;
      return r;
    }

    if (uop.attrs.space == AddrSpace::Global) {
      if (src.kind != OperandKind::Addr) {
        r.blocked_reason = BlockedReason::Error;
        r.diag = Diagnostic{ "units.mem", "E_LD_GLOBAL_KIND", "ld.global expects addr source", std::nullopt, std::nullopt, std::nullopt };
        return r;
      }
      auto addr = eval_addr(src, warp);
      auto bytes = mem_.read_global(addr, sz);
      if (!bytes) {
        r.blocked_reason = BlockedReason::Error;
        r.diag = Diagnostic{ "units.mem", "E_GLOBAL_MISS", "global read failed", std::nullopt, std::nullopt, std::nullopt };
        return r;
      }
      auto v = unpack_le(*bytes);
      write_reg64(uop.outputs[0], warp, v);
      obs.counter("uop.mem.ld.global");
      r.progressed = true;
      return r;
    }

    r.blocked_reason = BlockedReason::Error;
    r.diag = Diagnostic{ "units.mem", "E_LD_SPACE", "unsupported ld space", std::nullopt, std::nullopt, std::nullopt };
    return r;
  }
  case MicroOpOp::St: {
    if (uop.inputs.size() != 2 || !uop.outputs.empty()) {
      r.blocked_reason = BlockedReason::Error;
      r.diag = Diagnostic{ "units.mem", "E_UOP_ARITY", "ST expects 2 in, 0 out", std::nullopt, std::nullopt, std::nullopt };
      return r;
    }
    const auto& dst = uop.inputs[0];
    const auto& val = uop.inputs[1];
    auto sz = type_size(uop.attrs.type);

    if (uop.attrs.space != AddrSpace::Global) {
      r.blocked_reason = BlockedReason::Error;
      r.diag = Diagnostic{ "units.mem", "E_ST_SPACE", "only st.global supported", std::nullopt, std::nullopt, std::nullopt };
      return r;
    }
    if (dst.kind != OperandKind::Addr) {
      r.blocked_reason = BlockedReason::Error;
      r.diag = Diagnostic{ "units.mem", "E_ST_GLOBAL_KIND", "st.global expects addr dest", std::nullopt, std::nullopt, std::nullopt };
      return r;
    }
    auto addr = eval_addr(dst, warp);
    auto v = read_imm_or_reg(val, warp);
    mem_.write_global(addr, pack_le(v, sz));
    obs.counter("uop.mem.st.global");
    r.progressed = true;
    return r;
  }
  default:
    r.blocked_reason = BlockedReason::Error;
    r.diag = Diagnostic{ "units.mem", "E_UOP_UNSUPPORTED", "unsupported mem uop", std::nullopt, std::nullopt, std::nullopt };
    return r;
  }
}

} // namespace gpusim
