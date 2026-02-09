#include "cudart_shim/fatbin_registry.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <optional>

#include <new>

namespace gpusim_cudart_shim {

namespace {

// Known CUDA fatbin wrapper magic (nvcc-compatible wrappers).
constexpr std::uint32_t kFatbinWrapperMagic = 0x466243B1u;

struct FatbinWrapper64 final {
  std::uint32_t magic;
  std::uint32_t version;
  const void* data;
  const void* filename_or_elf;
};

// In-memory fatbin payload starts with this header.
// Layout matches the blob dumped from clang/nvcc fatbins in practice.
struct FatbinHeader final {
  std::uint32_t magic;      // 0xBA55ED50
  std::uint16_t version;    // 1
  std::uint16_t header_size; // typically 16
  std::uint64_t fat_size;   // size of payload following this header
};

struct FatbinDataHeader final {
  std::uint16_t kind;        // 1=PTX, 2=ELF
  std::uint16_t version;
  std::uint32_t header_size; // bytes to skip to reach data
  std::uint64_t data_size;   // bytes of data
};

constexpr std::uint32_t kFatbinMagic = 0xBA55ED50u;

static std::optional<std::pair<const std::uint8_t*, std::size_t>> mapped_region_for_ptr(const void* p) {
#if defined(__linux__)
  const auto addr = reinterpret_cast<std::uintptr_t>(p);
  std::ifstream in("/proc/self/maps");
  if (!in) return std::nullopt;

  std::string line;
  while (std::getline(in, line)) {
    // Format: start-end perms offset dev inode pathname
    // Example: 7f2c0d000000-7f2c0d021000 r--p 00000000 ...
    std::uintptr_t start = 0;
    std::uintptr_t end = 0;
    if (std::sscanf(line.c_str(), "%lx-%lx", &start, &end) != 2) continue;
    if (addr >= start && addr < end && end > start) {
      const auto* base = reinterpret_cast<const std::uint8_t*>(start);
      const auto len = static_cast<std::size_t>(end - start);
      return std::make_pair(base, len);
    }
  }
#endif
  (void)p;
  return std::nullopt;
}

static std::optional<std::size_t> find_needle(const std::uint8_t* begin,
                                              const std::uint8_t* end,
                                              const char* needle) {
  if (!begin || !end || begin >= end || !needle || *needle == '\0') return std::nullopt;
  const std::size_t nlen = std::strlen(needle);
  if (nlen == 0) return std::nullopt;

  const auto* it = std::search(begin, end, needle, needle + nlen);
  if (it == end) return std::nullopt;
  return static_cast<std::size_t>(it - begin);
}

static bool looks_like_ptx_header(const std::string& s) {
  // For MVP, require at least a header + some kernel content.
  return s.find(".version") != std::string::npos && s.find(".target") != std::string::npos &&
         s.find(".entry") != std::string::npos;
}

static bool is_ptx_ident_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$' || c == '.';
}

static std::string sanitize_identifier_token(const std::string& tok) {
  std::string out;
  out.reserve(tok.size());
  bool last_us = false;
  for (char c : tok) {
    const bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$' || c == '.';
    if (ok) {
      out.push_back(c);
      last_us = false;
    } else {
      if (!last_us) out.push_back('_');
      last_us = true;
    }
  }
  // Avoid empty identifiers.
  if (out.empty()) out = "_";
  return out;
}

static std::string deobfuscate_ptx_text_mvp(const std::uint8_t* data, std::size_t n) {
  // Many toolchains store PTX as mostly-ASCII but with embedded non-text bytes.
  // MVP strategy: keep ASCII printables and whitespace, replace others with spaces, strip quotes,
  // normalize common directive prefixes, drop .loc lines, and sanitize parameter symbol tokens.
  std::string text;
  text.reserve(n);

  for (std::size_t i = 0; i < n; i++) {
    const unsigned char c = static_cast<unsigned char>(data[i]);
    if (c == '\n' || c == '\r' || c == '\t') {
      text.push_back(static_cast<char>(c));
    } else if (c >= 0x20 && c <= 0x7Eu) {
      const char ch = static_cast<char>(c);
      if (ch == '"') {
        text.push_back(' ');
      } else {
        text.push_back(ch);
      }
    } else {
      text.push_back(' ');
    }
  }

  // Line-based cleanup.
  std::string cleaned;
  cleaned.reserve(text.size());
  std::size_t line_begin = 0;
  while (line_begin < text.size()) {
    std::size_t line_end = text.find('\n', line_begin);
    if (line_end == std::string::npos) line_end = text.size();
    std::string line = text.substr(line_begin, line_end - line_begin);

    // Trim leading spaces.
    std::size_t i = 0;
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) i++;
    const std::string_view sv(line.c_str() + i, line.size() - i);

    // Drop .loc lines (they are optional and often get mangled).
    if (sv.rfind(".loc", 0) == 0 || sv.rfind(".loc ", 0) == 0 || sv.rfind(".locI", 0) == 0) {
      // skip
    } else {
      // Fix common directive corruption: "$visible" at line start.
      if (sv.rfind("$visible", 0) == 0) {
        line.replace(i, 8, ".visible");
      } else if (sv.rfind("visible", 0) == 0) {
        line.insert(i, ".");
      }

      // Fix type tokens like ".u64%" / ".u32%".
      {
        std::size_t p = 0;
        while ((p = line.find(".u64%", p)) != std::string::npos) {
          line.replace(p, 5, ".u64 ");
          p += 5;
        }
        p = 0;
        while ((p = line.find(".u32%", p)) != std::string::npos) {
          line.replace(p, 5, ".u32 ");
          p += 5;
        }
      }

      // Sanitize identifier tokens in `.param` declarations and `ld.param/st.param` brackets.
      if (line.find(".param") != std::string::npos) {
        // Replace any bracketed symbol name [ ... ] tokens and param name tokens.
        // 1) sanitize things inside [ ... ]
        {
          std::size_t lb = 0;
          while ((lb = line.find('[', lb)) != std::string::npos) {
            const std::size_t rb = line.find(']', lb + 1);
            if (rb == std::string::npos) break;
            std::string inside = line.substr(lb + 1, rb - (lb + 1));
            inside = sanitize_identifier_token(inside);
            line.replace(lb + 1, rb - (lb + 1), inside);
            lb = rb + 1;
          }
        }
        // 2) sanitize tokens after type in `.param .u64 <name>` forms.
        {
          const std::size_t type_pos = line.find(".u64");
          const std::size_t type_pos2 = line.find(".u32");
          const std::size_t tp = (type_pos == std::string::npos) ? type_pos2 : type_pos;
          if (tp != std::string::npos) {
            std::size_t j = tp + 4;
            while (j < line.size() && !std::isspace(static_cast<unsigned char>(line[j]))) j++;
            while (j < line.size() && std::isspace(static_cast<unsigned char>(line[j]))) j++;
            std::size_t k = j;
            while (k < line.size() && is_ptx_ident_char(line[k])) k++;
            if (k > j) {
              const std::string name = line.substr(j, k - j);
              const std::string sn = sanitize_identifier_token(name);
              line.replace(j, k - j, sn);
            }
          }
        }
      }

      cleaned.append(line);
      cleaned.push_back('\n');
    }

    line_begin = (line_end < text.size()) ? (line_end + 1) : text.size();
  }

  return cleaned;
}

static void maybe_dump_texts(const std::vector<std::string>& texts, const char* env_name, const char* suffix) {
  const char* out = std::getenv(env_name);
  if (!out || *out == '\0') return;

  // Treat env value as a path prefix.
  for (std::size_t i = 0; i < texts.size(); i++) {
    std::string path(out);
    path += "_";
    path += std::to_string(i);
    path += suffix;
    std::ofstream f(path, std::ios::binary);
    if (!f) continue;
    f.write(texts[i].data(), static_cast<std::streamsize>(texts[i].size()));
  }
}

static void maybe_dump_bytes(const void* p, std::size_t n, const char* env_name) {
  const char* out = std::getenv(env_name);
  if (!out || *out == '\0') return;
  std::ofstream f(out, std::ios::binary);
  if (!f) return;
  f.write(reinterpret_cast<const char*>(p), static_cast<std::streamsize>(n));
}

} // namespace

std::vector<std::string> FatbinRegistry::extract_ptx_texts_mvp(void* fat_cubin) {
  std::vector<std::string> out;
  if (!fat_cubin) return out;

  // Optional escape hatch: load PTX from a file.
  // This is useful when the toolchain stores PTX in a tokenized/compressed form in the fatbin.
  if (const char* p = std::getenv("GPUSIM_CUDART_SHIM_PTX_OVERRIDE")) {
    if (p && *p != '\0') {
      std::ifstream f(p, std::ios::binary);
      if (f) {
        std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (looks_like_ptx_header(s)) {
          out.push_back(std::move(s));
          return out;
        }
      }
    }
  }

  // Try interpreting fat_cubin as a wrapper; if it matches, scan the payload.
  const void* scan_ptr = fat_cubin;
  const auto* w = reinterpret_cast<const FatbinWrapper64*>(fat_cubin);
  if (w->magic == kFatbinWrapperMagic && w->data) {
    scan_ptr = w->data;
  }

  // Find a mapped region containing scan_ptr so we never probe unmapped memory.
  const auto region = mapped_region_for_ptr(scan_ptr);
  const auto* scan_begin = reinterpret_cast<const std::uint8_t*>(scan_ptr);
  const auto* region_begin = region ? region->first : scan_begin;
  const auto region_len = region ? region->second : static_cast<std::size_t>(1u << 20); // fallback cap 1MiB
  const auto* region_end = region_begin + region_len;

  // IMPORTANT: parse from scan_ptr (fatbin payload), not from the start of the mapped region.
  const auto* begin = scan_begin;
  const auto* end = (scan_begin < region_end) ? region_end : scan_begin;
  const auto total_len = static_cast<std::size_t>(end - begin);

  // Optional: dump the wrapper and/or a chunk of bytes for offline inspection.
  maybe_dump_bytes(fat_cubin, sizeof(FatbinWrapper64), "GPUSIM_CUDART_SHIM_DUMP_FATBIN_WRAPPER");
  {
    const std::size_t cap = std::min<std::size_t>(total_len, 4u * 1024u * 1024u);
    maybe_dump_bytes(scan_ptr, cap, "GPUSIM_CUDART_SHIM_DUMP_FATBIN");
  }

  // Parse actual fatbin container: header + data entries.
  if (static_cast<std::size_t>(end - begin) < sizeof(FatbinHeader)) return out;
  const auto* fh = reinterpret_cast<const FatbinHeader*>(begin);
  if (fh->magic != kFatbinMagic) {
    // Fallback to old heuristic if this isn't a fatbin (should be rare).
    std::size_t cursor = 0;
    while (cursor < total_len) {
      const auto* cur = begin + cursor;
      const auto rel = find_needle(cur, end, ".version");
      if (!rel) break;
      const auto* ptx_start = cur + *rel;
      // Heuristic: NUL-terminated string.
      const auto* nul = static_cast<const std::uint8_t*>(std::memchr(ptx_start, 0, static_cast<std::size_t>(end - ptx_start)));
      if (!nul) break;
      const std::size_t len = static_cast<std::size_t>(nul - ptx_start);
      if (len > 0 && len < (16u * 1024u * 1024u)) {
        std::string ptx(reinterpret_cast<const char*>(ptx_start), len);
        if (ptx.find(".entry") != std::string::npos) out.push_back(std::move(ptx));
      }
      cursor = static_cast<std::size_t>((nul + 1) - begin);
    }
  } else {
    const std::size_t blob_size = static_cast<std::size_t>(fh->header_size) + static_cast<std::size_t>(fh->fat_size);
    const std::size_t safe_blob_size = std::min<std::size_t>(blob_size, static_cast<std::size_t>(end - begin));
    std::size_t off = fh->header_size;
    while (off + sizeof(FatbinDataHeader) <= safe_blob_size) {
      const auto* dh = reinterpret_cast<const FatbinDataHeader*>(begin + off);
      if (dh->kind == 0) break;
      if (dh->header_size < sizeof(FatbinDataHeader)) break;

      const std::size_t data_off = off + static_cast<std::size_t>(dh->header_size);
      const std::size_t data_end = data_off + static_cast<std::size_t>(dh->data_size);
      if (data_end > safe_blob_size) break;

      if (dh->kind == 1) {
        const auto* p = begin + data_off;
        const std::size_t n = static_cast<std::size_t>(dh->data_size);
        std::string ptx = deobfuscate_ptx_text_mvp(p, n);
        if (looks_like_ptx_header(ptx)) {
          out.push_back(std::move(ptx));
        }
      }

      // Next entry: entries are packed as header + data.
      off = data_end;
      // 8-byte alignment is common; be forgiving.
      while (off < safe_blob_size && (off % 8) != 0) off++;
    }
  }

  // Deduplicate exact matches.
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());

  // Optional: dump extracted PTX(s).
  maybe_dump_texts(out, "GPUSIM_CUDART_SHIM_DUMP_PTX", ".ptx");
  return out;
}

void** FatbinRegistry::register_fatbin(void* fat_cubin) {
  auto texts = extract_ptx_texts_mvp(fat_cubin);

  const auto id = next_id_++;
  FatbinModule m;
  m.id = id;
  m.ptx_texts = std::move(texts);
  modules_.emplace(id, std::move(m));

  // Stable unique handle: allocate a small token.
  auto* token = new (std::nothrow) ModuleId(id);
  auto** handle = reinterpret_cast<void**>(token);
  handle_to_id_[handle] = id;
  return handle;
}

void FatbinRegistry::unregister_fatbin(void** handle) {
  if (!handle) return;
  auto it = handle_to_id_.find(handle);
  if (it != handle_to_id_.end()) {
    modules_.erase(it->second);
    handle_to_id_.erase(it);
  }
  auto* token = reinterpret_cast<ModuleId*>(handle);
  delete token;
}

const FatbinModule* FatbinRegistry::lookup(ModuleId id) const {
  auto it = modules_.find(id);
  if (it == modules_.end()) return nullptr;
  return &it->second;
}

const FatbinModule* FatbinRegistry::lookup_by_handle(void** handle) const {
  if (!handle) return nullptr;
  auto it = handle_to_id_.find(handle);
  if (it == handle_to_id_.end()) return nullptr;
  return lookup(it->second);
}

} // namespace gpusim_cudart_shim
