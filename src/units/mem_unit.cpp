#include "gpusim/units.h"

#include <cstdio>
#include <cstdlib>

namespace gpusim {

namespace {

static std::size_t reg_lane_index(std::size_t reg, std::uint32_t lane, std::uint32_t warp_size) {
  return reg * static_cast<std::size_t>(warp_size) + static_cast<std::size_t>(lane);
}

static std::uint64_t read_reg_lane64(const Operand& o, const WarpState& warp, std::uint32_t lane) {
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

static void write_reg_lane64(const Operand& o, WarpState& warp, std::uint32_t lane, std::uint64_t v) {
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
  if (o.kind == OperandKind::Reg) return read_reg_lane64(o, warp, lane);
  if (o.kind == OperandKind::Special) return read_special_u32(o.special, warp, lane);
  return 0u;
}

static std::uint64_t eval_addr_lane(const Operand& o, const WarpState& warp, std::uint32_t lane) {
  // OperandKind::Addr stores base reg id + imm_i64 offset
  std::uint64_t base = 0;
  if (o.reg_id >= 0) {
    Operand base_reg;
    base_reg.kind = OperandKind::Reg;
    base_reg.type = ValueType::U64;
    base_reg.reg_id = o.reg_id;
    base = read_reg_lane64(base_reg, warp, lane);
  }
  return base + static_cast<std::uint64_t>(o.imm_i64);
}

static std::uint64_t thread_linear_global(const WarpState& warp, std::uint32_t lane) {
  const std::uint64_t gx = warp.launch.grid_dim.x;
  const std::uint64_t gy = warp.launch.grid_dim.y;
  const std::uint64_t cta_linear = static_cast<std::uint64_t>(warp.ctaid.x) +
                                  gx * (static_cast<std::uint64_t>(warp.ctaid.y) + gy * static_cast<std::uint64_t>(warp.ctaid.z));

  const std::uint64_t bx = warp.launch.block_dim.x;
  const std::uint64_t by = warp.launch.block_dim.y;
  const std::uint64_t bz = warp.launch.block_dim.z;
  const std::uint64_t threads_per_block = bx * by * bz;

  const std::uint64_t lane_linear = static_cast<std::uint64_t>(warp.lane_base_thread_linear) + lane;
  return cta_linear * threads_per_block + lane_linear;
}

static std::uint64_t local_effective_addr(const WarpState& warp, std::uint32_t lane, std::uint64_t local_addr) {
  // Give each thread a fixed-size local window to avoid overlap.
  // Current demo uses <= 32 bytes, so 4KB per thread is plenty.
  constexpr std::uint64_t kLocalStride = 4096;
  const std::uint64_t tid = thread_linear_global(warp, lane);
  return tid * kLocalStride + local_addr;
}

static std::vector<std::uint8_t> read_sparse(const std::unordered_map<std::uint64_t, std::uint8_t>& mem,
                                             std::uint64_t addr,
                                             std::uint64_t size) {
  std::vector<std::uint8_t> out;
  out.reserve(static_cast<std::size_t>(size));
  for (std::uint64_t i = 0; i < size; i++) {
    auto it = mem.find(addr + i);
    out.push_back(it == mem.end() ? static_cast<std::uint8_t>(0) : it->second);
  }
  return out;
}

static void write_sparse(std::unordered_map<std::uint64_t, std::uint8_t>& mem,
                         std::uint64_t addr,
                         const std::vector<std::uint8_t>& bytes) {
  for (std::size_t i = 0; i < bytes.size(); i++) {
    mem[addr + static_cast<std::uint64_t>(i)] = bytes[i];
  }
}

} // namespace

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

  const bool log_stores = []() -> bool {
    if (const char* v = std::getenv("GPUSIM_LOG_GLOBAL_STORES")) {
      return v && *v != '\0';
    }
    return false;
  }();

  if (warp.done) {
    r.warp_done = true;
    return r;
  }

  const auto exec_mask = lane_mask_and(warp.active, uop.guard);

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
      for (std::uint32_t lane = 0; lane < exec_mask.width && lane < 32; lane++) {
        if (!lane_mask_test(exec_mask, lane)) continue;
        write_reg_lane64(uop.outputs[0], warp, lane, v);
      }
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
      for (std::uint32_t lane = 0; lane < exec_mask.width && lane < 32; lane++) {
        if (!lane_mask_test(exec_mask, lane)) continue;
        auto addr = eval_addr_lane(src, warp, lane);
        if (sz != 0 && (addr % sz) != 0) {
          r.blocked_reason = BlockedReason::Error;
          r.diag = Diagnostic{ "units.mem", "E_GLOBAL_ALIGN", "global read is misaligned", std::nullopt, std::nullopt, std::nullopt };
          return r;
        }
        auto bytes = mem_.read_global(addr, sz);
        if (!bytes) {
          r.blocked_reason = BlockedReason::Error;
          r.diag = Diagnostic{ "units.mem", "E_GLOBAL_MISS", "global read failed at addr=" + std::to_string(addr) + " size=" + std::to_string(sz), std::nullopt, std::nullopt, std::nullopt };
          return r;
        }
        auto v = unpack_le(*bytes);
        write_reg_lane64(uop.outputs[0], warp, lane, v);
      }
      obs.counter("uop.mem.ld.global");
      r.progressed = true;
      return r;
    }

    if (uop.attrs.space == AddrSpace::Local) {
      if (src.kind != OperandKind::Addr) {
        r.blocked_reason = BlockedReason::Error;
        r.diag = Diagnostic{ "units.mem", "E_LD_LOCAL_KIND", "ld.local expects addr source", std::nullopt, std::nullopt, std::nullopt };
        return r;
      }
      for (std::uint32_t lane = 0; lane < exec_mask.width && lane < 32; lane++) {
        if (!lane_mask_test(exec_mask, lane)) continue;
        const auto local_addr = eval_addr_lane(src, warp, lane);
        const auto eff = local_effective_addr(warp, lane, local_addr);
        auto bytes = read_sparse(local_, eff, sz);
        auto v = unpack_le(bytes);
        write_reg_lane64(uop.outputs[0], warp, lane, v);
      }
      obs.counter("uop.mem.ld.local");
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

    if (uop.attrs.space == AddrSpace::Global) {
      if (dst.kind != OperandKind::Addr) {
        r.blocked_reason = BlockedReason::Error;
        r.diag = Diagnostic{ "units.mem", "E_ST_GLOBAL_KIND", "st.global expects addr dest", std::nullopt, std::nullopt, std::nullopt };
        return r;
      }
      for (std::uint32_t lane = 0; lane < exec_mask.width && lane < 32; lane++) {
        if (!lane_mask_test(exec_mask, lane)) continue;
        auto addr = eval_addr_lane(dst, warp, lane);
        if (sz != 0 && (addr % sz) != 0) {
          r.blocked_reason = BlockedReason::Error;
          r.diag = Diagnostic{ "units.mem", "E_GLOBAL_ALIGN", "global write is misaligned", std::nullopt, std::nullopt, std::nullopt };
          return r;
        }
        auto v = read_operand_lane(val, warp, lane);
        if (!v.has_value()) {
          r.diag = Diagnostic{ "units.mem", "E_SPECIAL_UNKNOWN", "unknown special operand", std::nullopt, std::nullopt, std::nullopt };
          r.blocked_reason = BlockedReason::Error;
          return r;
        }

        if (log_stores && lane == 0 && (warp.warpid == 0 || warp.warpid == 1)) {
          static std::uint32_t logged_mask = 0;
          const std::uint32_t bit = (warp.warpid == 0) ? 1u : 2u;
          if ((logged_mask & bit) == 0) {
            std::fprintf(stderr,
                         "[gpu-sim] st.global(warp=%u lane0 base=%u ctaid.x=%u) addr=0x%llx size=%llu val=0x%llx\n",
                         static_cast<unsigned>(warp.warpid),
                         static_cast<unsigned>(warp.lane_base_thread_linear),
                         static_cast<unsigned>(warp.ctaid.x),
                         static_cast<unsigned long long>(addr),
                         static_cast<unsigned long long>(sz),
                         static_cast<unsigned long long>(*v));
            logged_mask |= bit;
          }
        }

        if (!mem_.write_global(addr, pack_le(*v, sz))) {
          r.blocked_reason = BlockedReason::Error;
          r.diag = Diagnostic{ "units.mem", "E_GLOBAL_MISS", "global write failed at addr=" + std::to_string(addr) + " size=" + std::to_string(sz), std::nullopt, std::nullopt, std::nullopt };
          return r;
        }
      }
      obs.counter("uop.mem.st.global");
      r.progressed = true;
      return r;
    }

    if (uop.attrs.space == AddrSpace::Local) {
      if (dst.kind != OperandKind::Addr) {
        r.blocked_reason = BlockedReason::Error;
        r.diag = Diagnostic{ "units.mem", "E_ST_LOCAL_KIND", "st.local expects addr dest", std::nullopt, std::nullopt, std::nullopt };
        return r;
      }
      for (std::uint32_t lane = 0; lane < exec_mask.width && lane < 32; lane++) {
        if (!lane_mask_test(exec_mask, lane)) continue;
        const auto local_addr = eval_addr_lane(dst, warp, lane);
        const auto eff = local_effective_addr(warp, lane, local_addr);
        auto v = read_operand_lane(val, warp, lane);
        if (!v.has_value()) {
          r.diag = Diagnostic{ "units.mem", "E_SPECIAL_UNKNOWN", "unknown special operand", std::nullopt, std::nullopt, std::nullopt };
          r.blocked_reason = BlockedReason::Error;
          return r;
        }
        write_sparse(local_, eff, pack_le(*v, sz));
      }
      obs.counter("uop.mem.st.local");
      r.progressed = true;
      return r;
    }

    r.blocked_reason = BlockedReason::Error;
    r.diag = Diagnostic{ "units.mem", "E_ST_SPACE", "unsupported st space", std::nullopt, std::nullopt, std::nullopt };
    return r;
  }
  default:
    r.blocked_reason = BlockedReason::Error;
    r.diag = Diagnostic{ "units.mem", "E_UOP_UNSUPPORTED", "unsupported mem uop", std::nullopt, std::nullopt, std::nullopt };
    return r;
  }
}

} // namespace gpusim
