#include "cudart_shim/ptx_entry_validate.h"

#include <cctype>
#include <optional>
#include <string_view>

namespace gpusim_cudart_shim {

static bool is_ident_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$' || c == '.';
}

static std::size_t skip_ws(std::string_view s, std::size_t i) {
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++;
  return i;
}

static std::optional<std::string> parse_entry_name_at(std::string_view s, std::size_t entry_kw_pos) {
  // entry_kw_pos points at '.' in ".entry".
  constexpr std::string_view kw = ".entry";
  if (entry_kw_pos >= s.size()) return std::nullopt;
  if (entry_kw_pos + kw.size() > s.size()) return std::nullopt;
  if (s.substr(entry_kw_pos, kw.size()) != kw) return std::nullopt;

  // Require identifier boundary before ".entry".
  if (entry_kw_pos > 0 && is_ident_char(s[entry_kw_pos - 1])) return std::nullopt;

  std::size_t i = entry_kw_pos + kw.size();
  i = skip_ws(s, i);

  if (i >= s.size() || !is_ident_char(s[i])) return std::nullopt;
  const std::size_t name_begin = i;
  while (i < s.size() && is_ident_char(s[i])) i++;
  const std::size_t name_end = i;

  if (name_end <= name_begin) return std::nullopt;
  return std::string(s.substr(name_begin, name_end - name_begin));
}

bool ptx_contains_entry_mvp(const std::string& ptx_text, const std::string& entry) {
  if (entry.empty()) return false;

  // MVP: cheap textual scan. Parse each `.entry <name>` and compare exactly.
  std::string_view s(ptx_text);
  constexpr std::string_view needle = ".entry";
  std::size_t pos = 0;
  while (true) {
    pos = s.find(needle, pos);
    if (pos == std::string_view::npos) return false;

    if (auto name = parse_entry_name_at(s, pos)) {
      if (*name == entry) return true;
    }
    pos += needle.size();
  }
}

std::vector<std::string> ptx_list_entry_names_mvp(const std::string& ptx_text, std::size_t max_entries) {
  std::vector<std::string> out;
  if (max_entries == 0) return out;

  std::string_view s(ptx_text);
  constexpr std::string_view needle = ".entry";
  std::size_t pos = 0;
  while (out.size() < max_entries) {
    pos = s.find(needle, pos);
    if (pos == std::string_view::npos) break;

    if (auto name = parse_entry_name_at(s, pos)) {
      out.push_back(std::move(*name));
    }
    pos += needle.size();
  }
  return out;
}

} // namespace gpusim_cudart_shim
