#pragma once

#include "gpusim/runtime.h"
#include "cudart_shim/assets_provider.h"

#include <mutex>
#include <optional>
#include <string>

#include <cstdint>

namespace gpusim_cudart_shim {

class RuntimeContext final {
public:
  static RuntimeContext& instance();

  // Ensures runtime is initialized. On failure, latches fatal error.
  bool ensure_initialized(std::string& out_error);

  bool has_fatal_error() const;
  const std::string& fatal_error() const { return fatal_error_; }

  gpusim::Runtime& runtime();
  const AssetsText& assets() const { return assets_; }

  std::uint32_t warp_size() const { return warp_size_; }
  const std::string& profile() const { return profile_; }
  bool deterministic() const { return deterministic_; }

  std::mutex& global_mutex() { return mu_; }

private:
  RuntimeContext() = default;

  mutable std::mutex mu_;
  bool initialized_ = false;
  std::string fatal_error_;

  AssetsText assets_;
  std::optional<gpusim::Runtime> rt_;
  std::uint32_t warp_size_ = 32;
  std::string profile_;
  bool deterministic_ = false;
};

} // namespace gpusim_cudart_shim
