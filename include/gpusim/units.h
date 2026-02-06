#pragma once

#include "gpusim/contracts.h"
#include "gpusim/memory.h"
#include "gpusim/observability.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace gpusim {

struct WarpState final {
  // Launch / context metadata
  LaunchConfig launch;
  Dim3 ctaid;
  std::uint32_t warpid = 0;                 // warp index within CTA
  std::uint32_t lane_base_thread_linear = 0; // warpid * warp_size

  PC pc = 0;
  bool done = false;
  LaneMask active;

  // Lane-wise storage in reg-major layout: [reg][lane]
  std::vector<std::uint32_t> r_u32;
  std::vector<std::uint64_t> r_u64;
  std::vector<std::uint8_t> p;
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
