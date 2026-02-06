#pragma once

#include "gpusim/abi.h"
#include "gpusim/contracts.h"
#include "gpusim/ptx_tokens.h"

#include <optional>
#include <string>
#include <vector>

namespace gpusim {

struct KernelImage final {
  std::string name;
  std::uint32_t reg_u32_count = 0;
  std::uint32_t reg_u64_count = 0;
  std::vector<ParamDesc> params;
  std::vector<InstRecord> insts;
};

struct ModuleImage final {
  std::vector<KernelImage> kernels;
};

class Parser final {
public:
  ModuleImage parse_ptx_text(const std::string& ptx_text);
  ModuleImage parse_ptx_file(const std::string& path);

  ModuleTokens parse_ptx_text_tokens(const std::string& ptx_text, const std::string& file_name = "<ptx>");
  ModuleTokens parse_ptx_file_tokens(const std::string& path);
};

class Binder final {
public:
  KernelImage bind_first_kernel(const ModuleImage& m);
  KernelTokens bind_first_kernel(const ModuleTokens& m);
};

} // namespace gpusim
