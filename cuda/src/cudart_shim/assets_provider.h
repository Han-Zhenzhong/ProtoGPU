#pragma once

#include <optional>
#include <string>

namespace gpusim_cudart_shim {

struct AssetsText final {
  std::string config_json;
  std::string ptx_isa_json;
  std::string inst_desc_json;
};

struct AssetsLoadError final {
  std::string message;
};

class AssetsProvider {
public:
  virtual ~AssetsProvider() = default;
  virtual std::optional<AssetsLoadError> load(AssetsText& out) = 0;
};

class EnvOrEmbeddedAssetsProvider final : public AssetsProvider {
public:
  std::optional<AssetsLoadError> load(AssetsText& out) override;

private:
  static std::optional<AssetsLoadError> read_optional_path_env(const char* env_name,
                                                              std::optional<std::string>& out_path);
  static std::optional<AssetsLoadError> slurp_file(const std::string& path, std::string& out_text);
};

} // namespace gpusim_cudart_shim
