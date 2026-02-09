#include "cudart_shim/launch_config_stack.h"

namespace gpusim_cudart_shim {

LaunchConfigStack& LaunchConfigStack::tls() {
  thread_local LaunchConfigStack s;
  return s;
}

void LaunchConfigStack::push(const CallConfig& c) {
  stack_.push_back(c);
}

std::optional<CallConfig> LaunchConfigStack::pop() {
  if (stack_.empty()) return std::nullopt;
  auto c = stack_.back();
  stack_.pop_back();
  return c;
}

} // namespace gpusim_cudart_shim
