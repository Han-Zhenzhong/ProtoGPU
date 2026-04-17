#include "gpusim/observability.h"
#include "gpusim/units.h"

#include <cstdint>
#include <cstring>
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

static std::uint32_t f32_to_bits(float f) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &f, sizeof(float));
  return bits;
}

static gpusim::MicroOp make_warp_reduce_add_uop(std::int64_t dst_reg,
                                                 std::int64_t src_reg,
                                                 std::uint32_t warp_size,
                                                 std::uint32_t guard_bits) {
  using namespace gpusim;

  Operand in;
  in.kind = OperandKind::Reg;
  in.type = ValueType::F32;
  in.reg_id = src_reg;

  Operand out;
  out.kind = OperandKind::Reg;
  out.type = ValueType::F32;
  out.reg_id = dst_reg;

  MicroOp u;
  u.kind = MicroOpKind::Exec;
  u.op = MicroOpOp::WarpReduceAdd;
  u.attrs.type = ValueType::F32;
  u.inputs = { in };
  u.outputs = { out };
  u.guard.width = warp_size;
  u.guard.bits_lo = guard_bits;
  return u;
}

static gpusim::MicroOp make_cvt_uop(std::int64_t dst_reg,
                                    std::int64_t src_reg,
                                    std::uint32_t warp_size,
                                    std::uint32_t guard_bits) {
  using namespace gpusim;

  Operand in;
  in.kind = OperandKind::Reg;
  in.type = ValueType::U32;
  in.reg_id = src_reg;

  Operand out;
  out.kind = OperandKind::Reg;
  out.type = ValueType::F32;
  out.reg_id = dst_reg;

  MicroOp u;
  u.kind = MicroOpKind::Exec;
  u.op = MicroOpOp::Cvt;
  u.inputs = { in };
  u.outputs = { out };
  u.guard.width = warp_size;
  u.guard.bits_lo = guard_bits;
  return u;
}

static gpusim::MicroOp make_add_uop(std::int64_t dst_reg,
                                    std::int64_t lhs_reg,
                                    std::int64_t rhs_reg,
                                    gpusim::ValueType type,
                                    std::uint32_t warp_size,
                                    std::uint32_t guard_bits) {
  using namespace gpusim;

  Operand lhs;
  lhs.kind = OperandKind::Reg;
  lhs.type = type;
  lhs.reg_id = lhs_reg;

  Operand rhs;
  rhs.kind = OperandKind::Reg;
  rhs.type = type;
  rhs.reg_id = rhs_reg;

  Operand out;
  out.kind = OperandKind::Reg;
  out.type = type;
  out.reg_id = dst_reg;

  MicroOp u;
  u.kind = MicroOpKind::Exec;
  u.op = MicroOpOp::Add;
  u.attrs.type = type;
  u.inputs = {lhs, rhs};
  u.outputs = {out};
  u.guard.width = warp_size;
  u.guard.bits_lo = guard_bits;
  return u;
}

static gpusim::MicroOp make_shfl_uop(std::int64_t dst_reg,
                                     std::int64_t src_reg,
                                     std::int64_t lane_or_idx_reg,
                                     std::int64_t clamp_reg,
                                     std::int64_t mask_reg,
                                     std::uint32_t warp_size,
                                     const std::string& mode_flag) {
  using namespace gpusim;

  Operand out;
  out.kind = OperandKind::Reg;
  out.type = ValueType::F32;
  out.reg_id = dst_reg;

  Operand in_src;
  in_src.kind = OperandKind::Reg;
  in_src.type = ValueType::F32;
  in_src.reg_id = src_reg;

  Operand in_lane;
  in_lane.kind = OperandKind::Reg;
  in_lane.type = ValueType::U32;
  in_lane.reg_id = lane_or_idx_reg;

  Operand in_clamp;
  in_clamp.kind = OperandKind::Reg;
  in_clamp.type = ValueType::U32;
  in_clamp.reg_id = clamp_reg;

  Operand in_mask;
  in_mask.kind = OperandKind::Reg;
  in_mask.type = ValueType::U32;
  in_mask.reg_id = mask_reg;

  MicroOp u;
  u.kind = MicroOpKind::Exec;
  u.op = MicroOpOp::Shfl;
  u.attrs.type = ValueType::U32;
  u.attrs.flags = {"sync", mode_flag};
  u.inputs = {in_src, in_lane, in_clamp, in_mask};
  u.outputs = {out};
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

  {
    // CVT u32->f32 updates only participating lanes with converted f32 bits.
    const std::uint32_t warp_size = 8;

    WarpState warp;
    warp.launch.grid_dim = Dim3{ 1, 1, 1 };
    warp.launch.block_dim = Dim3{ warp_size, 1, 1 };
    warp.launch.warp_size = warp_size;
    warp.ctaid = Dim3{ 0, 0, 0 };
    warp.warpid = 0;
    warp.lane_base_thread_linear = 0;
    warp.pc = 0;
    warp.done = false;
    warp.active.width = warp_size;
    warp.active.bits_lo = 0xFFu;

    warp.r_u32.assign(static_cast<std::size_t>(2) * warp_size, 0u);
    warp.r_u64.clear();
    warp.p.assign(static_cast<std::size_t>(8) * warp_size, 1);

    for (std::uint32_t lane = 0; lane < warp_size; lane++) {
      warp.r_u32[reg_lane_index(1, lane, warp_size)] = lane + 10u;
      warp.r_u32[reg_lane_index(0, lane, warp_size)] = f32_to_bits(1234.0f + static_cast<float>(lane));
    }

    const std::uint32_t guard_bits = 0b00111100u;
    ObsControl obs(ObsConfig{ false, 16 });
    ExecCore exec;
    auto uop = make_cvt_uop(/*dst=*/0, /*src=*/1, warp_size, guard_bits);

    auto sr = exec.step(uop, warp, obs);
    EXPECT_TRUE(!sr.diag.has_value());

    for (std::uint32_t lane = 0; lane < warp_size; lane++) {
      const auto idx = reg_lane_index(0, lane, warp_size);
      if ((guard_bits & (1u << lane)) != 0u) {
        EXPECT_EQ(warp.r_u32[idx], f32_to_bits(static_cast<float>(lane + 10u)));
      } else {
        EXPECT_EQ(warp.r_u32[idx], f32_to_bits(1234.0f + static_cast<float>(lane)));
      }
    }
  }

  {
    // CVT rejects unsupported type pairs in this milestone.
    const std::uint32_t warp_size = 4;

    WarpState warp;
    warp.launch.grid_dim = Dim3{ 1, 1, 1 };
    warp.launch.block_dim = Dim3{ warp_size, 1, 1 };
    warp.launch.warp_size = warp_size;
    warp.ctaid = Dim3{ 0, 0, 0 };
    warp.warpid = 0;
    warp.lane_base_thread_linear = 0;
    warp.pc = 0;
    warp.done = false;
    warp.active.width = warp_size;
    warp.active.bits_lo = 0xFu;

    warp.r_u32.assign(static_cast<std::size_t>(2) * warp_size, 0u);
    warp.r_u64.clear();
    warp.p.assign(static_cast<std::size_t>(8) * warp_size, 1);

    ObsControl obs(ObsConfig{ false, 16 });
    ExecCore exec;
    auto uop = make_cvt_uop(/*dst=*/0, /*src=*/1, warp_size, 0xFu);
    uop.inputs[0].type = ValueType::S32;

    auto sr = exec.step(uop, warp, obs);
    EXPECT_TRUE(sr.diag.has_value());
    if (sr.diag) {
      EXPECT_EQ(sr.diag->code, std::string("E_UOP_TYPE"));
    }
  }

  {
    // ADD f32 performs floating-point addition (not integer bit arithmetic).
    const std::uint32_t warp_size = 4;

    WarpState warp;
    warp.launch.grid_dim = Dim3{ 1, 1, 1 };
    warp.launch.block_dim = Dim3{ warp_size, 1, 1 };
    warp.launch.warp_size = warp_size;
    warp.ctaid = Dim3{ 0, 0, 0 };
    warp.warpid = 0;
    warp.lane_base_thread_linear = 0;
    warp.pc = 0;
    warp.done = false;
    warp.active.width = warp_size;
    warp.active.bits_lo = 0xFu;

    warp.r_u32.assign(static_cast<std::size_t>(3) * warp_size, 0u);
    warp.r_u64.clear();
    warp.p.assign(static_cast<std::size_t>(8) * warp_size, 1);

    for (std::uint32_t lane = 0; lane < warp_size; lane++) {
      warp.r_u32[reg_lane_index(1, lane, warp_size)] = f32_to_bits(static_cast<float>(lane + 1));
      warp.r_u32[reg_lane_index(2, lane, warp_size)] = f32_to_bits(10.0f + static_cast<float>(lane));
    }

    ObsControl obs(ObsConfig{ false, 16 });
    ExecCore exec;
    auto uop = make_add_uop(/*dst=*/0, /*lhs=*/1, /*rhs=*/2, ValueType::F32, warp_size, 0xFu);

    auto sr = exec.step(uop, warp, obs);
    EXPECT_TRUE(!sr.diag.has_value());
    for (std::uint32_t lane = 0; lane < warp_size; lane++) {
      EXPECT_EQ(warp.r_u32[reg_lane_index(0, lane, warp_size)], f32_to_bits(static_cast<float>(11u + 2u * lane)));
    }
  }

  {
    // Full-mask warp_reduce_add: all lanes get the same f32 sum.
    const std::uint32_t warp_size = 8;

    WarpState warp;
    warp.launch.grid_dim = Dim3{ 1, 1, 1 };
    warp.launch.block_dim = Dim3{ warp_size, 1, 1 };
    warp.launch.warp_size = warp_size;
    warp.ctaid = Dim3{ 0, 0, 0 };
    warp.warpid = 0;
    warp.lane_base_thread_linear = 0;
    warp.pc = 0;
    warp.done = false;
    warp.active.width = warp_size;
    warp.active.bits_lo = (1u << warp_size) - 1u;

    warp.r_u32.assign(static_cast<std::size_t>(2) * warp_size, 0u);
    warp.r_u64.clear();
    warp.p.assign(static_cast<std::size_t>(8) * warp_size, 1);

    for (std::uint32_t lane = 0; lane < warp_size; lane++) {
      const auto idx = reg_lane_index(1, lane, warp_size);
      warp.r_u32[idx] = f32_to_bits(static_cast<float>(lane + 1));
    }

    ObsControl obs(ObsConfig{ false, 16 });
    ExecCore exec;
    auto uop = make_warp_reduce_add_uop(/*dst=*/0, /*src=*/1, warp_size, (1u << warp_size) - 1u);

    auto sr = exec.step(uop, warp, obs);
    EXPECT_TRUE(!sr.diag.has_value());

    const float expected = 36.0f;
    for (std::uint32_t lane = 0; lane < warp_size; lane++) {
      const auto idx = reg_lane_index(0, lane, warp_size);
      EXPECT_EQ(warp.r_u32[idx], f32_to_bits(expected));
    }
  }

  {
    // Guard-masked warp_reduce_add: only participating lanes are updated.
    const std::uint32_t warp_size = 8;

    WarpState warp;
    warp.launch.grid_dim = Dim3{ 1, 1, 1 };
    warp.launch.block_dim = Dim3{ warp_size, 1, 1 };
    warp.launch.warp_size = warp_size;
    warp.ctaid = Dim3{ 0, 0, 0 };
    warp.warpid = 0;
    warp.lane_base_thread_linear = 0;
    warp.pc = 0;
    warp.done = false;
    warp.active.width = warp_size;
    warp.active.bits_lo = 0xFFu;

    warp.r_u32.assign(static_cast<std::size_t>(2) * warp_size, 0u);
    warp.r_u64.clear();
    warp.p.assign(static_cast<std::size_t>(8) * warp_size, 1);

    for (std::uint32_t lane = 0; lane < warp_size; lane++) {
      warp.r_u32[reg_lane_index(1, lane, warp_size)] = f32_to_bits(static_cast<float>(lane + 1));
      warp.r_u32[reg_lane_index(0, lane, warp_size)] = f32_to_bits(1000.0f + static_cast<float>(lane));
    }

    // participate lanes 0,2,4,6 only
    const std::uint32_t guard_bits = 0b01010101u;
    ObsControl obs(ObsConfig{ false, 16 });
    ExecCore exec;
    auto uop = make_warp_reduce_add_uop(/*dst=*/0, /*src=*/1, warp_size, guard_bits);

    auto sr = exec.step(uop, warp, obs);
    EXPECT_TRUE(!sr.diag.has_value());

    const float expected = 1.0f + 3.0f + 5.0f + 7.0f;
    for (std::uint32_t lane = 0; lane < warp_size; lane++) {
      const auto idx = reg_lane_index(0, lane, warp_size);
      if ((guard_bits & (1u << lane)) != 0u) {
        EXPECT_EQ(warp.r_u32[idx], f32_to_bits(expected));
      } else {
        EXPECT_EQ(warp.r_u32[idx], f32_to_bits(1000.0f + static_cast<float>(lane)));
      }
    }
  }

  {
    // In-place form (dst == src) is read-then-write correct.
    const std::uint32_t warp_size = 4;

    WarpState warp;
    warp.launch.grid_dim = Dim3{ 1, 1, 1 };
    warp.launch.block_dim = Dim3{ warp_size, 1, 1 };
    warp.launch.warp_size = warp_size;
    warp.ctaid = Dim3{ 0, 0, 0 };
    warp.warpid = 0;
    warp.lane_base_thread_linear = 0;
    warp.pc = 0;
    warp.done = false;
    warp.active.width = warp_size;
    warp.active.bits_lo = 0xFu;

    warp.r_u32.assign(static_cast<std::size_t>(1) * warp_size, 0u);
    warp.r_u64.clear();
    warp.p.assign(static_cast<std::size_t>(8) * warp_size, 1);

    warp.r_u32[reg_lane_index(0, 0, warp_size)] = f32_to_bits(10.0f);
    warp.r_u32[reg_lane_index(0, 1, warp_size)] = f32_to_bits(20.0f);
    warp.r_u32[reg_lane_index(0, 2, warp_size)] = f32_to_bits(30.0f);
    warp.r_u32[reg_lane_index(0, 3, warp_size)] = f32_to_bits(40.0f);

    ObsControl obs(ObsConfig{ false, 16 });
    ExecCore exec;
    auto uop = make_warp_reduce_add_uop(/*dst=*/0, /*src=*/0, warp_size, 0xFu);

    auto sr = exec.step(uop, warp, obs);
    EXPECT_TRUE(!sr.diag.has_value());

    const auto expected_bits = f32_to_bits(100.0f);
    for (std::uint32_t lane = 0; lane < warp_size; lane++) {
      EXPECT_EQ(warp.r_u32[reg_lane_index(0, lane, warp_size)], expected_bits);
    }
  }

  {
    // Empty participating mask is a no-op.
    const std::uint32_t warp_size = 4;

    WarpState warp;
    warp.launch.grid_dim = Dim3{ 1, 1, 1 };
    warp.launch.block_dim = Dim3{ warp_size, 1, 1 };
    warp.launch.warp_size = warp_size;
    warp.ctaid = Dim3{ 0, 0, 0 };
    warp.warpid = 0;
    warp.lane_base_thread_linear = 0;
    warp.pc = 0;
    warp.done = false;
    warp.active.width = warp_size;
    warp.active.bits_lo = 0u;

    warp.r_u32.assign(static_cast<std::size_t>(2) * warp_size, 0u);
    warp.r_u64.clear();
    warp.p.assign(static_cast<std::size_t>(8) * warp_size, 1);

    for (std::uint32_t lane = 0; lane < warp_size; lane++) {
      warp.r_u32[reg_lane_index(0, lane, warp_size)] = f32_to_bits(300.0f + static_cast<float>(lane));
      warp.r_u32[reg_lane_index(1, lane, warp_size)] = f32_to_bits(static_cast<float>(lane + 1));
    }

    ObsControl obs(ObsConfig{ false, 16 });
    ExecCore exec;
    auto uop = make_warp_reduce_add_uop(/*dst=*/0, /*src=*/1, warp_size, 0xFu);

    auto sr = exec.step(uop, warp, obs);
    EXPECT_TRUE(!sr.diag.has_value());
    for (std::uint32_t lane = 0; lane < warp_size; lane++) {
      EXPECT_EQ(warp.r_u32[reg_lane_index(0, lane, warp_size)], f32_to_bits(300.0f + static_cast<float>(lane)));
    }
  }

  {
    // Non-f32 type is rejected for this milestone.
    const std::uint32_t warp_size = 4;

    WarpState warp;
    warp.launch.grid_dim = Dim3{ 1, 1, 1 };
    warp.launch.block_dim = Dim3{ warp_size, 1, 1 };
    warp.launch.warp_size = warp_size;
    warp.ctaid = Dim3{ 0, 0, 0 };
    warp.warpid = 0;
    warp.lane_base_thread_linear = 0;
    warp.pc = 0;
    warp.done = false;
    warp.active.width = warp_size;
    warp.active.bits_lo = 0xFu;

    warp.r_u32.assign(static_cast<std::size_t>(2) * warp_size, 0u);
    warp.r_u64.clear();
    warp.p.assign(static_cast<std::size_t>(8) * warp_size, 1);

    ObsControl obs(ObsConfig{ false, 16 });
    ExecCore exec;
    auto uop = make_warp_reduce_add_uop(/*dst=*/0, /*src=*/1, warp_size, 0xFu);
    uop.attrs.type = ValueType::U32;

    auto sr = exec.step(uop, warp, obs);
    EXPECT_TRUE(sr.diag.has_value());
    if (sr.diag) {
      EXPECT_EQ(sr.diag->code, std::string("E_UOP_TYPE"));
    }
  }

  {
    // SHFL down across width=8 segment: lane i gets value from i+1, lane7 keeps its own value.
    const std::uint32_t warp_size = 8;

    WarpState warp;
    warp.launch.grid_dim = Dim3{ 1, 1, 1 };
    warp.launch.block_dim = Dim3{ warp_size, 1, 1 };
    warp.launch.warp_size = warp_size;
    warp.ctaid = Dim3{ 0, 0, 0 };
    warp.warpid = 0;
    warp.lane_base_thread_linear = 0;
    warp.pc = 0;
    warp.done = false;
    warp.active.width = warp_size;
    warp.active.bits_lo = 0xFFu;

    warp.r_u32.assign(static_cast<std::size_t>(5) * warp_size, 0u);
    warp.r_u64.clear();
    warp.p.assign(static_cast<std::size_t>(8) * warp_size, 1);

    const std::uint32_t kClamp8 = ((32u - 8u) << 8) | 31u;
    for (std::uint32_t lane = 0; lane < warp_size; lane++) {
      warp.r_u32[reg_lane_index(1, lane, warp_size)] = f32_to_bits(static_cast<float>(lane + 1));
      warp.r_u32[reg_lane_index(2, lane, warp_size)] = 1u;
      warp.r_u32[reg_lane_index(3, lane, warp_size)] = kClamp8;
      warp.r_u32[reg_lane_index(4, lane, warp_size)] = 0xFFFFFFFFu;
    }

    ObsControl obs(ObsConfig{ false, 16 });
    ExecCore exec;
    auto uop = make_shfl_uop(/*dst=*/0, /*src=*/1, /*lane=*/2, /*clamp=*/3, /*mask=*/4, warp_size, "down");

    auto sr = exec.step(uop, warp, obs);
    EXPECT_TRUE(!sr.diag.has_value());

    for (std::uint32_t lane = 0; lane < warp_size; lane++) {
      const auto idx = reg_lane_index(0, lane, warp_size);
      const float expected = (lane + 1u < warp_size) ? static_cast<float>(lane + 2u) : static_cast<float>(lane + 1u);
      EXPECT_EQ(warp.r_u32[idx], f32_to_bits(expected));
    }
  }

  {
    // SHFL idx across width=8 segment: all lanes read from source lane 0.
    const std::uint32_t warp_size = 8;

    WarpState warp;
    warp.launch.grid_dim = Dim3{ 1, 1, 1 };
    warp.launch.block_dim = Dim3{ warp_size, 1, 1 };
    warp.launch.warp_size = warp_size;
    warp.ctaid = Dim3{ 0, 0, 0 };
    warp.warpid = 0;
    warp.lane_base_thread_linear = 0;
    warp.pc = 0;
    warp.done = false;
    warp.active.width = warp_size;
    warp.active.bits_lo = 0xFFu;

    warp.r_u32.assign(static_cast<std::size_t>(5) * warp_size, 0u);
    warp.r_u64.clear();
    warp.p.assign(static_cast<std::size_t>(8) * warp_size, 1);

    const std::uint32_t kClamp8 = ((32u - 8u) << 8) | 31u;
    for (std::uint32_t lane = 0; lane < warp_size; lane++) {
      warp.r_u32[reg_lane_index(1, lane, warp_size)] = f32_to_bits(static_cast<float>(100u + lane));
      warp.r_u32[reg_lane_index(2, lane, warp_size)] = 0u;
      warp.r_u32[reg_lane_index(3, lane, warp_size)] = kClamp8;
      warp.r_u32[reg_lane_index(4, lane, warp_size)] = 0xFFFFFFFFu;
    }

    ObsControl obs(ObsConfig{ false, 16 });
    ExecCore exec;
    auto uop = make_shfl_uop(/*dst=*/0, /*src=*/1, /*lane=*/2, /*clamp=*/3, /*mask=*/4, warp_size, "idx");

    auto sr = exec.step(uop, warp, obs);
    EXPECT_TRUE(!sr.diag.has_value());

    for (std::uint32_t lane = 0; lane < warp_size; lane++) {
      const auto idx = reg_lane_index(0, lane, warp_size);
      EXPECT_EQ(warp.r_u32[idx], f32_to_bits(100.0f));
    }
  }

  return g_failures == 0 ? 0 : 1;
}
