#include "gpusim/runtime.h"

#include <cstdint>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void fail(const std::string& msg, const char* file, int line) {
  g_failures++;
  std::cerr << file << ":" << line << ": " << msg << "\n";
}

#define EXPECT_TRUE(cond) \
  do { \
    if (!(cond)) fail(std::string("EXPECT_TRUE failed: ") + #cond, __FILE__, __LINE__); \
  } while (0)

#define EXPECT_EQ(a, b) \
  do { \
    const auto& _a = (a); \
    const auto& _b = (b); \
    if (!(_a == _b)) { \
      fail(std::string("EXPECT_EQ failed: ") + #a + " != " + #b, __FILE__, __LINE__); \
    } \
  } while (0)

} // namespace

int main() {
  using namespace gpusim;

  {
    const std::string json = R"JSON(
    {
      "sim": {
        "warp_size": 16,
        "max_steps": 1234,
        "sm_count": 4,
        "parallel": true,
        "deterministic": true,
        "cta_scheduler": "sm_round_robin",
        "warp_scheduler": "round_robin_interleave_step",
        "allow_unknown_selectors": true
      },
      "memory": {
        "model": "no_cache_addrspace"
      },
      "observability": {
        "enabled": true,
        "trace_capacity": 99
      }
    }
    )JSON";

    auto cfg = load_app_config_json_text(json);
    EXPECT_EQ(cfg.sim.warp_size, static_cast<std::uint32_t>(16));
    EXPECT_EQ(cfg.sim.max_steps, static_cast<std::uint64_t>(1234));
    EXPECT_EQ(cfg.sim.sm_count, static_cast<std::uint32_t>(4));
    EXPECT_TRUE(cfg.sim.parallel);
    EXPECT_TRUE(cfg.sim.deterministic);

    EXPECT_EQ(cfg.sim.cta_scheduler, std::string("sm_round_robin"));
    EXPECT_EQ(cfg.sim.warp_scheduler, std::string("round_robin_interleave_step"));
    EXPECT_EQ(cfg.sim.memory_model, std::string("no_cache_addrspace"));
    EXPECT_TRUE(cfg.sim.allow_unknown_selectors);

    EXPECT_TRUE(cfg.obs.enabled);
    EXPECT_EQ(cfg.obs.trace_capacity, static_cast<std::uint64_t>(99));
  }

  {
    // arch.profile/components should override sim.* selectors.
    const std::string json = R"JSON(
    {
      "sim": {
        "cta_scheduler": "fifo",
        "warp_scheduler": "in_order_run_to_completion"
      },
      "arch": {
        "profile": "baseline_no_cache",
        "components": {
          "cta_scheduler": "sm_round_robin",
          "warp_scheduler": "round_robin_interleave_step",
          "memory_model": "no_cache_addrspace"
        }
      }
    }
    )JSON";

    auto cfg = load_app_config_json_text(json);
    EXPECT_EQ(cfg.sim.profile, std::string("baseline_no_cache"));
    EXPECT_EQ(cfg.sim.cta_scheduler, std::string("sm_round_robin"));
    EXPECT_EQ(cfg.sim.warp_scheduler, std::string("round_robin_interleave_step"));
    EXPECT_EQ(cfg.sim.memory_model, std::string("no_cache_addrspace"));
  }

  {
    // Missing sim.* fields should keep defaults from SimConfig.
    const std::string json = R"JSON(
    {
      "sim": { },
      "observability": { }
    }
    )JSON";

    auto cfg = load_app_config_json_text(json);
    EXPECT_EQ(cfg.sim.warp_size, static_cast<std::uint32_t>(32));
    EXPECT_EQ(cfg.sim.max_steps, static_cast<std::uint64_t>(100000));
    EXPECT_EQ(cfg.sim.sm_count, static_cast<std::uint32_t>(1));
    EXPECT_TRUE(!cfg.sim.parallel);
    EXPECT_TRUE(!cfg.sim.deterministic);

    EXPECT_EQ(cfg.sim.profile, std::string(""));
    EXPECT_EQ(cfg.sim.cta_scheduler, std::string("fifo"));
    EXPECT_EQ(cfg.sim.warp_scheduler, std::string("in_order_run_to_completion"));
    EXPECT_EQ(cfg.sim.memory_model, std::string("no_cache_addrspace"));
    EXPECT_TRUE(!cfg.sim.allow_unknown_selectors);
  }

  if (g_failures) {
    std::cerr << "FAILURES: " << g_failures << "\n";
    return 1;
  }
  std::cout << "OK\n";
  return 0;
}
