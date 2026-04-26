#include "protogpu_ioctl.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>

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

std::uint32_t unpack_u32_le(const std::uint8_t* bytes) {
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8) |
         (static_cast<std::uint32_t>(bytes[2]) << 16) |
         (static_cast<std::uint32_t>(bytes[3]) << 24);
}

void test_device_node_roundtrip() {
  const char* node = "/dev/protogpu0";
  int fd = ::open(node, O_RDWR | O_CLOEXEC);
  if (fd < 0) {
    if (errno == ENOENT || errno == ENODEV) {
      std::cout << "SKIP: /dev/protogpu0 is not available\n";
      return;
    }
    fail(std::string("open failed: ") + std::strerror(errno), __FILE__, __LINE__);
    return;
  }

  protogpu_ioctl_create_ctx create_ctx{};
  create_ctx.uapi_version = PROTOGPU_IOCTL_UAPI_VERSION_V1;
  EXPECT_TRUE(::ioctl(fd, PROTOGPU_IOCTL_CREATE_CTX, &create_ctx) == 0);
  EXPECT_TRUE(create_ctx.ctx_id != 0);

  protogpu_ioctl_set_broker set_broker{};
  std::snprintf(set_broker.unix_socket_path,
                sizeof(set_broker.unix_socket_path),
                "%s",
                "/tmp/protogpu-broker.sock");
  EXPECT_TRUE(::ioctl(fd, PROTOGPU_IOCTL_SET_BROKER, &set_broker) == 0);

  protogpu_ioctl_alloc_bo alloc{};
  alloc.ctx_id = create_ctx.ctx_id;
  alloc.bytes = 4;
  alloc.align = 4;
  alloc.flags = PROTOGPU_BUFFER_OUTPUT;
  EXPECT_TRUE(::ioctl(fd, PROTOGPU_IOCTL_ALLOC_BO, &alloc) == 0);
  EXPECT_TRUE(alloc.handle != 0);

  void* mapped = ::mmap(nullptr,
                        static_cast<size_t>(alloc.bytes),
                        PROT_READ | PROT_WRITE,
                        MAP_SHARED,
                        fd,
                        static_cast<off_t>(alloc.mmap_offset));
  EXPECT_TRUE(mapped != MAP_FAILED);
  if (mapped != MAP_FAILED) {
    std::memset(mapped, 0, static_cast<size_t>(alloc.bytes));
  }

  std::vector<std::uint8_t> arena;
  const auto ptx_ref = append_string(arena, R"PTX(
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
)PTX");
  const auto isa_ref = append_string(arena, R"JSON(
{
  "insts": [
    {"ptx_opcode":"ld","type_mod":"u64","operand_kinds":["reg","symbol"],"ir_op":"ld"},
    {"ptx_opcode":"ld","type_mod":"u32","operand_kinds":["reg","symbol"],"ir_op":"ld"},
    {"ptx_opcode":"st","type_mod":"u32","operand_kinds":["addr","reg"],"ir_op":"st"},
    {"ptx_opcode":"ret","type_mod":"","operand_kinds":[],"ir_op":"ret"}
  ]
}
)JSON");
  const auto desc_ref = append_string(arena, R"JSON(
{
  "insts": [
    {"opcode":"ld","type_mod":"u64","operand_kinds":["reg","symbol"],"uops":[{"kind":"MEM","op":"LD","in":[1],"out":[0]}]},
    {"opcode":"ld","type_mod":"u32","operand_kinds":["reg","symbol"],"uops":[{"kind":"MEM","op":"LD","in":[1],"out":[0]}]},
    {"opcode":"st","type_mod":"u32","operand_kinds":["addr","reg"],"uops":[{"kind":"MEM","op":"ST","in":[0,1],"out":[]}]},
    {"opcode":"ret","type_mod":"","operand_kinds":[],"uops":[{"kind":"CTRL","op":"RET","in":[],"out":[]}]}
  ]
}
)JSON");
  const auto cfg_ref = append_string(arena, "{\"sim\":{\"warp_size\":1,\"max_steps\":64},\"observability\":{\"enabled\":true,\"trace_capacity\":64}}");
  const auto entry_ref = append_string(arena, "write_value");
  const auto args_ref = append_records(arena, std::vector<protogpu_arg_desc>{
    protogpu_arg_desc{ PROTOGPU_ARG_BUFFER_HANDLE, 0, alloc.handle, protogpu_blob_ref{ 0, 0 } },
    protogpu_arg_desc{ PROTOGPU_ARG_U32, 0, 123, protogpu_blob_ref{ 0, 0 } },
  });
  const auto bindings_ref = append_records(arena, std::vector<protogpu_buffer_binding>{
    protogpu_buffer_binding{ alloc.handle, PROTOGPU_BUFFER_OUTPUT, 0 },
  });

  protogpu_ioctl_submit_job submit{};
  submit.ctx_id = create_ctx.ctx_id;
  submit.job_id = 0;
  submit.job.uapi_version = PROTOGPU_UAPI_VERSION_V1;
  submit.job.module = protogpu_module_desc{ ptx_ref, isa_ref, desc_ref, cfg_ref, entry_ref };
  submit.job.launch.grid_dim = protogpu_dim3{ 1, 1, 1 };
  submit.job.launch.block_dim = protogpu_dim3{ 1, 1, 1 };
  submit.job.launch.args = args_ref;
  submit.job.buffer_bindings = bindings_ref;
  submit.arena_user_ptr = reinterpret_cast<std::uint64_t>(arena.data());
  submit.arena_bytes = arena.size();
  EXPECT_TRUE(::ioctl(fd, PROTOGPU_IOCTL_SUBMIT_JOB, &submit) == 0);
  EXPECT_TRUE(submit.job_id != 0);

  protogpu_ioctl_wait_job wait{};
  wait.ctx_id = create_ctx.ctx_id;
  wait.job_id = submit.job_id;
  wait.timeout_ns = 0;
  EXPECT_TRUE(::ioctl(fd, PROTOGPU_IOCTL_WAIT_JOB, &wait) == 0);
  if (wait.status != static_cast<std::uint32_t>(PROTOGPU_STATUS_OK)) {
    std::cerr << "wait.status=" << wait.status << " (expected "
              << static_cast<std::uint32_t>(PROTOGPU_STATUS_OK) << ")\n";
  }
  EXPECT_EQ(wait.status, static_cast<std::uint32_t>(PROTOGPU_STATUS_OK));

  char diag[256] = {0};
  protogpu_ioctl_read_diag read_diag{};
  read_diag.ctx_id = create_ctx.ctx_id;
  read_diag.job_id = submit.job_id;
  read_diag.user_ptr = reinterpret_cast<std::uint64_t>(diag);
  read_diag.capacity = sizeof(diag) - 1;
  EXPECT_TRUE(::ioctl(fd, PROTOGPU_IOCTL_READ_DIAG, &read_diag) == 0);
  EXPECT_TRUE(read_diag.bytes_written <= sizeof(diag) - 1);
  diag[read_diag.bytes_written < sizeof(diag) ? read_diag.bytes_written : sizeof(diag) - 1] = '\0';
  if (wait.status != static_cast<std::uint32_t>(PROTOGPU_STATUS_OK)) {
    std::cerr << "diag: " << diag << "\n";
  }

  if (mapped != MAP_FAILED) {
    EXPECT_EQ(unpack_u32_le(static_cast<const std::uint8_t*>(mapped)), static_cast<std::uint32_t>(123));
    ::munmap(mapped, static_cast<size_t>(alloc.bytes));
  }

  protogpu_ioctl_free_bo free_bo{};
  free_bo.ctx_id = create_ctx.ctx_id;
  free_bo.handle = alloc.handle;
  EXPECT_TRUE(::ioctl(fd, PROTOGPU_IOCTL_FREE_BO, &free_bo) == 0);

  protogpu_ioctl_destroy_ctx destroy_ctx{};
  destroy_ctx.ctx_id = create_ctx.ctx_id;
  EXPECT_TRUE(::ioctl(fd, PROTOGPU_IOCTL_DESTROY_CTX, &destroy_ctx) == 0);

  ::close(fd);
}

}  // namespace

int main() {
  test_device_node_roundtrip();

  if (g_failures) {
    std::cerr << "FAILURES: " << g_failures << "\n";
    return 1;
  }
  std::cout << "OK\n";
  return 0;
}
