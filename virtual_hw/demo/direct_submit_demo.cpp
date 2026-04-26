#include "protogpu/broker_blocking_executor.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

template <typename T>
protogpu_blob_ref append_object(std::vector<std::uint8_t>& arena, const T& object) {
  const auto offset = static_cast<std::uint64_t>(arena.size());
  const auto* begin = reinterpret_cast<const std::uint8_t*>(&object);
  arena.insert(arena.end(), begin, begin + sizeof(T));
  return protogpu_blob_ref{ offset, sizeof(T) };
}

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

}  // namespace

int main() {
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
    "max_steps": 1000
  },
  "observability": {
    "enabled": true,
    "trace_capacity": 256
  }
}
)JSON";

  std::vector<std::uint8_t> arena;
  const auto ptx_ref = append_string(arena, ptx);
  const auto ptx_isa_ref = append_string(arena, ptx_isa);
  const auto inst_desc_ref = append_string(arena, inst_desc);
  const auto config_ref = append_string(arena, config);
  const auto entry_ref = append_string(arena, "write_value");

  std::vector<protogpu_arg_desc> args = {
    protogpu_arg_desc{ PROTOGPU_ARG_BUFFER_HANDLE, 0, 1, protogpu_blob_ref{ 0, 0 } },
    protogpu_arg_desc{ PROTOGPU_ARG_U32, 0, 99, protogpu_blob_ref{ 0, 0 } },
  };
  const auto args_ref = append_records(arena, args);

  std::vector<protogpu_buffer_binding> bindings = {
    protogpu_buffer_binding{ 1, PROTOGPU_BUFFER_OUTPUT, 0 },
  };
  const auto bindings_ref = append_records(arena, bindings);

  protogpu_submit_job job{};
  job.uapi_version = PROTOGPU_UAPI_VERSION_V1;
  job.module = protogpu_module_desc{ ptx_ref, ptx_isa_ref, inst_desc_ref, config_ref, entry_ref };
  job.launch.grid_dim = protogpu_dim3{ 1, 1, 1 };
  job.launch.block_dim = protogpu_dim3{ 1, 1, 1 };
  job.launch.shared_mem_bytes = 0;
  job.launch.args = args_ref;
  job.buffer_bindings = bindings_ref;

  std::vector<protogpu::MappedBuffer> buffers = {
    protogpu::MappedBuffer{ 1, PROTOGPU_BUFFER_OUTPUT, std::vector<std::uint8_t>(4, 0) },
  };

  auto result = protogpu::execute_blocking_job(job, arena, buffers);
  if (result.status != PROTOGPU_STATUS_OK) {
    std::cerr << "broker execution failed: " << result.diagnostic << "\n";
    return 1;
  }

  std::cout << "direct-submit result: " << unpack_u32_le(buffers[0].bytes) << "\n";
  return 0;
}
