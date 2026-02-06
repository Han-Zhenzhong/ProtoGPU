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

KernelImage Binder::bind_kernel_by_name(const ModuleImage& m, const std::string& entry) {
  for (const auto& k : m.kernels) {
    if (k.name == entry) return k;
  }
  throw std::runtime_error("Binder: kernel entry not found: " + entry);
}

KernelTokens Binder::bind_kernel_by_name(const ModuleTokens& m, const std::string& entry) {
  for (const auto& k : m.kernels) {
    if (k.name == entry) return k;
  }
  throw std::runtime_error("Binder: kernel entry not found: " + entry);
}

} // namespace gpusim
