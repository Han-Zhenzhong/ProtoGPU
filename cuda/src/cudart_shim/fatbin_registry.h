#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace gpusim_cudart_shim {

using ModuleId = std::uint64_t;

struct FatbinModule final {
  ModuleId id = 0;
  std::vector<std::string> ptx_texts;
};

class FatbinRegistry final {
public:
  void** register_fatbin(void* fat_cubin);
  void unregister_fatbin(void** handle);

  const FatbinModule* lookup(ModuleId id) const;
  const FatbinModule* lookup_by_handle(void** handle) const;

private:
  ModuleId next_id_ = 1;
  std::unordered_map<ModuleId, FatbinModule> modules_;
  std::unordered_map<void**, ModuleId> handle_to_id_;

  // TODO(M4): fatbin->PTX extraction.
  static std::vector<std::string> extract_ptx_texts_mvp(void* fat_cubin);
};

} // namespace gpusim_cudart_shim
