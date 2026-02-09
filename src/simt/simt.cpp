#include "gpusim/simt.h"

#include "gpusim/json.h"
#include "gpusim/scheduler.h"
#include "gpusim/simt_scheduler.h"

#include "control_flow_analysis.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <mutex>
#include <sstream>
#include <thread>

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

SimtExecutor::SimtExecutor(SimConfig cfg, DescriptorRegistry registry, ObsControl& obs, IMemoryModel& mem)
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

static LaneMask lane_mask_or_same_width(const LaneMask& a, const LaneMask& b) {
  LaneMask m;
  m.width = a.width;
  m.bits_lo = a.bits_lo | b.bits_lo;
  return m;
}

static LaneMask lane_mask_not_width(const LaneMask& m) {
  LaneMask r;
  r.width = m.width;
  if (m.width >= 32) {
    r.bits_lo = ~m.bits_lo;
    return r;
  }
  const std::uint32_t mask = (m.width == 0) ? 0u : ((1u << m.width) - 1u);
  r.bits_lo = (~m.bits_lo) & mask;
  return r;
}

static bool lane_mask_empty(const LaneMask& m) { return m.bits_lo == 0u; }

SimResult SimtExecutor::run(const KernelImage& kernel, const LaunchConfig& launch) {
  SimResult res;
  std::uint64_t ts = 0;

  // Baseline deterministic mode: force the existing single-threaded behavior.
  const bool parallel_enabled = cfg_.parallel && !cfg_.deterministic && cfg_.sm_count > 1;
  const std::uint32_t sm_count = parallel_enabled ? cfg_.sm_count : 1;

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

  // Validate memory model selector (current implementation is no-cache + addrspace).
  if (cfg_.memory_model != "no_cache_addrspace") {
    if (!cfg_.allow_unknown_selectors) {
      res.diag = Diagnostic{ "simt", "E_MEMORY_MODEL", "unknown memory.model: " + cfg_.memory_model, std::nullopt, kernel.name, std::nullopt };
      return res;
    }
  }

  // Create CTA scheduler implementation.
  auto cta_sched = create_cta_scheduler(cfg_.cta_scheduler, sm_count);
  if (!cta_sched) {
    if (!cfg_.allow_unknown_selectors) {
      res.diag = Diagnostic{ "simt", "E_CTA_SCHED", "unknown CTA scheduler: " + cfg_.cta_scheduler, std::nullopt, kernel.name, std::nullopt };
      return res;
    }
    cta_sched = create_cta_scheduler("fifo", sm_count);
  }

  // Validate warp scheduler selector (warp schedulers are stateful; a fresh instance is created per CTA).
  {
    auto ws = create_warp_scheduler(cfg_.warp_scheduler);
    if (!ws) {
      if (!cfg_.allow_unknown_selectors) {
        res.diag = Diagnostic{ "simt", "E_WARP_SCHED", "unknown warp scheduler: " + cfg_.warp_scheduler, std::nullopt, kernel.name, std::nullopt };
        return res;
      }
    }
  }

  std::atomic<std::uint64_t> shared_clock{ 0 };
  std::atomic<std::uint64_t> shared_steps{ 0 };

  std::atomic<std::uint64_t>* shared_clock_ptr = parallel_enabled ? &shared_clock : nullptr;
  std::atomic<std::uint64_t>* shared_steps_ptr = parallel_enabled ? &shared_steps : nullptr;

  const auto next_ts = [&]() -> std::uint64_t {
    if (shared_clock_ptr) return shared_clock_ptr->fetch_add(1, std::memory_order_relaxed);
    return ts++;
  };

  // Emit a one-time config summary for debuggability of modular composition.
  {
    gpusim::json::Object components;
    components.emplace("cta_scheduler", gpusim::json::Value(cfg_.cta_scheduler));
    components.emplace("warp_scheduler", gpusim::json::Value(cfg_.warp_scheduler));
    components.emplace("memory_model", gpusim::json::Value(cfg_.memory_model));

    gpusim::json::Object o;
    o.emplace("profile", gpusim::json::Value(cfg_.profile));
    o.emplace("components", gpusim::json::Value(std::move(components)));
    o.emplace("sm_count", gpusim::json::Value(static_cast<double>(sm_count)));
    o.emplace("parallel", gpusim::json::Value(parallel_enabled));
    o.emplace("deterministic", gpusim::json::Value(cfg_.deterministic));

    obs_.emit(Event{ next_ts(), EventCategory::Stream, "RUN_START",
                    std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                    std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                    gpusim::json::stringify(gpusim::json::Value(std::move(o))) });
  }

  // Submit CTA work items into the selected CTA scheduler.
  for (std::uint32_t cz = 0; cz < launch.grid_dim.z; cz++) {
    for (std::uint32_t cy = 0; cy < launch.grid_dim.y; cy++) {
      for (std::uint32_t cx = 0; cx < launch.grid_dim.x; cx++) {
        const std::uint64_t cta_linear = (static_cast<std::uint64_t>(cz) * launch.grid_dim.y + cy) * launch.grid_dim.x + cx;
        cta_sched->submit(CtaWorkItem{ cx, cy, cz, cta_linear, static_cast<CTAId>(cta_linear) });
      }
    }
  }
  cta_sched->close();

  auto run_one_cta = [&](const CtaWorkItem& item,
                        const std::optional<SmId>& sm_id,
                        std::atomic<std::uint64_t>* shared_clock_local,
                        std::atomic<std::uint64_t>* shared_steps_local,
                        ICtaScheduler* scheduler,
                        std::mutex* diag_mu,
                        std::optional<Diagnostic>* diag_out) -> void {
    const auto next_ts_local = [&]() -> std::uint64_t {
      if (shared_clock_local) return shared_clock_local->fetch_add(1, std::memory_order_relaxed);
      return ts++;
    };

    auto should_stop_for_steps = [&]() -> bool {
      if (!shared_steps_local) return res.steps >= cfg_.max_steps;
      return shared_steps_local->load(std::memory_order_relaxed) >= cfg_.max_steps;
    };

    std::vector<WarpState> warps;
    warps.resize(static_cast<std::size_t>(warp_count));
    for (std::uint32_t w = 0; w < warp_count; w++) {
      auto& warp = warps[static_cast<std::size_t>(w)];
      warp.launch = launch;
      warp.launch.warp_size = warp_size;
      warp.ctaid = Dim3{ item.cx, item.cy, item.cz };
      warp.warpid = w;
      warp.lane_base_thread_linear = w * warp_size;
      warp.pc = 0;
      warp.done = false;

      const std::uint64_t remaining = (threads_per_block > warp.lane_base_thread_linear)
                                          ? (threads_per_block - warp.lane_base_thread_linear)
                                          : 0;
      const std::uint32_t lanes_on = static_cast<std::uint32_t>(remaining >= warp_size ? warp_size : remaining);
      warp.active = lane_mask_prefix(warp_size, lanes_on);

      warp.exited.width = warp_size;
      warp.exited.bits_lo = 0u;
      warp.simt_stack.clear();
      {
        SimtFrame f;
        f.pc = 0;
        f.active = warp.active;
        f.reconv_pc = static_cast<PC>(kernel.insts.size());
        f.is_join = false;
        warp.simt_stack.push_back(f);
      }

      warp.r_u32.assign(static_cast<std::size_t>(kernel.reg_u32_count) * warp_size, 0);
      warp.r_u64.assign(static_cast<std::size_t>(kernel.reg_u64_count) * warp_size, 0);
      warp.p.assign(static_cast<std::size_t>(8) * warp_size, 1);
    }

    const bool log_warps = []() -> bool {
      if (const char* v = std::getenv("GPUSIM_LOG_WARPS")) {
        return v && *v != '\0';
      }
      return false;
    }();
    if (log_warps) {
      std::fprintf(stderr,
                   "[gpu-sim] cta_start(ctaid=(%u,%u,%u) cta_linear=%llu) threads_per_block=%llu warp_size=%u warp_count=%llu\n",
                   static_cast<unsigned>(item.cx),
                   static_cast<unsigned>(item.cy),
                   static_cast<unsigned>(item.cz),
                   static_cast<unsigned long long>(item.cta_linear),
                   static_cast<unsigned long long>(threads_per_block),
                   static_cast<unsigned>(warp_size),
                   static_cast<unsigned long long>(warp_count));
      for (std::size_t wi = 0; wi < warps.size(); wi++) {
        const auto& w = warps[wi];
        std::fprintf(stderr,
                     "[gpu-sim]  warp[%zu]: warpid=%u base=%u active=%s\n",
                     wi,
                     static_cast<unsigned>(w.warpid),
                     static_cast<unsigned>(w.lane_base_thread_linear),
                     lane_mask_to_hex(w.active).c_str());
      }
    }

    // Precompute reconvergence PC for predicated BRA instructions.
    const auto cfa = analyze_control_flow(kernel, registry_, expander_, warp_size);
    if (cfa.diag) {
      if (diag_mu && diag_out) {
        std::lock_guard<std::mutex> lock(*diag_mu);
        *diag_out = *cfa.diag;
      } else {
        res.diag = *cfa.diag;
      }
      if (scheduler) scheduler->request_stop();
      return;
    }

    const auto emit_simt_ctrl = [&](const std::uint64_t ts_local,
                                   const std::string& action,
                                   const CTAId cta_id,
                                   const WarpId warp_id,
                                   const PC pc,
                                   const LaneMask mask,
                                   gpusim::json::Object extra) {
      obs_.emit(Event{ ts_local, EventCategory::Ctrl, action,
                      std::nullopt, cta_id, warp_id, std::nullopt, sm_id, std::nullopt, std::nullopt, std::nullopt,
                      pc, mask, std::nullopt, std::nullopt, std::nullopt, gpusim::json::stringify(gpusim::json::Value(std::move(extra))) });
    };

    const auto normalize_stack = [&](WarpState& warp, const CTAId cta_id, const WarpId warp_id) -> bool {
      while (true) {
        if (warp.simt_stack.empty()) {
          warp.done = true;
          warp.active.width = warp_size;
          warp.active.bits_lo = 0u;
          return false;
        }

        auto& top = warp.simt_stack.back();
        top.active = lane_mask_and(top.active, lane_mask_not_width(warp.exited));

        if (lane_mask_empty(top.active)) {
          gpusim::json::Object extra;
          extra.emplace("reason", gpusim::json::Value("empty_active"));
          extra.emplace("stack_depth_after", gpusim::json::Value(static_cast<double>(warp.simt_stack.size() - 1)));
          emit_simt_ctrl(next_ts_local(), "SIMT_POP", cta_id, warp_id, top.pc, top.active, std::move(extra));
          warp.simt_stack.pop_back();
          continue;
        }

        if (top.is_join) {
          // Join frame becomes an executable path only after all child paths are popped.
          top.is_join = false;
          warp.pc = top.pc;
          warp.active = top.active;
          return true;
        }

        if (top.pc == top.reconv_pc) {
          const PC reconv_pc = top.pc;
          const LaneMask arrived = top.active;
          warp.simt_stack.pop_back();

          // Find nearest join frame for this reconv_pc.
          bool found_join = false;
          for (auto it = warp.simt_stack.rbegin(); it != warp.simt_stack.rend(); ++it) {
            if (it->is_join && it->pc == reconv_pc) {
              it->active = lane_mask_or_same_width(it->active, arrived);
              found_join = true;

              gpusim::json::Object extra;
              extra.emplace("reconv_pc", gpusim::json::Value(static_cast<double>(reconv_pc)));
              extra.emplace("arrived", gpusim::json::Value(lane_mask_to_hex(arrived)));
              extra.emplace("join_active", gpusim::json::Value(lane_mask_to_hex(it->active)));
              extra.emplace("stack_depth_after", gpusim::json::Value(static_cast<double>(warp.simt_stack.size())));
              emit_simt_ctrl(next_ts_local(), "SIMT_MERGE", cta_id, warp_id, reconv_pc, it->active, std::move(extra));
              break;
            }
          }
          if (!found_join) {
            const auto d = Diagnostic{ "simt", "E_RECONV_INVALID", "reconv join frame not found", std::nullopt, kernel.name, reconv_pc };
            if (diag_mu && diag_out) {
              std::lock_guard<std::mutex> lock(*diag_mu);
              *diag_out = d;
            } else {
              res.diag = d;
            }
            if (scheduler) scheduler->request_stop();
            return false;
          }
          continue;
        }

        warp.pc = top.pc;
        warp.active = top.active;
        return true;
      }
    };

    auto local_warp_sched = create_warp_scheduler(cfg_.warp_scheduler);
    if (!local_warp_sched) local_warp_sched = create_warp_scheduler("in_order_run_to_completion");
    local_warp_sched->on_cta_start(static_cast<std::uint32_t>(warp_count));

    while (!should_stop_for_steps()) {
      if (scheduler && scheduler->stop_requested()) return;

      auto pick = local_warp_sched->pick_next(static_cast<std::uint32_t>(warp_count), [&](std::uint32_t w) {
        return w >= warp_count ? true : warps[static_cast<std::size_t>(w)].done;
      });
      if (!pick.has_value()) return;

      auto& warp = warps[static_cast<std::size_t>(*pick)];
      if (warp.done) continue;

      const WarpId warp_id = static_cast<WarpId>((item.cta_linear << 32) | static_cast<std::uint64_t>(warp.warpid));

      if (!normalize_stack(warp, item.cta_id, warp_id)) {
        // normalize_stack returns false for two reasons:
        //  1) The warp has no remaining SIMT frames (warp is done)
        //  2) A fatal reconvergence/stack error occurred (diag was set)
        // In case (1), keep scheduling other warps in the CTA.
        // In case (2), stop this CTA immediately.
        if ((diag_out && diag_out->has_value()) || res.diag.has_value()) {
          if (scheduler) scheduler->request_stop();
          return;
        }
        continue;
      }

      if (warp.pc < 0 || static_cast<std::size_t>(warp.pc) >= kernel.insts.size()) {
        const auto d = Diagnostic{ "simt", "E_PC_OOB", "PC out of range", std::nullopt, kernel.name, warp.pc };
        if (diag_mu && diag_out) {
          std::lock_guard<std::mutex> lock(*diag_mu);
          *diag_out = d;
        } else {
          res.diag = d;
        }
        if (scheduler) scheduler->request_stop();
        return;
      }

      const auto& inst = kernel.insts[static_cast<std::size_t>(warp.pc)];
      obs_.emit(Event{ next_ts_local(), EventCategory::Fetch, "FETCH",
                      std::nullopt, item.cta_id, warp_id, std::nullopt, sm_id, std::nullopt, std::nullopt, std::nullopt,
                      warp.pc, warp.active, inst.opcode, std::nullopt, std::nullopt, std::nullopt });
      obs_.counter("inst.fetch");

      auto desc = registry_.lookup(inst);
      if (!desc) {
        const auto d = Diagnostic{ "simt", "E_DESC_MISS", "no inst descriptor for " + format_inst_sig(inst), inst.dbg, kernel.name, warp.pc };
        if (diag_mu && diag_out) {
          std::lock_guard<std::mutex> lock(*diag_mu);
          *diag_out = d;
        } else {
          res.diag = d;
        }
        if (scheduler) scheduler->request_stop();
        return;
      }

      auto uops = expander_.expand(inst, *desc, warp_size);

      // Apply instruction-level predication by intersecting each uop.guard with a predicate mask.
      if (inst.pred.has_value()) {
        const auto pid_i64 = inst.pred->pred_id;
        if (pid_i64 < 0) {
          const auto d = Diagnostic{ "simt", "E_PRED_OOB", "predicate id out of range", inst.dbg, kernel.name, warp.pc };
          if (diag_mu && diag_out) {
            std::lock_guard<std::mutex> lock(*diag_mu);
            *diag_out = d;
          } else {
            res.diag = d;
          }
          if (scheduler) scheduler->request_stop();
          return;
        }
        const auto pid = static_cast<std::size_t>(pid_i64);
        const auto base = pid * static_cast<std::size_t>(warp_size);
        if (base + warp_size > warp.p.size()) {
          const auto d = Diagnostic{ "simt", "E_PRED_OOB", "predicate id out of range", inst.dbg, kernel.name, warp.pc };
          if (diag_mu && diag_out) {
            std::lock_guard<std::mutex> lock(*diag_mu);
            *diag_out = d;
          } else {
            res.diag = d;
          }
          if (scheduler) scheduler->request_stop();
          return;
        }

        LaneMask pred_mask;
        pred_mask.width = warp_size;
        pred_mask.bits_lo = 0u;
        for (std::uint32_t lane = 0; lane < warp_size && lane < 32; lane++) {
          if (!lane_mask_test(warp.active, lane)) continue;
          const bool pval = warp.p[base + lane] != 0;
          const bool enabled = inst.pred->is_negated ? (!pval) : pval;
          if (enabled) pred_mask.bits_lo |= (1u << lane);
        }
        for (auto& u : uops) {
          u.guard = lane_mask_and(u.guard, pred_mask);
        }
      }

      std::optional<PC> next_pc;
      struct PendingSplit final {
        bool active = false;
        PC target_pc = 0;
        PC reconv_pc = 0;
        LaneMask taken;
        LaneMask fallthrough;
        PC pc = 0;
      } split;

      bool any_ret_masked = false;
      for (const auto& uop : uops) {
        auto uop_name = [&]() -> std::string {
          switch (uop.op) {
          case MicroOpOp::Mov: return "MOV";
          case MicroOpOp::Add: return "ADD";
          case MicroOpOp::Mul: return "MUL";
          case MicroOpOp::Shl: return "SHL";
          case MicroOpOp::Fma: return "FMA";
          case MicroOpOp::Setp: return "SETP";
          case MicroOpOp::Ld: return "LD";
          case MicroOpOp::St: return "ST";
          case MicroOpOp::Bra: return "BRA";
          case MicroOpOp::Ret: return "RET";
          }
          return "UOP";
        }();
        obs_.emit(Event{ next_ts_local(),
                        (uop.kind == MicroOpKind::Exec
                            ? EventCategory::Exec
                            : (uop.kind == MicroOpKind::Control ? EventCategory::Ctrl : EventCategory::Mem)),
                        "UOP",
                        std::nullopt, item.cta_id, warp_id, std::nullopt, sm_id, std::nullopt, std::nullopt, std::nullopt,
                        warp.pc, warp.active, uop_name, std::nullopt, std::nullopt, std::nullopt });

        StepResult sr;
        if (uop.kind == MicroOpKind::Exec) {
          sr = exec_.step(uop, warp, obs_);
        } else if (uop.kind == MicroOpKind::Control) {
          if (uop.op == MicroOpOp::Ret) {
            const auto exec_mask = lane_mask_and(warp.active, uop.guard);
            if (exec_mask.bits_lo != 0u) {
              warp.exited = lane_mask_or_same_width(warp.exited, exec_mask);
              // Update active lanes for the current path frame (top of stack).
              if (!warp.simt_stack.empty()) {
                warp.simt_stack.back().active = lane_mask_and(warp.simt_stack.back().active, lane_mask_not_width(exec_mask));
                warp.active = warp.simt_stack.back().active;
              }
              any_ret_masked = true;
            }
            obs_.counter("uop.ctrl.ret");
            sr.progressed = true;
          } else {
            sr = ctrl_.step(uop, warp, obs_);
            if (uop.op == MicroOpOp::Bra && sr.next_pc.has_value()) {
              const auto taken = lane_mask_and(warp.active, uop.guard);
              const auto fallthrough = lane_mask_and(warp.active, lane_mask_not_width(uop.guard));
              if (!lane_mask_empty(taken) && !lane_mask_empty(fallthrough)) {
                const PC pc_here = warp.pc;
                if (pc_here < 0 || static_cast<std::size_t>(pc_here) >= cfa.reconv_pc.size() || !cfa.reconv_pc[static_cast<std::size_t>(pc_here)].has_value()) {
                  const auto d = Diagnostic{ "simt", "E_RECONV_MISS", "missing reconv_pc for divergent BRA", inst.dbg, kernel.name, pc_here };
                  if (diag_mu && diag_out) {
                    std::lock_guard<std::mutex> lock(*diag_mu);
                    *diag_out = d;
                  } else {
                    res.diag = d;
                  }
                  if (scheduler) scheduler->request_stop();
                  return;
                }
                split.active = true;
                split.pc = pc_here;
                split.target_pc = *sr.next_pc;
                split.reconv_pc = *cfa.reconv_pc[static_cast<std::size_t>(pc_here)];
                split.taken = taken;
                split.fallthrough = fallthrough;

                gpusim::json::Object extra;
                extra.emplace("target_pc", gpusim::json::Value(static_cast<double>(split.target_pc)));
                extra.emplace("reconv_pc", gpusim::json::Value(static_cast<double>(split.reconv_pc)));
                extra.emplace("mask_active", gpusim::json::Value(lane_mask_to_hex(warp.active)));
                extra.emplace("mask_taken", gpusim::json::Value(lane_mask_to_hex(taken)));
                extra.emplace("mask_fallthrough", gpusim::json::Value(lane_mask_to_hex(fallthrough)));
                emit_simt_ctrl(next_ts_local(), "SIMT_SPLIT", item.cta_id, warp_id, pc_here, warp.active, std::move(extra));
              }
            }
          }
        } else {
          sr = mem_unit_.step(uop, warp, obs_);
        }

        if (sr.next_pc.has_value()) {
          if (next_pc.has_value() && *next_pc != *sr.next_pc) {
            const auto d = Diagnostic{ "simt", "E_NEXT_PC_CONFLICT", "multiple control uops set next_pc", inst.dbg, kernel.name, warp.pc };
            if (diag_mu && diag_out) {
              std::lock_guard<std::mutex> lock(*diag_mu);
              *diag_out = d;
            } else {
              res.diag = d;
            }
            if (scheduler) scheduler->request_stop();
            return;
          }
          next_pc = *sr.next_pc;
        }

        if (sr.diag) {
          auto d = *sr.diag;
          if (!d.function) d.function = kernel.name;
          if (!d.inst_index) d.inst_index = warp.pc;
          if (!d.location && inst.dbg) d.location = inst.dbg;

          if (diag_mu && diag_out) {
            std::lock_guard<std::mutex> lock(*diag_mu);
            *diag_out = d;
          } else {
            res.diag = d;
          }
          if (scheduler) scheduler->request_stop();
          return;
        }
      }

      if (!warp.done) {
        const auto old_pc = warp.pc;

        if (!warp.simt_stack.empty()) {
          auto& top = warp.simt_stack.back();

          if (split.active) {
            // Replace current path frame with a join + two child paths.
            const PC outer_reconv = top.reconv_pc;
            warp.simt_stack.pop_back();

            SimtFrame join;
            join.pc = split.reconv_pc;
            join.active.width = warp_size;
            join.active.bits_lo = 0u;
            join.reconv_pc = outer_reconv;
            join.is_join = true;

            SimtFrame fall;
            fall.pc = old_pc + 1;
            fall.active = split.fallthrough;
            fall.reconv_pc = split.reconv_pc;
            fall.is_join = false;

            SimtFrame taken;
            taken.pc = split.target_pc;
            taken.active = split.taken;
            taken.reconv_pc = split.reconv_pc;
            taken.is_join = false;

            warp.simt_stack.push_back(join);
            warp.simt_stack.push_back(fall);
            warp.simt_stack.push_back(taken);
          } else {
            if (next_pc.has_value()) {
              top.pc = *next_pc;
            } else {
              top.pc = old_pc + 1;
            }
          }
        }

        // Refresh warp.pc/active from stack top.
        if (!warp.simt_stack.empty()) {
          warp.pc = warp.simt_stack.back().pc;
          warp.active = warp.simt_stack.back().active;
        } else {
          warp.done = true;
        }

        obs_.emit(Event{ next_ts_local(), EventCategory::Commit, "COMMIT",
                        std::nullopt, item.cta_id, warp_id, std::nullopt, sm_id, std::nullopt, std::nullopt, std::nullopt,
                        old_pc, warp.active, inst.opcode, std::nullopt, std::nullopt, std::nullopt });
        obs_.counter("inst.commit");
      }

      if (shared_steps_local) {
        shared_steps_local->fetch_add(1, std::memory_order_relaxed);
      } else {
        res.steps++;
      }
    }

    if (scheduler) scheduler->request_stop();
    return;
  };

  if (parallel_enabled) {
    ICtaScheduler& scheduler = *cta_sched;
    std::mutex diag_mu;
    std::optional<Diagnostic> diag;

    std::vector<std::thread> workers;
    workers.reserve(sm_count);
    for (std::uint32_t i = 0; i < sm_count; i++) {
      workers.emplace_back([&, sm_id = static_cast<SmId>(i)]() {
        while (true) {
          if (shared_steps.load(std::memory_order_relaxed) >= cfg_.max_steps) {
            scheduler.request_stop();
            return;
          }
          auto item = scheduler.acquire(sm_id);
          if (!item.has_value()) return;
          run_one_cta(*item, sm_id, &shared_clock, &shared_steps, &scheduler, &diag_mu, &diag);
          if (scheduler.stop_requested()) return;
        }
      });
    }
    for (auto& t : workers) t.join();

    res.steps = shared_steps.load(std::memory_order_relaxed);
    if (diag) res.diag = diag;
    res.completed = !res.diag.has_value();
    return res;
  }

  // Single-threaded path uses the selected CTA scheduler as well.
  while (true) {
    auto item = cta_sched->acquire(0);
    if (!item.has_value()) break;
    run_one_cta(*item, std::nullopt, nullptr, nullptr, cta_sched.get(), nullptr, nullptr);
    if (res.diag) {
      res.completed = false;
      return res;
    }
  }

  res.completed = !res.diag.has_value();
  return res;
}

} // namespace gpusim
