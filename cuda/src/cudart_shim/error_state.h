#pragma once

#include "cudart_shim/cuda_abi_min.h"

#include <string>

namespace gpusim_cudart_shim {

class ErrorState final {
public:
  static ErrorState& tls();

  void set(cudaError_t err, std::string msg = {});
  cudaError_t get_and_clear();
  cudaError_t peek() const { return last_error_; }

  const std::string& last_message() const { return last_message_; }

private:
  cudaError_t last_error_ = cudaSuccess;
  std::string last_message_;
};

} // namespace gpusim_cudart_shim
