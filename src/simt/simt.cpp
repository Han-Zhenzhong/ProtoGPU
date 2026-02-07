#include "gpusim/simt.h"

#include "gpusim/json.h"
#include "gpusim/scheduler.h"
#include "gpusim/simt_scheduler.h"

#include <atomic>
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

      warp.r_u32.assign(static_cast<std::size_t>(kernel.reg_u32_count) * warp_size, 0);
      warp.r_u64.assign(static_cast<std::size_t>(kernel.reg_u64_count) * warp_size, 0);
      warp.p.assign(static_cast<std::size_t>(8) * warp_size, 1);
    }

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
        obs_.emit(Event{ next_ts_local(),
                        (uop.kind == MicroOpKind::Exec
                            ? EventCategory::Exec
                            : (uop.kind == MicroOpKind::Control ? EventCategory::Ctrl : EventCategory::Mem)),
                        "UOP",
                        std::nullopt, item.cta_id, warp_id, std::nullopt, sm_id, std::nullopt, std::nullopt, std::nullopt,
                        warp.pc, warp.active, uop_name, std::nullopt, std::nullopt, std::nullopt });

        StepResult sr;
        if (uop.kind == MicroOpKind::Exec) sr = exec_.step(uop, warp, obs_);
        else if (uop.kind == MicroOpKind::Control) sr = ctrl_.step(uop, warp, obs_);
        else sr = mem_unit_.step(uop, warp, obs_);

        if (sr.diag) {
          if (diag_mu && diag_out) {
            std::lock_guard<std::mutex> lock(*diag_mu);
            *diag_out = sr.diag;
          } else {
            res.diag = sr.diag;
          }
          if (scheduler) scheduler->request_stop();
          return;
        }
      }

      if (!warp.done) {
        warp.pc += 1;
        obs_.emit(Event{ next_ts_local(), EventCategory::Commit, "COMMIT",
                        std::nullopt, item.cta_id, warp_id, std::nullopt, sm_id, std::nullopt, std::nullopt, std::nullopt,
                        warp.pc - 1, warp.active, inst.opcode, std::nullopt, std::nullopt, std::nullopt });
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
