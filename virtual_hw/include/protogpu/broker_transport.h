#pragma once

#include "protogpu/broker_blocking_executor.h"

#include <string>
#include <vector>

namespace protogpu {

// Send one blocking job to a broker daemon over a Unix domain socket.
BrokerExecutionResult execute_blocking_job_remote(const std::string& socket_path,
                                                  const protogpu_submit_job& job,
                                                  const std::vector<std::uint8_t>& arena,
                                                  std::vector<MappedBuffer>& mapped_buffers);

// Run a broker daemon loop: one job per connection, sequential processing.
int run_broker_daemon(const std::string& socket_path);

}  // namespace protogpu
