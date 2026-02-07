#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gpusim {

class IWarpScheduler {
public:
  virtual ~IWarpScheduler() = default;

  virtual void on_cta_start(std::uint32_t warp_count) = 0;

  // Returns a warp index in [0, warp_count). If all warps are done, returns nullopt.
  virtual std::optional<std::uint32_t> pick_next(std::uint32_t warp_count,
                                                 const std::function<bool(std::uint32_t)>& is_warp_done) = 0;
};

std::unique_ptr<IWarpScheduler> create_warp_scheduler(const std::string& name);
std::vector<std::string> list_warp_schedulers();

} // namespace gpusim
