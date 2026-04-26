#pragma once

#include "protogpu/uapi.h"

#include <cstdint>
#include <string>
#include <vector>

namespace protogpu {

struct MappedBuffer final {
  std::uint64_t handle = 0;
  std::uint32_t flags = 0;
  std::vector<std::uint8_t> bytes;
};

struct BrokerExecutionResult final {
  protogpu_status_code status = PROTOGPU_STATUS_OK;
  std::uint64_t steps = 0;
  std::string diagnostic;
  std::string trace_jsonl;
  std::string stats_json;
};

BrokerExecutionResult execute_blocking_job(const protogpu_submit_job& job,
                                           const std::vector<std::uint8_t>& arena,
                                           std::vector<MappedBuffer>& mapped_buffers);

}  // namespace protogpu
