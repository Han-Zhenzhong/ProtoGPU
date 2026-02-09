#include "cudart_shim/kernel_registry.h"

namespace gpusim_cudart_shim {

void KernelRegistry::register_function(ModuleId module_id, const void* host_fun, const char* device_name) {
  if (!host_fun || !device_name) return;

  KernelInfo ki;
  ki.module_id = module_id;
  ki.device_name = device_name;
  by_host_fun_[host_fun] = std::move(ki);
}

void KernelRegistry::unregister_module(ModuleId module_id) {
  for (auto it2 = by_host_fun_.begin(); it2 != by_host_fun_.end();) {
    if (it2->second.module_id == module_id) {
      it2 = by_host_fun_.erase(it2);
    } else {
      ++it2;
    }
  }
}

const KernelInfo* KernelRegistry::lookup(const void* host_fun) const {
  auto it = by_host_fun_.find(host_fun);
  if (it == by_host_fun_.end()) return nullptr;
  return &it->second;
}

} // namespace gpusim_cudart_shim
