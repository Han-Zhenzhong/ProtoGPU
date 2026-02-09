#pragma once

#include "cudart_shim/cuda_abi_min.h"

#include <optional>
#include <vector>

namespace gpusim_cudart_shim {

struct CallConfig final {
  dim3 grid;
  dim3 block;
  std::size_t shared = 0;
  cudaStream_t stream = nullptr;
};

class LaunchConfigStack final {
public:
  static LaunchConfigStack& tls();

  void push(const CallConfig& c);
  std::optional<CallConfig> pop();

private:
  std::vector<CallConfig> stack_;
};

} // namespace gpusim_cudart_shim
