#include "gpusim/contracts.h"
#include "gpusim/frontend.h"
#include "gpusim/instruction_desc.h"
#include "gpusim/memory.h"
#include "gpusim/observability.h"
#include "gpusim/simt.h"

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

  // Minimal inst_desc JSON for:
  // - setp (pred,special,imm) -> EXEC SETP in=[1,2] out=[0]
  // - bra  (imm)             -> CTRL BRA  in=[0]
  // - bra2 (imm,imm)         -> CTRL BRA  in=[0] and CTRL BRA in=[1] (conflict)
  // - mov  (reg,imm)         -> EXEC MOV  in=[1] out=[0]
  // - warp_reduce_add (reg,reg) -> EXEC WARP_REDUCE_ADD in=[1] out=[0]
  // - ret  ()                -> CTRL RET  in=[]
  const std::string desc_json = R"JSON(
  {
    "insts": [
      {
        "opcode": "setp",
        "type_mod": "u32",
        "operand_kinds": ["pred", "special", "imm"],
        "uops": [
          { "kind": "EXEC", "op": "SETP", "in": [1,2], "out": [0] }
        ]
      },
      {
        "opcode": "bra",
        "type_mod": "",
        "operand_kinds": ["imm"],
        "uops": [
          { "kind": "CTRL", "op": "BRA", "in": [0], "out": [] }
        ]
      },
      {
        "opcode": "bra2",
        "type_mod": "",
        "operand_kinds": ["imm", "imm"],
        "uops": [
          { "kind": "CTRL", "op": "BRA", "in": [0], "out": [] },
          { "kind": "CTRL", "op": "BRA", "in": [1], "out": [] }
        ]
      },
      {
        "opcode": "mov",
        "type_mod": "u32",
        "operand_kinds": ["reg", "imm"],
        "uops": [
          { "kind": "EXEC", "op": "MOV", "in": [1], "out": [0] }
        ]
      },
      {
        "opcode": "warp_reduce_add",
        "type_mod": "f32",
        "operand_kinds": ["reg", "reg"],
        "uops": [
          { "kind": "EXEC", "op": "WARP_REDUCE_ADD", "in": [1], "out": [0] }
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

gpusim::Operand pred(std::int64_t pid) {
  gpusim::Operand o;
  o.kind = gpusim::OperandKind::Pred;
  o.pred_id = pid;
  return o;
}

gpusim::Operand special(std::string s) {
  gpusim::Operand o;
  o.kind = gpusim::OperandKind::Special;
  o.special = std::move(s);
  return o;
}

std::optional<std::uint64_t> find_counter(const gpusim::ObsControl& obs, const std::string& key) {
  for (const auto& kv : obs.counters_snapshot()) {
    if (kv.first == key) return kv.second;
  }
  return std::nullopt;
}

void test_pred_oob_is_simt_error() {
  using namespace gpusim;

  auto registry = make_registry_for_tests();
  ObsControl obs(ObsConfig{ false, 0 });
  AddrSpaceManager mem;

  SimConfig cfg;
  cfg.warp_size = 1;

  // Any instruction is fine; we only need to hit the predication injection stage.
  InstRecord inst;
  inst.opcode = "mov";
  inst.mods.type_mod = "u32";
  inst.pred = PredGuard{ 8, false }; // warp has 8 predicate registers => pid==8 is OOB
  inst.operands = { reg_u32(0), imm(0) };

  auto kernel = make_kernel("k_pred_oob", /*reg_u32=*/1, { inst });

  SimtExecutor sim(cfg, std::move(registry), obs, mem);
  auto out = sim.run(kernel);
  EXPECT_TRUE(!out.completed);
  EXPECT_TRUE(out.diag.has_value());
  if (out.diag) {
    EXPECT_EQ(out.diag->module, std::string("simt"));
    EXPECT_EQ(out.diag->code, std::string("E_PRED_OOB"));
  }
}

void test_divergence_reconverges_and_completes() {
  using namespace gpusim;

  auto registry = make_registry_for_tests();
  ObsControl obs(ObsConfig{ true, 4096 });
  AddrSpaceManager mem;

  SimConfig cfg;
  cfg.warp_size = 2;

  // 0: setp p0, %laneid, 1  (lt) => lane0=true, lane1=false
  // 1: @p0 bra 3            => lane0 jumps to 3, lane1 falls through to 2
  // 2: mov r0, 1            => executed only by lane1
  // 3: mov r0, 2            => reconvergence point (both lanes execute)
  // 4: ret
  InstRecord setp_inst;
  setp_inst.opcode = "setp";
  setp_inst.mods.type_mod = "u32";
  setp_inst.operands = { pred(0), special("laneid"), imm(1) };

  InstRecord bra_inst;
  bra_inst.opcode = "bra";
  bra_inst.pred = PredGuard{ 0, false };
  bra_inst.operands = { imm(3) };

  InstRecord mov1;
  mov1.opcode = "mov";
  mov1.mods.type_mod = "u32";
  mov1.operands = { reg_u32(0), imm(1) };

  InstRecord mov2;
  mov2.opcode = "mov";
  mov2.mods.type_mod = "u32";
  mov2.operands = { reg_u32(0), imm(2) };

  InstRecord ret;
  ret.opcode = "ret";

  auto kernel = make_kernel("k_div", /*reg_u32=*/1, { setp_inst, bra_inst, mov1, mov2, ret });

  LaunchConfig launch;
  launch.grid_dim = Dim3{ 1, 1, 1 };
  launch.block_dim = Dim3{ 2, 1, 1 };
  launch.warp_size = 2;

  SimtExecutor sim(cfg, std::move(registry), obs, mem);
  auto out = sim.run(kernel, launch);

  EXPECT_TRUE(out.completed);
  EXPECT_TRUE(!out.diag.has_value());

  bool saw_split = false;
  for (const auto& e : obs.trace_snapshot()) {
    if (e.category == EventCategory::Ctrl && e.action == "SIMT_SPLIT") {
      saw_split = true;
      break;
    }
  }
  EXPECT_TRUE(saw_split);
}

void test_next_pc_conflict_is_fail_fast() {
  using namespace gpusim;

  auto registry = make_registry_for_tests();
  ObsControl obs(ObsConfig{ false, 0 });
  AddrSpaceManager mem;

  SimConfig cfg;
  cfg.warp_size = 1;

  InstRecord inst;
  inst.opcode = "bra2";
  inst.operands = { imm(1), imm(2) };

  auto kernel = make_kernel("k_next_pc_conflict", /*reg_u32=*/0, { inst });

  SimtExecutor sim(cfg, std::move(registry), obs, mem);
  auto out = sim.run(kernel);
  EXPECT_TRUE(!out.completed);
  EXPECT_TRUE(out.diag.has_value());
  if (out.diag) {
    EXPECT_EQ(out.diag->module, std::string("simt"));
    EXPECT_EQ(out.diag->code, std::string("E_NEXT_PC_CONFLICT"));
  }
}

void test_pc_oob_is_fail_fast() {
  using namespace gpusim;

  auto registry = make_registry_for_tests();
  ObsControl obs(ObsConfig{ false, 0 });
  AddrSpaceManager mem;

  SimConfig cfg;
  cfg.warp_size = 1;
  cfg.max_steps = 10;

  // PC 0 branches to PC 5; the next fetch will fail with E_PC_OOB.
  InstRecord inst;
  inst.opcode = "bra";
  inst.operands = { imm(5) };

  auto kernel = make_kernel("k_pc_oob", /*reg_u32=*/0, { inst });

  SimtExecutor sim(cfg, std::move(registry), obs, mem);
  auto out = sim.run(kernel);
  EXPECT_TRUE(!out.completed);
  EXPECT_TRUE(out.diag.has_value());
  if (out.diag) {
    EXPECT_EQ(out.diag->module, std::string("simt"));
    EXPECT_EQ(out.diag->code, std::string("E_PC_OOB"));
  }
}

void test_warp_reduce_add_predicated_exec_mask_path() {
  using namespace gpusim;

  auto registry = make_registry_for_tests();
  ObsControl obs(ObsConfig{ true, 1024 });
  AddrSpaceManager mem;

  SimConfig cfg;
  cfg.warp_size = 2;

  // 0: mov.u32 r0, 1065353216  // 1.0f bits
  // 1: setp.u32 p0, %laneid, 1 // lane0 true, lane1 false (default lt)
  // 2: @p0 warp_reduce_add.f32 r1, r0
  // 3: ret
  InstRecord mov;
  mov.opcode = "mov";
  mov.mods.type_mod = "u32";
  mov.operands = { reg_u32(0), imm(1065353216) };

  InstRecord setp_inst;
  setp_inst.opcode = "setp";
  setp_inst.mods.type_mod = "u32";
  setp_inst.operands = { pred(0), special("laneid"), imm(1) };

  InstRecord wra;
  wra.opcode = "warp_reduce_add";
  wra.mods.type_mod = "f32";
  wra.pred = PredGuard{ 0, false };
  wra.operands = { reg_u32(1), reg_u32(0) };

  InstRecord ret;
  ret.opcode = "ret";

  auto kernel = make_kernel("k_warp_reduce_pred", /*reg_u32=*/2, { mov, setp_inst, wra, ret });

  LaunchConfig launch;
  launch.grid_dim = Dim3{ 1, 1, 1 };
  launch.block_dim = Dim3{ 2, 1, 1 };
  launch.warp_size = 2;

  SimtExecutor sim(cfg, std::move(registry), obs, mem);
  const auto out = sim.run(kernel, launch);

  EXPECT_TRUE(out.completed);
  EXPECT_TRUE(!out.diag.has_value());

  const auto c = find_counter(obs, "uop.exec.warp_reduce_add.f32");
  EXPECT_TRUE(c.has_value());
  if (c) {
    EXPECT_EQ(*c, static_cast<std::uint64_t>(1));
  }
}

} // namespace

int main() {
  test_pred_oob_is_simt_error();
  test_divergence_reconverges_and_completes();
  test_next_pc_conflict_is_fail_fast();
  test_pc_oob_is_fail_fast();
  test_warp_reduce_add_predicated_exec_mask_path();

  if (g_failures) {
    std::cerr << "FAILURES: " << g_failures << "\n";
    return 1;
  }
  std::cout << "OK\n";
  return 0;
}
