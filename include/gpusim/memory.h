#pragma once

#include "gpusim/abi.h"
#include "gpusim/contracts.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace gpusim {

class IMemoryModel {
public:
  virtual ~IMemoryModel() = default;

  virtual DevicePtr alloc_global(std::uint64_t bytes, std::uint64_t align = 16) = 0;
  virtual void write_global(std::uint64_t addr, const std::vector<std::uint8_t>& bytes) = 0;
  virtual std::optional<std::vector<std::uint8_t>> read_global(std::uint64_t addr, std::uint64_t size) const = 0;

  virtual void set_param_layout(const std::vector<ParamDesc>& layout) = 0;
  virtual void set_param_blob(const std::vector<std::uint8_t>& blob) = 0;
  virtual std::optional<std::vector<std::uint8_t>> read_param_symbol(const std::string& name, std::uint64_t size) const = 0;
};

class AddrSpaceManager final : public IMemoryModel {
public:
  DevicePtr alloc_global(std::uint64_t bytes, std::uint64_t align = 16) override;
  void write_global(std::uint64_t addr, const std::vector<std::uint8_t>& bytes) override;
  std::optional<std::vector<std::uint8_t>> read_global(std::uint64_t addr, std::uint64_t size) const override;

  void set_param_layout(const std::vector<ParamDesc>& layout) override;
  void set_param_blob(const std::vector<std::uint8_t>& blob) override;
  std::optional<std::vector<std::uint8_t>> read_param_symbol(const std::string& name, std::uint64_t size) const override;

private:
  mutable std::mutex mu_;

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
