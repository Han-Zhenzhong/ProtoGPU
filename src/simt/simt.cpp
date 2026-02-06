#include "gpusim/simt.h"

#include <limits>
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
  LaunchConfig launch;
  launch.grid_dim = Dim3{ 1, 1, 1 };
  launch.block_dim = Dim3{ cfg_.warp_size, 1, 1 };
  launch.warp_size = cfg_.warp_size;
  return run(kernel, launch);
}

static bool mul_u32_overflow(std::uint32_t a, std::uint32_t b, std::uint64_t& out) {
  out = static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(b);
  return out > 0xFFFF'FFFFull;
}

static bool mul_u64_overflow(std::uint64_t a, std::uint64_t b, std::uint64_t& out) {
  if (a == 0 || b == 0) {
    out = 0;
    return false;
  }
  if (a > (std::numeric_limits<std::uint64_t>::max)() / b) return true;
  out = a * b;
  return false;
}
static LaneMask lane_mask_prefix(std::uint32_t warp_size, std::uint32_t lanes_on) {
  LaneMask m;
  m.width = warp_size;
  if (warp_size == 0) {
    m.bits_lo = 0;
    return m;
  }
  if (lanes_on >= 32) {
    m.bits_lo = 0xFFFF'FFFFu;
    return m;
  }
  if (lanes_on == 0) {
    m.bits_lo = 0u;
    return m;
  }
  m.bits_lo = (1u << lanes_on) - 1u;
  return m;
}

SimResult SimtExecutor::run(const KernelImage& kernel, const LaunchConfig& launch) {
  SimResult res;
  std::uint64_t ts = 0;

  if (launch.grid_dim.x == 0 || launch.grid_dim.y == 0 || launch.grid_dim.z == 0 ||
      launch.block_dim.x == 0 || launch.block_dim.y == 0 || launch.block_dim.z == 0) {
    res.diag = Diagnostic{ "simt", "E_LAUNCH_DIM", "grid/block dims must be >= 1", std::nullopt, kernel.name, std::nullopt };
    return res;
  }

  std::uint64_t threads_xy = 0;
  std::uint64_t threads_per_block = 0;
  const bool of1 = mul_u64_overflow(launch.block_dim.x, launch.block_dim.y, threads_xy);
  const bool of2 = mul_u64_overflow(threads_xy, launch.block_dim.z, threads_per_block);
  if (of1 || of2) {
    res.diag = Diagnostic{ "simt", "E_LAUNCH_OVERFLOW", "threads_per_block overflow", std::nullopt, kernel.name, std::nullopt };
    return res;
  }

  const auto warp_size = (launch.warp_size == 0) ? cfg_.warp_size : launch.warp_size;
  if (warp_size == 0 || warp_size > 32) {
    res.diag = Diagnostic{ "simt", "E_WARP_SIZE", "warp_size must be in [1,32]", std::nullopt, kernel.name, std::nullopt };
    return res;
  }
  const std::uint64_t warp_count = (threads_per_block + warp_size - 1) / warp_size;

  for (std::uint32_t cz = 0; cz < launch.grid_dim.z; cz++) {
    for (std::uint32_t cy = 0; cy < launch.grid_dim.y; cy++) {
      for (std::uint32_t cx = 0; cx < launch.grid_dim.x; cx++) {
        const std::uint64_t cta_linear = (static_cast<std::uint64_t>(cz) * launch.grid_dim.y + cy) * launch.grid_dim.x + cx;
        const CTAId cta_id = static_cast<CTAId>(cta_linear);

        for (std::uint32_t w = 0; w < warp_count; w++) {
          const WarpId warp_id = static_cast<WarpId>((cta_linear << 32) | static_cast<std::uint64_t>(w));

          WarpState warp;
          warp.launch = launch;
          warp.launch.warp_size = warp_size;
          warp.ctaid = Dim3{ cx, cy, cz };
          warp.warpid = w;
          warp.lane_base_thread_linear = w * warp_size;
          warp.pc = 0;
          warp.done = false;

          const std::uint64_t remaining = (threads_per_block > warp.lane_base_thread_linear)
                                              ? (threads_per_block - warp.lane_base_thread_linear)
                                              : 0;
          const std::uint32_t lanes_on = static_cast<std::uint32_t>(remaining >= warp_size ? warp_size : remaining);
          warp.active = lane_mask_prefix(warp_size, lanes_on);

          warp.r_u32.assign(static_cast<std::size_t>(kernel.reg_u32_count) * warp_size, 0);
          warp.r_u64.assign(static_cast<std::size_t>(kernel.reg_u64_count) * warp_size, 0);
          warp.p.assign(static_cast<std::size_t>(8) * warp_size, 1);

          while (!warp.done && res.steps < cfg_.max_steps) {
            if (warp.pc < 0 || static_cast<std::size_t>(warp.pc) >= kernel.insts.size()) {
              res.diag = Diagnostic{ "simt", "E_PC_OOB", "PC out of range", std::nullopt, kernel.name, warp.pc };
              return res;
            }

            const auto& inst = kernel.insts[static_cast<std::size_t>(warp.pc)];
            obs_.emit(Event{ ts++, EventCategory::Fetch, "FETCH",
                            std::nullopt, cta_id, warp_id, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                            warp.pc, warp.active, inst.opcode, std::nullopt, std::nullopt, std::nullopt });
            obs_.counter("inst.fetch");

            auto desc = registry_.lookup(inst);
            if (!desc) {
              res.diag = Diagnostic{ "simt", "E_DESC_MISS", "no inst descriptor for " + format_inst_sig(inst), inst.dbg, kernel.name, warp.pc };
              return res;
            }

            auto uops = expander_.expand(inst, *desc, warp_size);
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
                              "UOP",
                              std::nullopt, cta_id, warp_id, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                              warp.pc, warp.active, uop_name, std::nullopt, std::nullopt, std::nullopt });

              StepResult sr;
              if (uop.kind == MicroOpKind::Exec) sr = exec_.step(uop, warp, obs_);
              else if (uop.kind == MicroOpKind::Control) sr = ctrl_.step(uop, warp, obs_);
              else sr = mem_unit_.step(uop, warp, obs_);

              if (sr.diag) {
                res.diag = sr.diag;
                return res;
              }
            }

            if (!warp.done) {
              warp.pc += 1;
              obs_.emit(Event{ ts++, EventCategory::Commit, "COMMIT",
                              std::nullopt, cta_id, warp_id, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                              warp.pc - 1, warp.active, inst.opcode, std::nullopt, std::nullopt, std::nullopt });
              obs_.counter("inst.commit");
            }

            res.steps++;
          }
        }
      }
    }
  }

  res.completed = !res.diag.has_value();
  return res;
}

} // namespace gpusim
