#include "gpusim/frontend.h"

#include <stdexcept>

namespace gpusim {

KernelImage Binder::bind_first_kernel(const ModuleImage& m) {
  if (m.kernels.empty()) throw std::runtime_error("Binder: no kernels found");
  return m.kernels.front();
}

KernelTokens Binder::bind_first_kernel(const ModuleTokens& m) {
  if (m.kernels.empty()) throw std::runtime_error("Binder: no kernels found");
  return m.kernels.front();
}

} // namespace gpusim
