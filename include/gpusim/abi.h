#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gpusim {

using DevicePtr = std::uint64_t;
using HostBufId = std::uint64_t;

enum class ParamType : std::uint8_t {
  U32,
  U64,
};

struct ParamDesc final {
  std::string name;
  ParamType type = ParamType::U32;
  std::uint32_t size = 0;
  std::uint32_t align = 0;
  std::uint32_t offset = 0;
};

struct KernelArgs final {
  std::vector<ParamDesc> layout;
  std::vector<std::uint8_t> blob;
};

} // namespace gpusim
