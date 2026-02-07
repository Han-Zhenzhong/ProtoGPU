#include "gpusim/scheduler.h"

#include <condition_variable>

namespace gpusim {

namespace {

class FifoCtaScheduler final : public ICtaScheduler {
public:
  explicit FifoCtaScheduler(std::uint32_t /*sm_count*/) {}

  void submit(CtaWorkItem item) override {
    {
      std::lock_guard<std::mutex> lock(mu_);
      q_.push_back(std::move(item));
    }
    cv_.notify_one();
  }

  void close() override {
    {
      std::lock_guard<std::mutex> lock(mu_);
      closed_ = true;
    }
    cv_.notify_all();
  }

  void request_stop() override {
    stop_.store(true, std::memory_order_relaxed);
    cv_.notify_all();
  }

  bool stop_requested() const override { return stop_.load(std::memory_order_relaxed); }

  std::optional<CtaWorkItem> acquire(SmId /*sm_id*/) override {
    std::unique_lock<std::mutex> lock(mu_);
    cv_.wait(lock, [&]() { return stop_.load(std::memory_order_relaxed) || !q_.empty() || closed_; });
    if (stop_.load(std::memory_order_relaxed)) return std::nullopt;
    if (!q_.empty()) {
      auto item = q_.front();
      q_.pop_front();
      return item;
    }
    return std::nullopt;
  }

private:
  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::deque<CtaWorkItem> q_;
  bool closed_ = false;
  std::atomic<bool> stop_{ false };
};

// Deterministic partitioning policy: CTA i is assigned to SM (i % sm_count).
// This is intentionally "non-stealing" to make the policy difference observable.
class SmRoundRobinCtaScheduler final : public ICtaScheduler {
public:
  explicit SmRoundRobinCtaScheduler(std::uint32_t sm_count) : sm_count_(sm_count == 0 ? 1u : sm_count) {
    q_.resize(sm_count_);
  }

  void submit(CtaWorkItem item) override {
    const auto sm = static_cast<std::uint32_t>(item.cta_linear % sm_count_);
    {
      std::lock_guard<std::mutex> lock(mu_);
      q_[sm].push_back(std::move(item));
    }
    cv_.notify_all();
  }

  void close() override {
    {
      std::lock_guard<std::mutex> lock(mu_);
      closed_ = true;
    }
    cv_.notify_all();
  }

  void request_stop() override {
    stop_.store(true, std::memory_order_relaxed);
    cv_.notify_all();
  }

  bool stop_requested() const override { return stop_.load(std::memory_order_relaxed); }

  std::optional<CtaWorkItem> acquire(SmId sm_id) override {
    const auto sm = static_cast<std::uint32_t>(sm_id % sm_count_);
    std::unique_lock<std::mutex> lock(mu_);
    cv_.wait(lock, [&]() {
      return stop_.load(std::memory_order_relaxed) || !q_[sm].empty() || closed_;
    });
    if (stop_.load(std::memory_order_relaxed)) return std::nullopt;
    if (!q_[sm].empty()) {
      auto item = q_[sm].front();
      q_[sm].pop_front();
      return item;
    }
    return std::nullopt;
  }

private:
  std::uint32_t sm_count_ = 1;
  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::vector<std::deque<CtaWorkItem>> q_;
  bool closed_ = false;
  std::atomic<bool> stop_{ false };
};

} // namespace

std::unique_ptr<ICtaScheduler> create_cta_scheduler(const std::string& name, std::uint32_t sm_count) {
  if (name == "fifo") return std::make_unique<FifoCtaScheduler>(sm_count);
  if (name == "sm_round_robin") return std::make_unique<SmRoundRobinCtaScheduler>(sm_count);
  return nullptr;
}

std::vector<std::string> list_cta_schedulers() { return { "fifo", "sm_round_robin" }; }

} // namespace gpusim
