#pragma once

#include "gpusim/contracts.h"
#include "gpusim/memory.h"
#include "gpusim/observability.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace gpusim {

struct WarpState final {
  PC pc = 0;
  bool done = false;
  LaneMask active;

  std::vector<std::uint64_t> r_u32; // packed in u64
  std::vector<std::uint64_t> r_u64;
  std::vector<bool> p;
};

class ExecCore final {
public:
  StepResult step(const MicroOp& uop, WarpState& warp, ObsControl& obs);
};

class ControlUnit final {
public:
  StepResult step(const MicroOp& uop, WarpState& warp, ObsControl& obs);
};

class MemUnit final {
public:
  explicit MemUnit(AddrSpaceManager& mem) : mem_(mem) {}
  StepResult step(const MicroOp& uop, WarpState& warp, ObsControl& obs);

private:
  AddrSpaceManager& mem_;
};

} // namespace gpusim
