#include "protogpu/broker_blocking_executor.h"

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

protogpu_blob_ref append_string(std::vector<std::uint8_t>& arena, const std::string& text) {
  const auto offset = static_cast<std::uint64_t>(arena.size());
  arena.insert(arena.end(), text.begin(), text.end());
  return protogpu_blob_ref{ offset, static_cast<std::uint64_t>(text.size()) };
}

template <typename T>
protogpu_record_ref append_records(std::vector<std::uint8_t>& arena, const std::vector<T>& items) {
  const auto offset = static_cast<std::uint64_t>(arena.size());
  if (!items.empty()) {
    const auto* begin = reinterpret_cast<const std::uint8_t*>(items.data());
    arena.insert(arena.end(), begin, begin + items.size() * sizeof(T));
  }
  return protogpu_record_ref{ offset, static_cast<std::uint32_t>(items.size()), 0 };
}

std::uint32_t unpack_u32_le(const std::vector<std::uint8_t>& bytes) {
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8) |
         (static_cast<std::uint32_t>(bytes[2]) << 16) |
         (static_cast<std::uint32_t>(bytes[3]) << 24);
}

void build_common_payloads(std::vector<std::uint8_t>& arena,
                           protogpu_module_desc& module,
                           std::string& expected_entry) {
  const std::string ptx = R"PTX(
.version 6.4
.target sm_70
.address_size 64

.visible .entry write_value(
    .param .u64 out_ptr,
    .param .u32 value
)
{
  .reg .u64 %rd<2>;
  .reg .u32 %r<2>;

  ld.param.u64 %rd1, [out_ptr];
  ld.param.u32 %r1, [value];
  st.global.u32 [%rd1], %r1;
  ret;
}
)PTX";

  const std::string ptx_isa = R"JSON(
{
  "insts": [
    {"ptx_opcode":"ld","type_mod":"u64","operand_kinds":["reg","symbol"],"ir_op":"ld"},
    {"ptx_opcode":"ld","type_mod":"u32","operand_kinds":["reg","symbol"],"ir_op":"ld"},
    {"ptx_opcode":"st","type_mod":"u32","operand_kinds":["addr","reg"],"ir_op":"st"},
    {"ptx_opcode":"ret","type_mod":"","operand_kinds":[],"ir_op":"ret"}
  ]
}
)JSON";

  const std::string inst_desc = R"JSON(
{
  "insts": [
    {"opcode":"ld","type_mod":"u64","operand_kinds":["reg","symbol"],"uops":[{"kind":"MEM","op":"LD","in":[1],"out":[0]}]},
    {"opcode":"ld","type_mod":"u32","operand_kinds":["reg","symbol"],"uops":[{"kind":"MEM","op":"LD","in":[1],"out":[0]}]},
    {"opcode":"st","type_mod":"u32","operand_kinds":["addr","reg"],"uops":[{"kind":"MEM","op":"ST","in":[0,1],"out":[]}]},
    {"opcode":"ret","type_mod":"","operand_kinds":[],"uops":[{"kind":"CTRL","op":"RET","in":[],"out":[]}]}
  ]
}
)JSON";

  const std::string config = R"JSON(
{
  "sim": {
    "warp_size": 1,
    "max_steps": 1000,
    "deterministic": true
  },
  "observability": {
    "enabled": true,
    "trace_capacity": 256
  }
}
)JSON";

  expected_entry = "write_value";
  module.ptx_text = append_string(arena, ptx);
  module.ptx_isa_json = append_string(arena, ptx_isa);
  module.inst_desc_json = append_string(arena, inst_desc);
  module.config_json = append_string(arena, config);
  module.entry_name = append_string(arena, expected_entry);
}

void test_successful_blocking_execution() {
  std::vector<std::uint8_t> arena;
  protogpu_module_desc module{};
  std::string entry;
  build_common_payloads(arena, module, entry);

  std::vector<protogpu_arg_desc> args = {
    protogpu_arg_desc{ PROTOGPU_ARG_BUFFER_HANDLE, 0, 7, protogpu_blob_ref{ 0, 0 } },
    protogpu_arg_desc{ PROTOGPU_ARG_U32, 0, 123, protogpu_blob_ref{ 0, 0 } },
  };
  std::vector<protogpu_buffer_binding> bindings = {
    protogpu_buffer_binding{ 7, PROTOGPU_BUFFER_OUTPUT, 0 },
  };

  protogpu_submit_job job{};
  job.uapi_version = PROTOGPU_UAPI_VERSION_V1;
  job.module = module;
  job.launch.grid_dim = protogpu_dim3{ 1, 1, 1 };
  job.launch.block_dim = protogpu_dim3{ 1, 1, 1 };
  job.launch.args = append_records(arena, args);
  job.buffer_bindings = append_records(arena, bindings);

  std::vector<protogpu::MappedBuffer> mapped_buffers = {
    protogpu::MappedBuffer{ 7, PROTOGPU_BUFFER_OUTPUT, std::vector<std::uint8_t>(4, 0) },
  };

  auto result = protogpu::execute_blocking_job(job, arena, mapped_buffers);
  EXPECT_EQ(result.status, PROTOGPU_STATUS_OK);
  EXPECT_TRUE(result.diagnostic.empty());
  EXPECT_TRUE(!result.trace_jsonl.empty());
  EXPECT_TRUE(!result.stats_json.empty());
  EXPECT_EQ(unpack_u32_le(mapped_buffers[0].bytes), static_cast<std::uint32_t>(123));
}

void test_missing_buffer_binding_fails() {
  std::vector<std::uint8_t> arena;
  protogpu_module_desc module{};
  std::string entry;
  build_common_payloads(arena, module, entry);

  std::vector<protogpu_arg_desc> args = {
    protogpu_arg_desc{ PROTOGPU_ARG_BUFFER_HANDLE, 0, 99, protogpu_blob_ref{ 0, 0 } },
    protogpu_arg_desc{ PROTOGPU_ARG_U32, 0, 55, protogpu_blob_ref{ 0, 0 } },
  };

  protogpu_submit_job job{};
  job.uapi_version = PROTOGPU_UAPI_VERSION_V1;
  job.module = module;
  job.launch.grid_dim = protogpu_dim3{ 1, 1, 1 };
  job.launch.block_dim = protogpu_dim3{ 1, 1, 1 };
  job.launch.args = append_records(arena, args);
  job.buffer_bindings = append_records(arena, std::vector<protogpu_buffer_binding>{});

  std::vector<protogpu::MappedBuffer> mapped_buffers;
  auto result = protogpu::execute_blocking_job(job, arena, mapped_buffers);
  EXPECT_EQ(result.status, PROTOGPU_STATUS_RUNTIME_ERROR);
  EXPECT_TRUE(result.diagnostic.find("buffer handle not bound") != std::string::npos);
}

}  // namespace

int main() {
  test_successful_blocking_execution();
  test_missing_buffer_binding_fails();

  if (g_failures) {
    std::cerr << "FAILURES: " << g_failures << "\n";
    return 1;
  }
  std::cout << "OK\n";
  return 0;
}