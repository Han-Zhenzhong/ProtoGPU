#pragma once

#include "gpusim/contracts.h"
#include "gpusim/frontend.h"
#include "gpusim/instruction_desc.h"

#include <optional>
#include <vector>

namespace gpusim {

struct ControlFlowAnalysisResult final {
  // reconv_pc[pc] is set for PCs that contain a control-flow BRA uop.
  // For other PCs, the entry is std::nullopt.
  std::vector<std::optional<PC>> reconv_pc;

  // Sentinel reconvergence PC representing EXIT.
  PC exit_pc = 0;

  std::optional<Diagnostic> diag;
};

ControlFlowAnalysisResult analyze_control_flow(const KernelImage& kernel,
                                              const DescriptorRegistry& registry,
                                              const Expander& expander,
                                              std::uint32_t warp_size);

} // namespace gpusim
