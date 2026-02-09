#include "cudart_shim/error_state.h"

namespace gpusim_cudart_shim {

ErrorState& ErrorState::tls() {
  thread_local ErrorState s;
  return s;
}

void ErrorState::set(cudaError_t err, std::string msg) {
  last_error_ = err;
  last_message_ = std::move(msg);
}

cudaError_t ErrorState::get_and_clear() {
  auto out = last_error_;
  last_error_ = cudaSuccess;
  last_message_.clear();
  return out;
}

} // namespace gpusim_cudart_shim
