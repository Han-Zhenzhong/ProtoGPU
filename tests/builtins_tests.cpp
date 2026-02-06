#include "gpusim/observability.h"
#include "gpusim/units.h"

#include <cstdint>
#include <optional>
#include <string>

namespace {

int g_failures = 0;

void fail(const std::string& msg, const char* file, int line) {
  g_failures++;
  (void)msg;
  (void)file;
  (void)line;
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

static std::size_t reg_lane_index(std::size_t reg, std::uint32_t lane, std::uint32_t warp_size) {
  return reg * static_cast<std::size_t>(warp_size) + static_cast<std::size_t>(lane);
}

static gpusim::MicroOp make_mov_special_to_reg0(const std::string& name, std::uint32_t warp_size) {
  using namespace gpusim;

  Operand in;
  in.kind = OperandKind::Special;
  in.type = ValueType::U32;
  in.special = name;

  Operand out;
  out.kind = OperandKind::Reg;
  out.type = ValueType::U32;
  out.reg_id = 0;

  MicroOp u;
  u.kind = MicroOpKind::Exec;
  u.op = MicroOpOp::Mov;
  u.inputs = { in };
  u.outputs = { out };
  u.guard = lane_mask_all(warp_size);
  return u;
}

} // namespace

int main() {
  using namespace gpusim;

  {
    // 3D inversion: block=(4,3,2) => threads_per_block=24.
    // Validate tid.x repeats with period blockDim.x.
    const std::uint32_t warp_size = 32;

    WarpState warp;
    warp.launch.grid_dim = Dim3{ 1, 1, 1 };
    warp.launch.block_dim = Dim3{ 4, 3, 2 };
    warp.launch.warp_size = warp_size;
    warp.ctaid = Dim3{ 0, 0, 0 };
    warp.warpid = 0;
    warp.lane_base_thread_linear = 0;
    warp.pc = 0;
    warp.done = false;
    warp.active.width = warp_size;
    warp.active.bits_lo = (1u << 24) - 1u;

    warp.r_u32.assign(static_cast<std::size_t>(1) * warp_size, 0);
    warp.r_u64.assign(static_cast<std::size_t>(0) * warp_size, 0);
    warp.p.assign(static_cast<std::size_t>(8) * warp_size, 1);

    ObsControl obs(ObsConfig{ false, 16 });
    ExecCore exec;

    auto uop = make_mov_special_to_reg0("tid.x", warp_size);
    auto sr = exec.step(uop, warp, obs);
    EXPECT_TRUE(!sr.diag.has_value());

    for (std::uint32_t lane = 0; lane < 24; lane++) {
      const auto idx = reg_lane_index(0, lane, warp_size);
      EXPECT_EQ(warp.r_u32[idx], lane % 4u);
    }
    for (std::uint32_t lane = 24; lane < warp_size; lane++) {
      const auto idx = reg_lane_index(0, lane, warp_size);
      EXPECT_EQ(warp.r_u32[idx], 0u);
    }
  }

  {
    // Partial warp: block=(40,1,1) => warp0 full, warp1 only 8 lanes active.
    const std::uint32_t warp_size = 32;

    WarpState warp;
    warp.launch.grid_dim = Dim3{ 1, 1, 1 };
    warp.launch.block_dim = Dim3{ 40, 1, 1 };
    warp.launch.warp_size = warp_size;
    warp.ctaid = Dim3{ 0, 0, 0 };
    warp.warpid = 1;
    warp.lane_base_thread_linear = 32;
    warp.pc = 0;
    warp.done = false;
    warp.active.width = warp_size;
    warp.active.bits_lo = (1u << 8) - 1u;

    warp.r_u32.assign(static_cast<std::size_t>(1) * warp_size, 0);
    warp.r_u64.assign(static_cast<std::size_t>(0) * warp_size, 0);
    warp.p.assign(static_cast<std::size_t>(8) * warp_size, 1);

    ObsControl obs(ObsConfig{ false, 16 });
    ExecCore exec;

    auto uop = make_mov_special_to_reg0("tid.x", warp_size);
    auto sr = exec.step(uop, warp, obs);
    EXPECT_TRUE(!sr.diag.has_value());

    for (std::uint32_t lane = 0; lane < 8; lane++) {
      const auto idx = reg_lane_index(0, lane, warp_size);
      EXPECT_EQ(warp.r_u32[idx], 32u + lane);
    }
    for (std::uint32_t lane = 8; lane < warp_size; lane++) {
      const auto idx = reg_lane_index(0, lane, warp_size);
      EXPECT_EQ(warp.r_u32[idx], 0u);
    }
  }

  return g_failures == 0 ? 0 : 1;
}
