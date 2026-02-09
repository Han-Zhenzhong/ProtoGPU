#pragma once

#include "cudart_shim/fatbin_registry.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace gpusim_cudart_shim {

struct KernelInfo final {
  ModuleId module_id = 0;
  std::string device_name;
};

class KernelRegistry final {
public:
  void register_function(ModuleId module_id, const void* host_fun, const char* device_name);
  void unregister_module(ModuleId module_id);
  const KernelInfo* lookup(const void* host_fun) const;

private:
  std::unordered_map<const void*, KernelInfo> by_host_fun_;
};

} // namespace gpusim_cudart_shim
