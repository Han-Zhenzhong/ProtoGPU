#include "cudart_shim/runtime_context.h"

#include "cudart_shim/error_state.h"

namespace gpusim_cudart_shim {

RuntimeContext& RuntimeContext::instance() {
  static RuntimeContext ctx;
  return ctx;
}

bool RuntimeContext::has_fatal_error() const {
  std::scoped_lock lk(mu_);
  return !fatal_error_.empty();
}

bool RuntimeContext::ensure_initialized(std::string& out_error) {
  std::scoped_lock lk(mu_);

  if (!fatal_error_.empty()) {
    out_error = fatal_error_;
    return false;
  }
  if (initialized_) return true;

  EnvOrEmbeddedAssetsProvider provider;
  AssetsText assets;
  if (auto e = provider.load(assets)) {
    fatal_error_ = e->message;
    out_error = fatal_error_;
    return false;
  }

  try {
    auto cfg = gpusim::load_app_config_json_text(assets.config_json);
    warp_size_ = cfg.sim.warp_size;
    profile_ = cfg.sim.profile;
    deterministic_ = cfg.sim.deterministic;

    // Fail-fast: validate the remaining assets are parseable before we report init success.
    // This matches the requirement that env overrides must error immediately if invalid.
    {
      gpusim::PtxIsaRegistry isa;
      isa.load_json_text(assets.ptx_isa_json);
    }
    {
      gpusim::DescriptorRegistry reg;
      reg.load_json_text(assets.inst_desc_json);
    }

    rt_.emplace(cfg);
    assets_ = std::move(assets);
    initialized_ = true;
    return true;
  } catch (const std::exception& ex) {
    fatal_error_ = std::string("RuntimeContext init failed: ") + ex.what();
    out_error = fatal_error_;
    return false;
  }
}

gpusim::Runtime& RuntimeContext::runtime() {
  // Caller must ensure_initialized() first.
  return *rt_;
}

} // namespace gpusim_cudart_shim
