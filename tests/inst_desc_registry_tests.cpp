#include "gpusim/instruction_desc.h"

#include <functional>
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

#define EXPECT_CONTAINS(haystack, needle) \
  do { \
    const std::string _h = (haystack); \
    const std::string _n = (needle); \
    if (_h.find(_n) == std::string::npos) { \
      fail(std::string("EXPECT_CONTAINS failed: missing '") + _n + "' in '" + _h + "'", __FILE__, __LINE__); \
    } \
  } while (0)

std::string must_throw_message(const std::function<void()>& f) {
  try {
    f();
  } catch (const std::exception& e) {
    return e.what();
  }
  fail("expected exception, but none was thrown", __FILE__, __LINE__);
  return "";
}

void test_non_strict_allows_unknown_keys() {
  using namespace gpusim;

  const std::string json = R"JSON(
  {
    "insts": [
      {
        "opcode": "mov",
        "type_mod": "u32",
        "operand_kinds": ["reg", "imm"],
        "uops": [
          { "kind": "EXEC", "op": "MOV", "in": [1], "out": [0], "extra_uop_key": 123 }
        ],
        "extra_inst_key": "ok"
      }
    ],
    "extra_root_key": true
  }
  )JSON";

  DescriptorRegistry reg;
  bool threw = false;
  try {
    reg.load_json_text(json);
  } catch (...) {
    threw = true;
  }
  EXPECT_TRUE(!threw);
}

void test_strict_rejects_unknown_keys() {
  using namespace gpusim;

  const std::string json = R"JSON(
  {
    "insts": [
      {
        "opcode": "mov",
        "type_mod": "u32",
        "operand_kinds": ["reg", "imm"],
        "uops": [
          { "kind": "EXEC", "op": "MOV", "in": [1], "out": [0] }
        ],
        "extra_inst_key": "boom"
      }
    ]
  }
  )JSON";

  DescriptorRegistryLoadOptions opt;
  opt.strict_keys = true;

  DescriptorRegistry reg;
  const std::string msg = must_throw_message([&]() { reg.load_json_text(json, opt); });
  EXPECT_CONTAINS(msg, "DescriptorRegistry:");
  EXPECT_CONTAINS(msg, "unknown key");
  EXPECT_CONTAINS(msg, "insts[0]");
}

void test_strict_rejects_unknown_root_key() {
  using namespace gpusim;

  const std::string json = R"JSON(
  {
    "insts": [
      {
        "opcode": "mov",
        "type_mod": "u32",
        "operand_kinds": ["reg", "imm"],
        "uops": [
          { "kind": "EXEC", "op": "MOV", "in": [1], "out": [0] }
        ]
      }
    ],
    "extra_root_key": 1
  }
  )JSON";

  DescriptorRegistryLoadOptions opt;
  opt.strict_keys = true;

  DescriptorRegistry reg;
  const std::string msg = must_throw_message([&]() { reg.load_json_text(json, opt); });
  EXPECT_CONTAINS(msg, "unknown key");
  EXPECT_CONTAINS(msg, "root");
}

void test_unknown_uop_has_context_in_message() {
  using namespace gpusim;

  const std::string json = R"JSON(
  {
    "insts": [
      {
        "opcode": "mov",
        "type_mod": "u32",
        "operand_kinds": ["reg", "imm"],
        "uops": [
          { "kind": "EXEC", "op": "__BOGUS_OP__", "in": [1], "out": [0] }
        ]
      }
    ]
  }
  )JSON";

  DescriptorRegistry reg;
  const std::string msg = must_throw_message([&]() { reg.load_json_text(json); });
  EXPECT_CONTAINS(msg, "insts[0]");
  EXPECT_CONTAINS(msg, "uops[0]");
  EXPECT_CONTAINS(msg, "unknown uop op");
}

} // namespace

int main() {
  test_non_strict_allows_unknown_keys();
  test_strict_rejects_unknown_keys();
  test_strict_rejects_unknown_root_key();
  test_unknown_uop_has_context_in_message();

  if (g_failures) {
    std::cerr << "FAILURES: " << g_failures << "\n";
    return 1;
  }
  std::cout << "OK\n";
  return 0;
}
