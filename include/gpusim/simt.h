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
};

struct SimResult final {
  bool completed = false;
  std::optional<Diagnostic> diag;
  std::uint64_t steps = 0;
};

class SimtExecutor final {
public:
  SimtExecutor(SimConfig cfg, DescriptorRegistry registry, ObsControl& obs, AddrSpaceManager& mem);

  SimResult run(const KernelImage& kernel);

private:
  SimConfig cfg_;
  DescriptorRegistry registry_;
  Expander expander_;
  ObsControl& obs_;
  AddrSpaceManager& mem_;

  ExecCore exec_;
  ControlUnit ctrl_;
  MemUnit mem_unit_;
};

} // namespace gpusim
