#pragma once

#include "cudart_shim/cuda_abi_min.h"

#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace gpusim_cudart_shim {

class StreamScheduler final {
public:
  using Command = std::function<void()>;

  // MVP: FIFO per stream, but executes synchronously.
  void submit(cudaStream_t stream, Command cmd);

  // Drains all streams (MVP: no-op because submit executes immediately).
  void drain_all();

private:
  std::mutex mu_;
  std::unordered_map<cudaStream_t, std::vector<Command>> queues_;
};

} // namespace gpusim_cudart_shim
