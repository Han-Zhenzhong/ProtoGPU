#include "cudart_shim/fatbin_registry.h"
#include "cudart_shim/ptx_entry_validate.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

#define EXPECT_CONTAINS(haystack, needle) \
  do { \
    const std::string _h = (haystack); \
    const std::string _n = (needle); \
    if (_h.find(_n) == std::string::npos) { \
      fail(std::string("EXPECT_CONTAINS failed: missing '") + _n + "' in '" + _h + "'", __FILE__, __LINE__); \
    } \
  } while (0)

constexpr const char* kEnvName = "GPUSIM_CUDART_SHIM_PTX_OVERRIDE";

class EnvVarGuard final {
public:
  explicit EnvVarGuard(const char* name) : name_(name) {
    const char* existing = std::getenv(name_);
    if (existing) {
      had_old_ = true;
      old_value_ = existing;
    }
  }

  ~EnvVarGuard() {
    if (had_old_) {
      set(old_value_);
    } else {
      clear();
    }
  }

  void set(const std::string& value) {
#if defined(_WIN32)
    _putenv_s(name_, value.c_str());
#else
    ::setenv(name_, value.c_str(), 1);
#endif
  }

  void clear() {
#if defined(_WIN32)
    _putenv_s(name_, "");
#else
    ::unsetenv(name_);
#endif
  }

private:
  const char* name_;
  bool had_old_ = false;
  std::string old_value_;
};

class TempDir final {
public:
  TempDir() {
    auto base = std::filesystem::temp_directory_path() / "gpusim_ptx_override_tests";
    std::filesystem::create_directories(base);
    for (int attempt = 0; attempt < 32; attempt++) {
      const auto candidate = base / ("case-" + std::to_string(std::rand()) + "-" + std::to_string(std::rand()));
      std::error_code ec;
      if (std::filesystem::create_directory(candidate, ec)) {
        path_ = candidate;
        break;
      }
    }
    if (path_.empty()) {
      throw std::runtime_error("failed to create temp directory for PTX override tests");
    }
  }

  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }

  const std::filesystem::path& path() const { return path_; }

private:
  std::filesystem::path path_;
};

std::string write_text_file(const std::filesystem::path& dir,
                            const std::string& name,
                            const std::string& text) {
  const auto path = dir / name;
  std::ofstream out(path, std::ios::binary);
  out << text;
  return path.string();
}

const gpusim_cudart_shim::FatbinModule* register_with_override(
    gpusim_cudart_shim::FatbinRegistry& registry,
    const std::string& override_value,
    void*** out_handle) {
  EnvVarGuard env(kEnvName);
  env.set(override_value);
  int dummy = 7;
  auto** handle = registry.register_fatbin(&dummy);
  *out_handle = handle;
  return registry.lookup_by_handle(handle);
}

const std::string kPtxHeader = R"PTX(.version 6.4
.target sm_70
.address_size 64

)PTX";

void test_multi_path_override_loads_in_order_and_trims_whitespace() {
  using namespace gpusim_cudart_shim;

  TempDir temp;
  const auto ptx_a = write_text_file(temp.path(), "a.ptx", kPtxHeader + R"PTX(.visible .entry only_a() { ret; }
)PTX");
  const auto ptx_b = write_text_file(temp.path(), "b.ptx", kPtxHeader + R"PTX(.visible .entry only_b() { ret; }
)PTX");

  FatbinRegistry registry;
  void** handle = nullptr;
  {
    EnvVarGuard env(kEnvName);
    env.set("  " + ptx_a + "  :  " + ptx_b + "  ");
    int dummy = 7;
    handle = registry.register_fatbin(&dummy);
  }

  const auto* mod = registry.lookup_by_handle(handle);
  EXPECT_TRUE(mod != nullptr);
  EXPECT_TRUE(mod->ptx_override_active);
  EXPECT_TRUE(mod->ptx_override_error.empty());
  EXPECT_EQ(mod->ptx_texts.size(), static_cast<std::size_t>(2));
  EXPECT_TRUE(ptx_contains_entry_mvp(mod->ptx_texts[0], "only_a"));
  EXPECT_TRUE(ptx_contains_entry_mvp(mod->ptx_texts[1], "only_b"));

  registry.unregister_fatbin(handle);
}

void test_duplicate_entry_resolution_uses_first_match_order() {
  using namespace gpusim_cudart_shim;

  TempDir temp;
  const auto ptx_first = write_text_file(temp.path(), "first.ptx", kPtxHeader + R"PTX(// first-copy
.visible .entry dup_entry() { ret; }
)PTX");
  const auto ptx_second = write_text_file(temp.path(), "second.ptx", kPtxHeader + R"PTX(// second-copy
.visible .entry dup_entry() { ret; }
)PTX");

  FatbinRegistry registry;
  void** handle = nullptr;
  {
    EnvVarGuard env(kEnvName);
    env.set(ptx_first + ":" + ptx_second);
    int dummy = 9;
    handle = registry.register_fatbin(&dummy);
  }

  const auto* mod = registry.lookup_by_handle(handle);
  EXPECT_TRUE(mod != nullptr);
  EXPECT_EQ(mod->ptx_texts.size(), static_cast<std::size_t>(2));

  const std::string* chosen = nullptr;
  for (const auto& ptx : mod->ptx_texts) {
    if (ptx_contains_entry_mvp(ptx, "dup_entry")) {
      chosen = &ptx;
      break;
    }
  }
  EXPECT_TRUE(chosen != nullptr);
  EXPECT_CONTAINS(*chosen, "first-copy");

  registry.unregister_fatbin(handle);
}

void test_invalid_override_is_authoritative_and_reports_error() {
  using namespace gpusim_cudart_shim;

  TempDir temp;
  const auto valid_ptx = write_text_file(temp.path(), "valid.ptx", kPtxHeader + R"PTX(.visible .entry ok() { ret; }
)PTX");
  const auto missing_path = (temp.path() / "missing.ptx").string();

  FatbinRegistry registry;
  void** handle = nullptr;
  {
    EnvVarGuard env(kEnvName);
    env.set(valid_ptx + ":" + missing_path);
    int dummy = 11;
    handle = registry.register_fatbin(&dummy);
  }

  const auto* mod = registry.lookup_by_handle(handle);
  EXPECT_TRUE(mod != nullptr);
  EXPECT_TRUE(mod->ptx_override_active);
  EXPECT_TRUE(mod->ptx_texts.empty());
  EXPECT_CONTAINS(mod->ptx_override_error, "GPUSIM_CUDART_SHIM_PTX_OVERRIDE invalid");
  EXPECT_CONTAINS(mod->ptx_override_error, missing_path);
  EXPECT_CONTAINS(mod->ptx_override_error, "cannot open file");

  registry.unregister_fatbin(handle);
}

void test_empty_path_element_is_rejected() {
  using namespace gpusim_cudart_shim;

  TempDir temp;
  const auto ptx_a = write_text_file(temp.path(), "a.ptx", kPtxHeader + R"PTX(.visible .entry a() { ret; }
)PTX");
  const auto ptx_b = write_text_file(temp.path(), "b.ptx", kPtxHeader + R"PTX(.visible .entry b() { ret; }
)PTX");

  FatbinRegistry registry;
  void** handle = nullptr;
  {
    EnvVarGuard env(kEnvName);
    env.set(ptx_a + "::" + ptx_b);
    int dummy = 13;
    handle = registry.register_fatbin(&dummy);
  }

  const auto* mod = registry.lookup_by_handle(handle);
  EXPECT_TRUE(mod != nullptr);
  EXPECT_TRUE(mod->ptx_override_active);
  EXPECT_TRUE(mod->ptx_texts.empty());
  EXPECT_CONTAINS(mod->ptx_override_error, "empty path element");

  registry.unregister_fatbin(handle);
}

} // namespace

int main() {
  test_multi_path_override_loads_in_order_and_trims_whitespace();
  test_duplicate_entry_resolution_uses_first_match_order();
  test_invalid_override_is_authoritative_and_reports_error();
  test_empty_path_element_is_rejected();

  if (g_failures) {
    std::cerr << "FAILURES: " << g_failures << "\n";
    return 1;
  }
  std::cout << "OK\n";
  return 0;
}