#include "gpusim/runtime.h"

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

static std::vector<std::uint8_t> pack_u64_le(std::uint64_t v) {
  std::vector<std::uint8_t> out(8, 0);
  for (int b = 0; b < 8; b++) {
    out[static_cast<std::size_t>(b)] = static_cast<std::uint8_t>((v >> (8 * b)) & 0xFFu);
  }
  return out;
}

static std::uint32_t unpack_u32_le(const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() < 4) return 0;
  return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
         (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

void test_ret_only_kernel_in_memory() {
  gpusim::AppConfig cfg;
  cfg.sim.warp_size = 1;

  gpusim::Runtime rt(cfg);

  const std::string ptx = R"PTX(
.version 6.4
.target sm_70
.address_size 64

.visible .entry k()
{
  ret;
}
)PTX";

  const std::string ptx_isa = R"JSON(
{
  "insts": [
    {"ptx_opcode":"ret","type_mod":"","operand_kinds":[],"ir_op":"ret"}
  ]
}
)JSON";

  const std::string inst_desc = R"JSON(
{
  "insts": [
    {
      "opcode": "ret",
      "type_mod": "",
      "operand_kinds": [],
      "uops": [ { "kind": "CTRL", "op": "RET", "in": [], "out": [] } ]
    }
  ]
}
)JSON";

  auto out = rt.run_ptx_kernel_text(ptx, ptx_isa, inst_desc);
  if (out.sim.diag) {
    std::cerr << "diag(module=" << out.sim.diag->module << ", code=" << out.sim.diag->code
              << ", msg=" << out.sim.diag->message << ")\n";
  }
  EXPECT_TRUE(out.sim.completed);
  EXPECT_TRUE(!out.sim.diag.has_value());
}

void test_with_args_in_memory_can_write_global() {
  gpusim::AppConfig cfg;
  cfg.sim.warp_size = 1;

  gpusim::Runtime rt(cfg);

  const std::string ptx = R"PTX(
.version 6.4
.target sm_70
.address_size 64

.visible .entry write_u32(
    .param .u64 out_ptr
)
{
  .reg .u64 %rd<2>;
  .reg .u32 %r<1>;

  ld.param.u64 %rd1, [out_ptr];
  mov.u32 %r0, 42;
  st.global.u32 [%rd1], %r0;
  ret;
}
)PTX";

  const std::string ptx_isa = R"JSON(
{
  "insts": [
    {"ptx_opcode":"ld","type_mod":"u64","operand_kinds":["reg","symbol"],"ir_op":"ld"},
    {"ptx_opcode":"mov","type_mod":"u32","operand_kinds":["reg","imm"],"ir_op":"mov"},
    {"ptx_opcode":"st","type_mod":"u32","operand_kinds":["addr","reg"],"ir_op":"st"},
    {"ptx_opcode":"ret","type_mod":"","operand_kinds":[],"ir_op":"ret"}
  ]
}
)JSON";

  const std::string inst_desc = R"JSON(
{
  "insts": [
    {
      "opcode": "ld",
      "type_mod": "u64",
      "operand_kinds": ["reg", "symbol"],
      "uops": [ { "kind": "MEM", "op": "LD", "in": [1], "out": [0] } ]
    },
    {
      "opcode": "mov",
      "type_mod": "u32",
      "operand_kinds": ["reg", "imm"],
      "uops": [ { "kind": "EXEC", "op": "MOV", "in": [1], "out": [0] } ]
    },
    {
      "opcode": "st",
      "type_mod": "u32",
      "operand_kinds": ["addr", "reg"],
      "uops": [ { "kind": "MEM", "op": "ST", "in": [0, 1], "out": [] } ]
    },
    {
      "opcode": "ret",
      "type_mod": "",
      "operand_kinds": [],
      "uops": [ { "kind": "CTRL", "op": "RET", "in": [], "out": [] } ]
    }
  ]
}
)JSON";

  const auto dev_out = rt.device_malloc(/*bytes=*/4, /*align=*/4);
  const auto host_out = rt.host_alloc(/*bytes=*/4);

  gpusim::KernelArgs ka;
  ka.blob = pack_u64_le(dev_out);

  auto out = rt.run_ptx_kernel_with_args_text(ptx, ptx_isa, inst_desc, ka);
  if (out.sim.diag) {
    std::cerr << "diag(module=" << out.sim.diag->module << ", code=" << out.sim.diag->code
              << ", msg=" << out.sim.diag->message << ")\n";
  }
  EXPECT_TRUE(out.sim.completed);
  EXPECT_TRUE(!out.sim.diag.has_value());

  if (out.sim.completed) {
    rt.memcpy_d2h(host_out, 0, dev_out, 4);
    const auto bytes = rt.host_read(host_out, 0, 4);
    EXPECT_TRUE(bytes.has_value());
    if (bytes) {
      EXPECT_EQ(unpack_u32_le(*bytes), static_cast<std::uint32_t>(42));
    }
  }
}

} // namespace

int main() {
  test_ret_only_kernel_in_memory();
  test_with_args_in_memory_can_write_global();

  if (g_failures) {
    std::cerr << "FAILURES: " << g_failures << "\n";
    return 1;
  }
  std::cout << "OK\n";
  return 0;
}
