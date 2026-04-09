#include "gpusim/contracts.h"
#include "gpusim/frontend.h"
#include "gpusim/instruction_desc.h"
#include "gpusim/memory.h"
#include "gpusim/observability.h"
#include "gpusim/simt.h"

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

#define EXPECT_CONTAINS(haystack, needle) \
  do { \
    const std::string _h = (haystack); \
    const std::string _n = (needle); \
    if (_h.find(_n) == std::string::npos) { \
      fail(std::string("EXPECT_CONTAINS failed: missing '") + _n + "' in '" + _h + "'", __FILE__, __LINE__); \
    } \
  } while (0)

gpusim::KernelImage make_kernel(std::string name, std::uint32_t reg_u32, std::vector<gpusim::InstRecord> insts) {
  gpusim::KernelImage k;
  k.name = std::move(name);
  k.reg_u32_count = reg_u32;
  k.reg_u64_count = 0;
  k.insts = std::move(insts);
  return k;
}

gpusim::DescriptorRegistry make_registry_for_tests() {
  using namespace gpusim;

  // Minimal inst_desc JSON for memory tests:
  // - ld.* (reg,symbol) -> MEM LD (in=[1], out=[0])
  // - ld.* (reg,addr)   -> MEM LD (in=[1], out=[0])
  // - st.* (addr,imm)   -> MEM ST (in=[0,1])
  // - st.* (addr,special) -> MEM ST (in=[0,1])
  // - st_bad (symbol,imm) -> MEM ST (in=[0,1]) to force MemUnit kind check
  // - ret -> CTRL RET (no operands)
  const std::string desc_json = R"JSON(
  {
    "insts": [
      {
        "opcode": "ld",
        "type_mod": "u32",
        "operand_kinds": ["reg", "symbol"],
        "uops": [
          { "kind": "MEM", "op": "LD", "in": [1], "out": [0] }
        ]
      },
      {
        "opcode": "ld",
        "type_mod": "u32",
        "operand_kinds": ["reg", "addr"],
        "uops": [
          { "kind": "MEM", "op": "LD", "in": [1], "out": [0] }
        ]
      },
      {
        "opcode": "st",
        "type_mod": "u32",
        "operand_kinds": ["addr", "imm"],
        "uops": [
          { "kind": "MEM", "op": "ST", "in": [0, 1], "out": [] }
        ]
      },
      {
        "opcode": "st",
        "type_mod": "u32",
        "operand_kinds": ["addr", "special"],
        "uops": [
          { "kind": "MEM", "op": "ST", "in": [0, 1], "out": [] }
        ]
      },
      {
        "opcode": "st_bad",
        "type_mod": "u32",
        "operand_kinds": ["symbol", "imm"],
        "uops": [
          { "kind": "MEM", "op": "ST", "in": [0, 1], "out": [] }
        ]
      },
      {
        "opcode": "ret",
        "type_mod": "",
        "operand_kinds": [],
        "uops": [
          { "kind": "CTRL", "op": "RET", "in": [], "out": [] }
        ]
      }
    ]
  }
  )JSON";

  DescriptorRegistry reg;
  reg.load_json_text(desc_json);
  return reg;
}

gpusim::Operand imm(std::int64_t v) {
  gpusim::Operand o;
  o.kind = gpusim::OperandKind::Imm;
  o.imm_i64 = v;
  return o;
}

gpusim::Operand reg_u32(std::int64_t rid) {
  gpusim::Operand o;
  o.kind = gpusim::OperandKind::Reg;
  o.type = gpusim::ValueType::U32;
  o.reg_id = rid;
  return o;
}

gpusim::Operand symbol(std::string name) {
  gpusim::Operand o;
  o.kind = gpusim::OperandKind::Symbol;
  o.symbol = std::move(name);
  return o;
}

gpusim::Operand special(std::string name) {
  gpusim::Operand o;
  o.kind = gpusim::OperandKind::Special;
  o.special = std::move(name);
  return o;
}

gpusim::Operand addr_imm(std::uint64_t addr) {
  gpusim::Operand o;
  o.kind = gpusim::OperandKind::Addr;
  o.type = gpusim::ValueType::U64;
  o.reg_id = -1;           // no base reg
  o.imm_i64 = (std::int64_t)addr; // absolute address in imm offset
  return o;
}

std::string must_run_start_extra_json(gpusim::ObsControl& obs) {
  auto events = obs.trace_snapshot();
  for (const auto& e : events) {
    if (e.action == "RUN_START" && e.extra_json.has_value()) {
      return *e.extra_json;
    }
  }
  fail("expected RUN_START extra_json, but not found", __FILE__, __LINE__);
  return "";
}

void test_unknown_memory_model_is_fail_fast() {
  using namespace gpusim;

  auto registry = make_registry_for_tests();
  ObsControl obs(ObsConfig{ false, 0 });
  AddrSpaceManager mem;

  SimConfig cfg;
  cfg.warp_size = 1;
  cfg.memory_model = "__bogus_mm__";
  cfg.allow_unknown_selectors = false;

  auto kernel = make_kernel("k_mm", /*reg_u32=*/0, {});

  SimtExecutor sim(cfg, std::move(registry), obs, mem);
  auto out = sim.run(kernel);

  EXPECT_TRUE(!out.completed);
  EXPECT_TRUE(out.diag.has_value());
  if (out.diag) {
    EXPECT_EQ(out.diag->module, std::string("simt"));
    EXPECT_EQ(out.diag->code, std::string("E_MEMORY_MODEL"));
  }
}

void test_allow_unknown_memory_model_continues() {
  using namespace gpusim;

  auto registry = make_registry_for_tests();
  ObsControl obs(ObsConfig{ true, 1024 });
  AddrSpaceManager mem;

  SimConfig cfg;
  cfg.warp_size = 1;
  cfg.memory_model = "__bogus_mm__";
  cfg.allow_unknown_selectors = true;
  cfg.sm_count = 2;
  cfg.parallel = true;

  auto kernel = make_kernel("k_mm_allow", /*reg_u32=*/0, {});

  SimtExecutor sim(cfg, std::move(registry), obs, mem);
  auto out = sim.run(kernel);

  EXPECT_TRUE(!out.completed);
  EXPECT_TRUE(out.diag.has_value());
  if (out.diag) {
    EXPECT_TRUE(out.diag->code != std::string("E_MEMORY_MODEL"));
  }

  const std::string extra = must_run_start_extra_json(obs);
  EXPECT_CONTAINS(extra, "\"memory_model\"");
  EXPECT_CONTAINS(extra, "__bogus_mm__");
}

void test_ld_param_kind_error() {
  using namespace gpusim;

  auto registry = make_registry_for_tests();
  ObsControl obs(ObsConfig{ false, 0 });
  AddrSpaceManager mem;

  SimConfig cfg;
  cfg.warp_size = 1;

  InstRecord inst;
  inst.opcode = "ld";
  inst.mods.type_mod = "u32";
  inst.mods.space = "param";
  inst.operands = { reg_u32(0), addr_imm(0) }; // wrong kind for param source (expects symbol)

  auto kernel = make_kernel("k_ldp_kind", /*reg_u32=*/2, { inst });

  SimtExecutor sim(cfg, std::move(registry), obs, mem);
  auto out = sim.run(kernel);

  EXPECT_TRUE(!out.completed);
  EXPECT_TRUE(out.diag.has_value());
  if (out.diag) {
    EXPECT_EQ(out.diag->module, std::string("units.mem"));
    EXPECT_EQ(out.diag->code, std::string("E_LD_PARAM_KIND"));
  }
}

void test_ld_param_miss_error() {
  using namespace gpusim;

  auto registry = make_registry_for_tests();
  ObsControl obs(ObsConfig{ false, 0 });
  AddrSpaceManager mem;

  SimConfig cfg;
  cfg.warp_size = 1;

  InstRecord inst;
  inst.opcode = "ld";
  inst.mods.type_mod = "u32";
  inst.mods.space = "param";
  inst.operands = { reg_u32(0), symbol("missing_param") };

  auto kernel = make_kernel("k_ldp_miss", /*reg_u32=*/1, { inst });

  SimtExecutor sim(cfg, std::move(registry), obs, mem);
  auto out = sim.run(kernel);

  EXPECT_TRUE(!out.completed);
  EXPECT_TRUE(out.diag.has_value());
  if (out.diag) {
    EXPECT_EQ(out.diag->module, std::string("units.mem"));
    EXPECT_EQ(out.diag->code, std::string("E_PARAM_MISS"));
  }
}

void test_ld_global_kind_error() {
  using namespace gpusim;

  auto registry = make_registry_for_tests();
  ObsControl obs(ObsConfig{ false, 0 });
  AddrSpaceManager mem;

  SimConfig cfg;
  cfg.warp_size = 1;

  InstRecord inst;
  inst.opcode = "ld";
  inst.mods.type_mod = "u32";
  inst.mods.space = "global";
  inst.operands = { reg_u32(0), symbol("not_addr") };

  auto kernel = make_kernel("k_ldg_kind", /*reg_u32=*/1, { inst });

  SimtExecutor sim(cfg, std::move(registry), obs, mem);
  auto out = sim.run(kernel);

  EXPECT_TRUE(!out.completed);
  EXPECT_TRUE(out.diag.has_value());
  if (out.diag) {
    EXPECT_EQ(out.diag->module, std::string("units.mem"));
    EXPECT_EQ(out.diag->code, std::string("E_LD_GLOBAL_KIND"));
  }
}

void test_st_global_kind_error() {
  using namespace gpusim;

  auto registry = make_registry_for_tests();
  ObsControl obs(ObsConfig{ false, 0 });
  AddrSpaceManager mem;

  SimConfig cfg;
  cfg.warp_size = 1;

  InstRecord inst;
  inst.opcode = "st_bad";
  inst.mods.type_mod = "u32";
  inst.mods.space = "global";
  inst.operands = { symbol("not_addr"), imm(1) };

  auto kernel = make_kernel("k_stg_kind", /*reg_u32=*/0, { inst });

  SimtExecutor sim(cfg, std::move(registry), obs, mem);
  auto out = sim.run(kernel);

  EXPECT_TRUE(!out.completed);
  EXPECT_TRUE(out.diag.has_value());
  if (out.diag) {
    EXPECT_EQ(out.diag->module, std::string("units.mem"));
    EXPECT_EQ(out.diag->code, std::string("E_ST_GLOBAL_KIND"));
  }
}

void test_deterministic_disables_parallel_in_run_start() {
  using namespace gpusim;

  auto registry = make_registry_for_tests();
  ObsControl obs(ObsConfig{ true, 4096 });
  AddrSpaceManager mem;

  SimConfig cfg;
  cfg.warp_size = 1;
  cfg.parallel = true;
  cfg.deterministic = true;
  cfg.sm_count = 4;

  auto kernel = make_kernel("k_det", /*reg_u32=*/0, {});

  SimtExecutor sim(cfg, std::move(registry), obs, mem);
  (void)sim.run(kernel);

  const std::string extra = must_run_start_extra_json(obs);
  EXPECT_CONTAINS(extra, "\"deterministic\":true");
  EXPECT_CONTAINS(extra, "\"parallel\":false");
  EXPECT_CONTAINS(extra, "\"memory_model\"");
  EXPECT_CONTAINS(extra, "no_cache_addrspace");
}

static std::uint32_t unpack_u32_le_bytes(const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() < 4) return 0;
  return (static_cast<std::uint32_t>(bytes[0]) << 0) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
         (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

void test_deterministic_same_addr_multilane_store_overwrite_order() {
  using namespace gpusim;

  auto registry = make_registry_for_tests();
  ObsControl obs(ObsConfig{ false, 0 });
  AddrSpaceManager mem;

  const DevicePtr addr = mem.alloc_global(/*bytes=*/4, /*align=*/4);

  SimConfig cfg;
  cfg.warp_size = 8;

  InstRecord st;
  st.opcode = "st";
  st.mods.type_mod = "u32";
  st.mods.space = "global";
  st.operands = { addr_imm(addr), special("laneid") };

  InstRecord ret;
  ret.opcode = "ret";

  auto kernel = make_kernel("k_st_lane_order", /*reg_u32=*/0, { st, ret });

  // Use a block_dim.x smaller than warp_size to ensure we are testing the active mask prefix behavior too.
  LaunchConfig launch;
  launch.grid_dim = Dim3{ 1, 1, 1 };
  launch.block_dim = Dim3{ 5, 1, 1 }; // active lanes: 0..4
  launch.warp_size = cfg.warp_size;

  SimtExecutor sim(cfg, std::move(registry), obs, mem);
  auto out = sim.run(kernel, launch);

  EXPECT_TRUE(out.completed);
  EXPECT_TRUE(!out.diag.has_value());

  auto bytes = mem.read_global(addr, 4);
  EXPECT_TRUE(bytes.has_value());
  if (bytes) {
    // MemUnit stores lanes in increasing lane order (0..warp_size-1). Therefore the highest active lane wins.
    const std::uint32_t v = unpack_u32_le_bytes(*bytes);
    EXPECT_EQ(v, static_cast<std::uint32_t>(4));
  }
}

} // namespace

int main() {
  test_unknown_memory_model_is_fail_fast();
  test_allow_unknown_memory_model_continues();
  test_ld_param_kind_error();
  test_ld_param_miss_error();
  test_ld_global_kind_error();
  test_st_global_kind_error();
  test_deterministic_disables_parallel_in_run_start();
  test_deterministic_same_addr_multilane_store_overwrite_order();

  if (g_failures) {
    std::cerr << "FAILURES: " << g_failures << "\n";
    return 1;
  }
  std::cout << "OK\n";
  return 0;
}
