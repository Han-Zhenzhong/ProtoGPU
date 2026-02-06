#pragma once

#include "gpusim/abi.h"
#include "gpusim/contracts.h"

#include <optional>
#include <string>
#include <vector>

namespace gpusim {

// Tokenized PTX instruction line (implementation-level interchange between Parser and PTX ISA mapper).
// Intentionally not part of contracts.h.
struct TokenizedPtxInst final {
  std::string ptx_opcode;
  InstMods mods;
  std::optional<PredGuard> pred;
  std::vector<std::string> operand_tokens;
  std::optional<SourceLocation> dbg;
};

struct KernelTokens final {
  std::string name;
  std::uint32_t reg_u32_count = 0;
  std::uint32_t reg_u64_count = 0;
  std::vector<ParamDesc> params;
  std::vector<TokenizedPtxInst> insts;
};

struct ModuleTokens final {
  std::vector<KernelTokens> kernels;
};

} // namespace gpusim
