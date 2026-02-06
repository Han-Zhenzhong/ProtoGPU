#pragma once

#include "gpusim/abi.h"
#include "gpusim/contracts.h"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace gpusim {

class AddrSpaceManager final {
public:
  DevicePtr alloc_global(std::uint64_t bytes, std::uint64_t align = 16);
  void write_global(std::uint64_t addr, const std::vector<std::uint8_t>& bytes);
  std::optional<std::vector<std::uint8_t>> read_global(std::uint64_t addr, std::uint64_t size) const;

  void set_param_layout(const std::vector<ParamDesc>& layout);
  void set_param_blob(const std::vector<std::uint8_t>& blob);
  std::optional<std::vector<std::uint8_t>> read_param_symbol(const std::string& name, std::uint64_t size) const;

private:
  struct Allocation final {
    std::uint64_t base = 0;
    std::uint64_t size = 0;
  };

  std::uint64_t next_global_addr_ = 0x1000;
  std::vector<Allocation> allocs_;
  std::unordered_map<std::uint64_t, std::uint8_t> global_;

  std::unordered_map<std::string, ParamDesc> param_layout_;
  std::vector<std::uint8_t> param_blob_;
};

} // namespace gpusim
