#include "gpusim/json.h"
#include "gpusim/observability.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

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

static std::string strip_trailing_newline(std::string s) {
  if (!s.empty() && s.back() == '\n') s.pop_back();
  return s;
}

void test_trace_header_line_shape() {
  gpusim::TraceHeader h;
  h.format_version = 1;
  h.schema = "gpusim_trace_jsonl_v1";
  h.profile = "baseline_no_cache";
  h.deterministic = true;

  const std::string line = strip_trailing_newline(gpusim::trace_header_to_json_line(h));
  const auto v = gpusim::json::parse(line);
  const auto& o = v.as_object();

  EXPECT_EQ(o.at("action").as_string(), std::string("TRACE_HEADER"));
  EXPECT_EQ(o.at("format_version").as_i64(), std::int64_t(1));
  EXPECT_EQ(o.at("schema").as_string(), std::string("gpusim_trace_jsonl_v1"));
  EXPECT_EQ(o.at("profile").as_string(), std::string("baseline_no_cache"));
  EXPECT_TRUE(o.at("deterministic").as_bool() == true);
}

void test_stats_json_shape_and_meta() {
  std::vector<std::pair<std::string, std::uint64_t>> counters;
  counters.push_back({"inst.fetch", 7});
  counters.push_back({"uop.mem.st.global", 2});

  gpusim::StatsMeta meta;
  meta.format_version = 1;
  meta.schema = "gpusim_stats_v1";
  meta.profile = "baseline_no_cache";
  meta.deterministic = false;

  const std::string s = gpusim::stats_to_json(counters, meta);
  const auto v = gpusim::json::parse(s);
  const auto& o = v.as_object();

  EXPECT_EQ(o.at("format_version").as_i64(), std::int64_t(1));
  EXPECT_EQ(o.at("schema").as_string(), std::string("gpusim_stats_v1"));
  EXPECT_EQ(o.at("profile").as_string(), std::string("baseline_no_cache"));
  EXPECT_TRUE(o.at("deterministic").as_bool() == false);

  const auto& cc = o.at("counters").as_object();
  EXPECT_TRUE(cc.at("inst.fetch").as_i64() == std::int64_t(7));
  EXPECT_TRUE(cc.at("uop.mem.st.global").as_i64() == std::int64_t(2));
}

} // namespace

int main() {
  test_trace_header_line_shape();
  test_stats_json_shape_and_meta();

  if (g_failures) {
    std::cerr << "FAILURES: " << g_failures << "\n";
    return 1;
  }
  std::cout << "OK\n";
  return 0;
}
