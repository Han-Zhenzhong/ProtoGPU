#pragma once

#include <string>
#include <vector>

namespace gpusim_cudart_shim {

bool ptx_contains_entry_mvp(const std::string& ptx_text, const std::string& entry);

// MVP diagnostics: scan PTX text and return up to max_entries parsed `.entry <name>` names.
std::vector<std::string> ptx_list_entry_names_mvp(const std::string& ptx_text, std::size_t max_entries);

} // namespace gpusim_cudart_shim
