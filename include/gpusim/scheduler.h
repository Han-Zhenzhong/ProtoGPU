#pragma once

#include "gpusim/contracts.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace gpusim {

struct CtaWorkItem final {
  std::uint32_t cx = 0;
  std::uint32_t cy = 0;
  std::uint32_t cz = 0;
  std::uint64_t cta_linear = 0;
  CTAId cta_id = 0;
};

class ICtaScheduler {
public:
  virtual ~ICtaScheduler() = default;

  virtual void submit(CtaWorkItem item) = 0;
  virtual void close() = 0;

  virtual void request_stop() = 0;
  virtual bool stop_requested() const = 0;

  virtual std::optional<CtaWorkItem> acquire(SmId sm_id) = 0;
};

std::unique_ptr<ICtaScheduler> create_cta_scheduler(const std::string& name, std::uint32_t sm_count);
std::vector<std::string> list_cta_schedulers();

} // namespace gpusim
