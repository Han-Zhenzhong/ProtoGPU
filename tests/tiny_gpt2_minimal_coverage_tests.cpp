#include "gpusim/runtime.h"

#include "gpusim/frontend.h"

#include <cstdint>
#include <cstring>
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
    const auto _a = (a); \
    const auto _b = (b); \
    if (!(_a == _b)) { \
      fail(std::string("EXPECT_EQ failed: ") + #a + " != " + #b, __FILE__, __LINE__); \
    } \
  } while (0)

static std::vector<std::uint8_t> pack_le_u32(std::uint32_t v) {
  std::vector<std::uint8_t> out(4);
  out[0] = static_cast<std::uint8_t>(v & 0xFFu);
  out[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
  out[2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
  out[3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
  return out;
}

static std::vector<std::uint8_t> pack_le_u64(std::uint64_t v) {
  std::vector<std::uint8_t> out(8);
  for (int i = 0; i < 8; i++) out[i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu);
  return out;
}

static std::uint32_t f32_to_bits(float f) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &f, sizeof(float));
  return bits;
}

static float bits_to_f32(std::uint32_t bits) {
  float f = 0.0f;
  std::memcpy(&f, &bits, sizeof(float));
  return f;
}

static void write_le(std::vector<std::uint8_t>& blob, std::uint32_t off, std::uint64_t v, std::uint32_t size) {
  EXPECT_TRUE(static_cast<std::uint64_t>(off) + size <= blob.size());
  for (std::uint32_t b = 0; b < size; b++) {
    blob[static_cast<std::size_t>(off + b)] = static_cast<std::uint8_t>((v >> (8u * b)) & 0xFFu);
  }
}

static gpusim::KernelArgs make_args_from_ptx(const std::string& ptx_path,
                                            const std::string& entry,
                                            const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& args) {
  using namespace gpusim;

  Parser parser;
  auto mod_tokens = parser.parse_ptx_file_tokens(ptx_path);
  Binder binder;
  KernelTokens kt = binder.bind_kernel_by_name(mod_tokens, entry);

  KernelArgs ka;
  ka.layout = kt.params;

  std::uint32_t blob_bytes = 0;
  for (const auto& p : ka.layout) {
    blob_bytes = std::max(blob_bytes, static_cast<std::uint32_t>(p.offset + p.size));
  }
  ka.blob.assign(static_cast<std::size_t>(blob_bytes), 0);

  for (const auto& [name, bytes] : args) {
    bool found = false;
    for (const auto& p : ka.layout) {
      if (p.name != name) continue;
      found = true;
      EXPECT_EQ(static_cast<std::uint32_t>(bytes.size()), p.size);
      EXPECT_TRUE(static_cast<std::size_t>(p.offset) + bytes.size() <= ka.blob.size());
      for (std::size_t i = 0; i < bytes.size(); i++) {
        ka.blob[static_cast<std::size_t>(p.offset) + i] = bytes[i];
      }
    }
    EXPECT_TRUE(found);
  }

  return ka;
}

static void test_m1_fma_ldst_predication() {
  using namespace gpusim;

  AppConfig cfg;
  cfg.sim.warp_size = 8;
  cfg.sim.max_steps = 100000;
  cfg.sim.sm_count = 1;
  cfg.sim.parallel = false;
  cfg.sim.deterministic = true;
  cfg.sim.memory_model = "no_cache_addrspace";
  cfg.sim.cta_scheduler = "fifo";
  cfg.sim.warp_scheduler = "in_order_run_to_completion";
  cfg.sim.allow_unknown_selectors = false;

  cfg.obs.enabled = false;
  cfg.obs.trace_capacity = 0;

  Runtime rt(cfg);

  const std::string ptx = "tests/fixtures/ptx/m1_fma_ldst_f32.ptx";
  const std::string isa = "assets/ptx_isa/demo_ptx64.json";
  const std::string desc = "assets/inst_desc/demo_desc.json";
  const std::string entry = "m1_fma_ldst_f32";

  constexpr std::uint32_t warp_size = 8;
  constexpr std::uint32_t n = 5; // predication should keep lanes [5..7] unchanged

  std::vector<float> a(warp_size), b(warp_size);
  for (std::uint32_t i = 0; i < warp_size; i++) {
    a[i] = 0.5f + static_cast<float>(i);
    b[i] = -1.25f + static_cast<float>(i) * 0.25f;
  }

  const std::uint64_t bytes = static_cast<std::uint64_t>(warp_size) * 4;
  DevicePtr d_a = rt.device_malloc(bytes, 16);
  DevicePtr d_b = rt.device_malloc(bytes, 16);
  DevicePtr d_out = rt.device_malloc(bytes, 16);

  HostBufId h_a = rt.host_alloc(bytes);
  HostBufId h_b = rt.host_alloc(bytes);
  HostBufId h_out = rt.host_alloc(bytes);

  // pack a/b into bytes
  {
    std::vector<std::uint8_t> ba(bytes), bb(bytes), bo(bytes);
    for (std::uint32_t i = 0; i < warp_size; i++) {
      const auto ab = f32_to_bits(a[i]);
      const auto bbv = f32_to_bits(b[i]);
      const auto ob = 0xDEADBEEFu; // sentinel
      std::memcpy(&ba[i * 4], &ab, 4);
      std::memcpy(&bb[i * 4], &bbv, 4);
      std::memcpy(&bo[i * 4], &ob, 4);
    }
    rt.host_write(h_a, 0, ba);
    rt.host_write(h_b, 0, bb);
    rt.host_write(h_out, 0, bo);
  }

  rt.memcpy_h2d(d_a, h_a, 0, bytes);
  rt.memcpy_h2d(d_b, h_b, 0, bytes);
  rt.memcpy_h2d(d_out, h_out, 0, bytes);

  LaunchConfig launch;
  launch.grid_dim = Dim3{ 1, 1, 1 };
  launch.block_dim = Dim3{ warp_size, 1, 1 };
  launch.warp_size = warp_size;

  auto ka = make_args_from_ptx(ptx, entry,
                              {
                                  {"in_a", pack_le_u64(d_a)},
                                  {"in_b", pack_le_u64(d_b)},
                                  {"out", pack_le_u64(d_out)},
                                  {"n", pack_le_u32(n)},
                              });

  auto out = rt.run_ptx_kernel_with_args_launch(ptx, isa, desc, ka, launch);
  EXPECT_TRUE(out.sim.completed);
  EXPECT_TRUE(!out.sim.diag.has_value());

  rt.memcpy_d2h(h_out, 0, d_out, bytes);
  auto bytes_out = rt.host_read(h_out, 0, bytes);
  EXPECT_TRUE(bytes_out.has_value());

  for (std::uint32_t i = 0; i < warp_size; i++) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, bytes_out->data() + i * 4, 4);
    if (i < n) {
      const float got = bits_to_f32(bits);
      const float expect = a[i] * b[i] + 1.0f;
      const float diff = got - expect;
      // loose tolerance; we only need consistent float32 behavior.
      EXPECT_TRUE(diff > -1e-5f && diff < 1e-5f);
    } else {
      EXPECT_EQ(bits, 0xDEADBEEFu);
    }
  }
}

static void test_m4_bra_loop_uniform() {
  using namespace gpusim;

  AppConfig cfg;
  cfg.sim.warp_size = 1;
  cfg.sim.max_steps = 100000;
  cfg.sim.sm_count = 1;
  cfg.sim.parallel = false;
  cfg.sim.deterministic = true;
  cfg.sim.memory_model = "no_cache_addrspace";
  cfg.sim.cta_scheduler = "fifo";
  cfg.sim.warp_scheduler = "in_order_run_to_completion";
  cfg.sim.allow_unknown_selectors = false;

  cfg.obs.enabled = false;
  cfg.obs.trace_capacity = 0;

  Runtime rt(cfg);

  const std::string ptx = "tests/fixtures/ptx/m4_bra_loop_u32.ptx";
  const std::string isa = "assets/ptx_isa/demo_ptx64.json";
  const std::string desc = "assets/inst_desc/demo_desc.json";
  const std::string entry = "m4_bra_loop_u32";

  DevicePtr d_out = rt.device_malloc(4, 16);
  HostBufId h_out = rt.host_alloc(4);

  // sentinel
  rt.host_write(h_out, 0, std::vector<std::uint8_t>{0xAA, 0xBB, 0xCC, 0xDD});
  rt.memcpy_h2d(d_out, h_out, 0, 4);

  LaunchConfig launch;
  launch.grid_dim = Dim3{ 1, 1, 1 };
  launch.block_dim = Dim3{ 1, 1, 1 };
  launch.warp_size = 1;

  auto ka = make_args_from_ptx(ptx, entry,
                              {
                                  {"out", pack_le_u64(d_out)},
                              });

  auto out = rt.run_ptx_kernel_with_args_launch(ptx, isa, desc, ka, launch);
  EXPECT_TRUE(out.sim.completed);
  EXPECT_TRUE(!out.sim.diag.has_value());

  rt.memcpy_d2h(h_out, 0, d_out, 4);
  auto bytes_out = rt.host_read(h_out, 0, 4);
  EXPECT_TRUE(bytes_out.has_value());

  // loop runs 3 times -> r0 == 3
  EXPECT_EQ((*bytes_out)[0], static_cast<std::uint8_t>(3));
  EXPECT_EQ((*bytes_out)[1], static_cast<std::uint8_t>(0));
  EXPECT_EQ((*bytes_out)[2], static_cast<std::uint8_t>(0));
  EXPECT_EQ((*bytes_out)[3], static_cast<std::uint8_t>(0));
}

} // namespace

int main() {
  test_m1_fma_ldst_predication();
  test_m4_bra_loop_uniform();

  if (g_failures) {
    std::cerr << "FAILURES: " << g_failures << "\n";
    return 1;
  }
  std::cout << "OK\n";
  return 0;
}
