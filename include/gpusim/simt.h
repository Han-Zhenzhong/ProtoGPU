#pragma once

#include "gpusim/frontend.h"
#include "gpusim/instruction_desc.h"
#include "gpusim/memory.h"
#include "gpusim/observability.h"
#include "gpusim/units.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gpusim {

struct SimConfig final {
  std::uint32_t warp_size = 32;
  std::uint64_t max_steps = 100000;

  // Multi-SM parallel execution (06.02)
  std::uint32_t sm_count = 1;
  bool parallel = false;
  bool deterministic = false;

  // Modular architecture selectors (10)
  std::string profile; // optional; empty means "implicit defaults"
  std::string cta_scheduler = "fifo";
  std::string warp_scheduler = "in_order_run_to_completion";
  std::string memory_model = "no_cache_addrspace";
  bool allow_unknown_selectors = false;
};

struct SimResult final {
  bool completed = false;
  std::optional<Diagnostic> diag;
  std::uint64_t steps = 0;
};

class SimtExecutor final {
public:
  SimtExecutor(SimConfig cfg, DescriptorRegistry registry, ObsControl& obs, IMemoryModel& mem);

  SimResult run(const KernelImage& kernel);
  SimResult run(const KernelImage& kernel, const LaunchConfig& launch);

private:
  SimConfig cfg_;
  DescriptorRegistry registry_;
  Expander expander_;
  ObsControl& obs_;
  IMemoryModel& mem_;

  ExecCore exec_;
  ControlUnit ctrl_;
  MemUnit mem_unit_;
};

} // namespace gpusim
