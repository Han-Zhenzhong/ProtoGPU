#include "gpusim/simt_scheduler.h"

namespace gpusim {

namespace {

class InOrderRunToCompletion final : public IWarpScheduler {
public:
  void on_cta_start(std::uint32_t /*warp_count*/) override { current_ = 0; }

  std::optional<std::uint32_t> pick_next(std::uint32_t warp_count,
                                         const std::function<bool(std::uint32_t)>& is_warp_done) override {
    if (warp_count == 0) return std::nullopt;

    // If current warp not done, keep issuing it.
    if (current_ < warp_count && !is_warp_done(current_)) return current_;

    // Otherwise advance to the next not-done warp.
    for (std::uint32_t i = current_; i < warp_count; i++) {
      if (!is_warp_done(i)) {
        current_ = i;
        return current_;
      }
    }
    return std::nullopt;
  }

private:
  std::uint32_t current_ = 0;
};

class RoundRobinInterleaveStep final : public IWarpScheduler {
public:
  void on_cta_start(std::uint32_t /*warp_count*/) override { next_ = 0; }

  std::optional<std::uint32_t> pick_next(std::uint32_t warp_count,
                                         const std::function<bool(std::uint32_t)>& is_warp_done) override {
    if (warp_count == 0) return std::nullopt;

    // Try up to warp_count picks to find a live warp.
    for (std::uint32_t k = 0; k < warp_count; k++) {
      const auto idx = (next_ + k) % warp_count;
      if (!is_warp_done(idx)) {
        next_ = (idx + 1) % warp_count;
        return idx;
      }
    }
    return std::nullopt;
  }

private:
  std::uint32_t next_ = 0;
};

} // namespace

std::unique_ptr<IWarpScheduler> create_warp_scheduler(const std::string& name) {
  if (name == "in_order_run_to_completion") return std::make_unique<InOrderRunToCompletion>();
  if (name == "round_robin_interleave_step") return std::make_unique<RoundRobinInterleaveStep>();
  return nullptr;
}

std::vector<std::string> list_warp_schedulers() { return { "in_order_run_to_completion", "round_robin_interleave_step" }; }

} // namespace gpusim
