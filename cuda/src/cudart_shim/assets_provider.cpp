#include "cudart_shim/assets_provider.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace gpusim_cudart_shim {

namespace embedded {
  const char* config_json();
  const char* ptx_isa_json();
  const char* inst_desc_json();
}

std::optional<AssetsLoadError> EnvOrEmbeddedAssetsProvider::read_optional_path_env(
    const char* env_name,
    std::optional<std::string>& out_path) {
  const char* v = std::getenv(env_name);
  if (!v || *v == '\0') {
    out_path.reset();
    return std::nullopt;
  }
  out_path = std::string(v);
  return std::nullopt;
}

std::optional<AssetsLoadError> EnvOrEmbeddedAssetsProvider::slurp_file(const std::string& path,
                                                                      std::string& out_text) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    return AssetsLoadError{ "cannot open file: " + path };
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  out_text = ss.str();
  return std::nullopt;
}

std::optional<AssetsLoadError> EnvOrEmbeddedAssetsProvider::load(AssetsText& out) {
  std::optional<std::string> cfg_path;
  std::optional<std::string> isa_path;
  std::optional<std::string> desc_path;

  if (auto e = read_optional_path_env("GPUSIM_CONFIG", cfg_path)) return e;
  if (auto e = read_optional_path_env("GPUSIM_PTX_ISA", isa_path)) return e;
  if (auto e = read_optional_path_env("GPUSIM_INST_DESC", desc_path)) return e;

  // Fail-fast rule: if env var is set but unreadable/unparseable -> error.
  if (cfg_path) {
    if (auto e = slurp_file(*cfg_path, out.config_json)) {
      return AssetsLoadError{ "GPUSIM_CONFIG set but " + e->message };
    }
  } else {
    out.config_json = embedded::config_json();
  }

  if (isa_path) {
    if (auto e = slurp_file(*isa_path, out.ptx_isa_json)) {
      return AssetsLoadError{ "GPUSIM_PTX_ISA set but " + e->message };
    }
  } else {
    out.ptx_isa_json = embedded::ptx_isa_json();
  }

  if (desc_path) {
    if (auto e = slurp_file(*desc_path, out.inst_desc_json)) {
      return AssetsLoadError{ "GPUSIM_INST_DESC set but " + e->message };
    }
  } else {
    out.inst_desc_json = embedded::inst_desc_json();
  }

  return std::nullopt;
}

} // namespace gpusim_cudart_shim
