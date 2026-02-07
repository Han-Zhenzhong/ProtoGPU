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
        "deterministic": true
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

    EXPECT_TRUE(cfg.obs.enabled);
    EXPECT_EQ(cfg.obs.trace_capacity, static_cast<std::uint64_t>(99));
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
  }

  if (g_failures) {
    std::cerr << "FAILURES: " << g_failures << "\n";
    return 1;
  }
  std::cout << "OK\n";
  return 0;
}
