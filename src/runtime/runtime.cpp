#include "gpusim/runtime.h"

#include "gpusim/json.h"

#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace gpusim {

static std::optional<Diagnostic> validate_launch(const LaunchConfig& launch, const std::string& kernel_name);

static RunOutputs run_kernel_tokens_impl(const AppConfig& cfg,
                                        ObsControl& obs,
                                        AddrSpaceManager& mem,
                                        const KernelTokens& kernel_tokens,
                                        const PtxIsaRegistry& isa,
                                        DescriptorRegistry reg,
                                        const KernelArgs* args,
                                        const LaunchConfig& launch) {
  RunOutputs out;

  PtxIsaMapper mapper;
  KernelImage kernel;
  kernel.name = kernel_tokens.name;
  kernel.reg_u32_count = kernel_tokens.reg_u32_count;
  kernel.reg_u64_count = kernel_tokens.reg_u64_count;
  kernel.params = kernel_tokens.params;

  if (auto diag = mapper.map_kernel(kernel_tokens.insts, isa, kernel.insts)) {
    out.sim.diag = *diag;
    out.sim.completed = false;
    out.sim.steps = 0;
    return out;
  }

  if (auto d = validate_launch(launch, kernel.name)) {
    out.sim.diag = *d;
    out.sim.completed = false;
    out.sim.steps = 0;
    return out;
  }

  // Ensure params never implicitly carry over across runs.
  mem.set_param_layout(kernel.params);
  if (args) {
    mem.set_param_blob(args->blob);
  } else {
    mem.set_param_blob({});
  }

  SimtExecutor exec(cfg.sim, std::move(reg), obs, mem);
  out.sim = exec.run(kernel, launch);
  return out;
}

static std::optional<Diagnostic> validate_launch(const LaunchConfig& launch, const std::string& kernel_name) {
  if (launch.grid_dim.x == 0 || launch.grid_dim.y == 0 || launch.grid_dim.z == 0 ||
      launch.block_dim.x == 0 || launch.block_dim.y == 0 || launch.block_dim.z == 0) {
    return Diagnostic{ "runtime", "E_LAUNCH_DIM", "grid/block dims must be >= 1", std::nullopt, kernel_name, std::nullopt };
  }

  auto mul_overflow = [](std::uint64_t a, std::uint64_t b, std::uint64_t& out) -> bool {
    if (a == 0 || b == 0) {
      out = 0;
      return false;
    }
    if (a > (std::numeric_limits<std::uint64_t>::max)() / b) return true;
    out = a * b;
    return false;
  };

  std::uint64_t xy = 0;
  std::uint64_t threads_per_block = 0;
  const bool of1 = mul_overflow(launch.block_dim.x, launch.block_dim.y, xy);
  const bool of2 = mul_overflow(xy, launch.block_dim.z, threads_per_block);
  if (of1 || of2 || threads_per_block == 0) {
    return Diagnostic{ "runtime", "E_LAUNCH_OVERFLOW", "threads_per_block overflow", std::nullopt, kernel_name, std::nullopt };
  }
  return std::nullopt;
}

static std::string slurp(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("cannot open " + path);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

AppConfig load_app_config_json_text(const std::string& text) {
  AppConfig cfg;
  auto root = gpusim::json::parse(text);
  const auto& o = root.as_object();

  if (auto* sim = o.find("sim") != o.end() ? &o.at("sim") : nullptr) {
    const auto& so = sim->as_object();
    if (auto* ws = so.find("warp_size") != so.end() ? &so.at("warp_size") : nullptr) {
      cfg.sim.warp_size = static_cast<std::uint32_t>(ws->as_u64());
    }
    if (auto* ms = so.find("max_steps") != so.end() ? &so.at("max_steps") : nullptr) {
      cfg.sim.max_steps = ms->as_u64();
    }

    if (auto* sc = so.find("sm_count") != so.end() ? &so.at("sm_count") : nullptr) {
      cfg.sim.sm_count = static_cast<std::uint32_t>(sc->as_u64());
    }
    if (auto* par = so.find("parallel") != so.end() ? &so.at("parallel") : nullptr) {
      cfg.sim.parallel = par->as_bool();
    }
    if (auto* det = so.find("deterministic") != so.end() ? &so.at("deterministic") : nullptr) {
      cfg.sim.deterministic = det->as_bool();
    }

    if (auto* cs = so.find("cta_scheduler") != so.end() ? &so.at("cta_scheduler") : nullptr) {
      cfg.sim.cta_scheduler = cs->as_string();
    }
    if (auto* wsched = so.find("warp_scheduler") != so.end() ? &so.at("warp_scheduler") : nullptr) {
      cfg.sim.warp_scheduler = wsched->as_string();
    }
    if (auto* allow = so.find("allow_unknown_selectors") != so.end() ? &so.at("allow_unknown_selectors") : nullptr) {
      cfg.sim.allow_unknown_selectors = allow->as_bool();
    }
  }

  // Memory model selector (kept separate for future expansion).
  if (auto* mem = o.find("memory") != o.end() ? &o.at("memory") : nullptr) {
    const auto& mo = mem->as_object();
    if (auto* model = mo.find("model") != mo.end() ? &mo.at("model") : nullptr) {
      cfg.sim.memory_model = model->as_string();
    }
  }

  // Architecture profile (preferred; can override sim.* selectors).
  if (auto* arch = o.find("arch") != o.end() ? &o.at("arch") : nullptr) {
    const auto& ao = arch->as_object();
    if (auto* prof = ao.find("profile") != ao.end() ? &ao.at("profile") : nullptr) {
      cfg.sim.profile = prof->as_string();

      // Profile defaults (baseline only for now).
      if (cfg.sim.profile == "baseline_no_cache") {
        cfg.sim.cta_scheduler = "fifo";
        cfg.sim.warp_scheduler = "in_order_run_to_completion";
        cfg.sim.memory_model = "no_cache_addrspace";
      }
    }

    if (auto* comps = ao.find("components") != ao.end() ? &ao.at("components") : nullptr) {
      const auto& co = comps->as_object();
      if (auto* cs = co.find("cta_scheduler") != co.end() ? &co.at("cta_scheduler") : nullptr) {
        cfg.sim.cta_scheduler = cs->as_string();
      }
      if (auto* wsched = co.find("warp_scheduler") != co.end() ? &co.at("warp_scheduler") : nullptr) {
        cfg.sim.warp_scheduler = wsched->as_string();
      }
      if (auto* mm = co.find("memory_model") != co.end() ? &co.at("memory_model") : nullptr) {
        cfg.sim.memory_model = mm->as_string();
      }
    }
  }
  if (auto* obs = o.find("observability") != o.end() ? &o.at("observability") : nullptr) {
    const auto& oo = obs->as_object();
    if (auto* en = oo.find("enabled") != oo.end() ? &oo.at("enabled") : nullptr) {
      cfg.obs.enabled = en->as_bool();
    }
    if (auto* cap = oo.find("trace_capacity") != oo.end() ? &oo.at("trace_capacity") : nullptr) {
      cfg.obs.trace_capacity = cap->as_u64();
    }
  }
  return cfg;
}

AppConfig load_app_config_json_file(const std::string& path) {
  return load_app_config_json_text(slurp(path));
}

Runtime::Runtime(AppConfig cfg) : cfg_(cfg), obs_(cfg.obs), mem_() {}

HostBufId Runtime::host_alloc(std::uint64_t bytes) {
  auto id = next_host_buf_id_++;
  host_bufs_[id] = std::vector<std::uint8_t>(static_cast<std::size_t>(bytes), 0);
  return id;
}

void Runtime::host_write(HostBufId id, std::uint64_t offset, const std::vector<std::uint8_t>& bytes) {
  auto it = host_bufs_.find(id);
  if (it == host_bufs_.end()) throw std::runtime_error("Runtime: host buffer not found");
  auto& buf = it->second;
  if (offset + bytes.size() > buf.size()) throw std::runtime_error("Runtime: host_write out of range");
  for (std::size_t i = 0; i < bytes.size(); i++) {
    buf[static_cast<std::size_t>(offset) + i] = bytes[i];
  }
}

std::optional<std::vector<std::uint8_t>> Runtime::host_read(HostBufId id, std::uint64_t offset, std::uint64_t bytes) const {
  auto it = host_bufs_.find(id);
  if (it == host_bufs_.end()) return std::nullopt;
  const auto& buf = it->second;
  if (offset + bytes > buf.size()) return std::nullopt;
  std::vector<std::uint8_t> out;
  out.reserve(static_cast<std::size_t>(bytes));
  for (std::uint64_t i = 0; i < bytes; i++) {
    out.push_back(buf[static_cast<std::size_t>(offset + i)]);
  }
  return out;
}

DevicePtr Runtime::device_malloc(std::uint64_t bytes, std::uint64_t align) {
  return mem_.alloc_global(bytes, align);
}

void Runtime::memcpy_h2d(DevicePtr dst, HostBufId src, std::uint64_t src_offset, std::uint64_t bytes) {
  auto data = host_read(src, src_offset, bytes);
  if (!data) throw std::runtime_error("Runtime: memcpy_h2d host_read failed");
  if (!mem_.write_global(dst, *data)) throw std::runtime_error("Runtime: memcpy_h2d write_global failed");
}

void Runtime::memcpy_d2h(HostBufId dst, std::uint64_t dst_offset, DevicePtr src, std::uint64_t bytes) {
  auto data = mem_.read_global(src, bytes);
  if (!data) throw std::runtime_error("Runtime: memcpy_d2h read_global failed");
  host_write(dst, dst_offset, *data);
}

RunOutputs Runtime::run_ptx_kernel(const std::string& ptx_path, const std::string& ptx_isa_path, const std::string& inst_desc_path) {
  LaunchConfig launch;
  launch.grid_dim = Dim3{ 1, 1, 1 };
  launch.block_dim = Dim3{ cfg_.sim.warp_size, 1, 1 };
  launch.warp_size = cfg_.sim.warp_size;
  return run_ptx_kernel_launch(ptx_path, ptx_isa_path, inst_desc_path, launch);
}

RunOutputs Runtime::run_ptx_kernel_launch(const std::string& ptx_path,
                                         const std::string& ptx_isa_path,
                                         const std::string& inst_desc_path,
                                         const LaunchConfig& launch) {
  Parser parser;
  auto mod_tokens = parser.parse_ptx_file_tokens(ptx_path);
  Binder binder;
  auto kernel_tokens = binder.bind_first_kernel(mod_tokens);

  PtxIsaRegistry isa;
  isa.load_json_file(ptx_isa_path);

  DescriptorRegistry reg;
  reg.load_json_file(inst_desc_path);

  return run_kernel_tokens_impl(cfg_, obs_, mem_, kernel_tokens, isa, std::move(reg), /*args=*/nullptr, launch);
}

RunOutputs Runtime::run_ptx_kernel_with_args(const std::string& ptx_path,
                                            const std::string& ptx_isa_path,
                                            const std::string& inst_desc_path,
                                            const KernelArgs& args) {
  LaunchConfig launch;
  launch.grid_dim = Dim3{ 1, 1, 1 };
  launch.block_dim = Dim3{ cfg_.sim.warp_size, 1, 1 };
  launch.warp_size = cfg_.sim.warp_size;
  return run_ptx_kernel_with_args_launch(ptx_path, ptx_isa_path, inst_desc_path, args, launch);
}

RunOutputs Runtime::run_ptx_kernel_with_args_launch(const std::string& ptx_path,
                                                    const std::string& ptx_isa_path,
                                                    const std::string& inst_desc_path,
                                                    const KernelArgs& args,
                                                    const LaunchConfig& launch) {
  Parser parser;
  auto mod_tokens = parser.parse_ptx_file_tokens(ptx_path);
  Binder binder;
  auto kernel_tokens = binder.bind_first_kernel(mod_tokens);

  PtxIsaRegistry isa;
  isa.load_json_file(ptx_isa_path);
  DescriptorRegistry reg;
  reg.load_json_file(inst_desc_path);

  return run_kernel_tokens_impl(cfg_, obs_, mem_, kernel_tokens, isa, std::move(reg), &args, launch);
}

RunOutputs Runtime::run_ptx_kernel_entry_launch(const std::string& ptx_path,
                                               const std::string& ptx_isa_path,
                                               const std::string& inst_desc_path,
                                               const std::string& entry,
                                               const LaunchConfig& launch) {
  Parser parser;
  auto mod_tokens = parser.parse_ptx_file_tokens(ptx_path);
  Binder binder;
  KernelTokens kernel_tokens;
  try {
    kernel_tokens = binder.bind_kernel_by_name(mod_tokens, entry);
  } catch (const std::exception&) {
    RunOutputs out;
    out.sim.diag = Diagnostic{ "runtime", "E_ENTRY_NOT_FOUND", "kernel entry not found: " + entry, std::nullopt, entry, std::nullopt };
    out.sim.completed = false;
    out.sim.steps = 0;
    return out;
  }

  PtxIsaRegistry isa;
  isa.load_json_file(ptx_isa_path);

  DescriptorRegistry reg;
  reg.load_json_file(inst_desc_path);

  return run_kernel_tokens_impl(cfg_, obs_, mem_, kernel_tokens, isa, std::move(reg), /*args=*/nullptr, launch);
}

RunOutputs Runtime::run_ptx_kernel_with_args_entry_launch(const std::string& ptx_path,
                                                          const std::string& ptx_isa_path,
                                                          const std::string& inst_desc_path,
                                                          const std::string& entry,
                                                          const KernelArgs& args,
                                                          const LaunchConfig& launch) {
  Parser parser;
  auto mod_tokens = parser.parse_ptx_file_tokens(ptx_path);
  Binder binder;
  KernelTokens kernel_tokens;
  try {
    kernel_tokens = binder.bind_kernel_by_name(mod_tokens, entry);
  } catch (const std::exception&) {
    RunOutputs out;
    out.sim.diag = Diagnostic{ "runtime", "E_ENTRY_NOT_FOUND", "kernel entry not found: " + entry, std::nullopt, entry, std::nullopt };
    out.sim.completed = false;
    out.sim.steps = 0;
    return out;
  }

  PtxIsaRegistry isa;
  isa.load_json_file(ptx_isa_path);
  DescriptorRegistry reg;
  reg.load_json_file(inst_desc_path);

  return run_kernel_tokens_impl(cfg_, obs_, mem_, kernel_tokens, isa, std::move(reg), &args, launch);
}

RunOutputs Runtime::run_ptx_kernel_text(const std::string& ptx_text,
                                       const std::string& ptx_isa_json_text,
                                       const std::string& inst_desc_json_text) {
  LaunchConfig launch;
  launch.grid_dim = Dim3{ 1, 1, 1 };
  launch.block_dim = Dim3{ cfg_.sim.warp_size, 1, 1 };
  launch.warp_size = cfg_.sim.warp_size;
  return run_ptx_kernel_text_launch(ptx_text, ptx_isa_json_text, inst_desc_json_text, launch);
}

RunOutputs Runtime::run_ptx_kernel_with_args_text(const std::string& ptx_text,
                                                 const std::string& ptx_isa_json_text,
                                                 const std::string& inst_desc_json_text,
                                                 const KernelArgs& args) {
  LaunchConfig launch;
  launch.grid_dim = Dim3{ 1, 1, 1 };
  launch.block_dim = Dim3{ cfg_.sim.warp_size, 1, 1 };
  launch.warp_size = cfg_.sim.warp_size;
  return run_ptx_kernel_with_args_text_launch(ptx_text, ptx_isa_json_text, inst_desc_json_text, args, launch);
}

RunOutputs Runtime::run_ptx_kernel_text_launch(const std::string& ptx_text,
                                              const std::string& ptx_isa_json_text,
                                              const std::string& inst_desc_json_text,
                                              const LaunchConfig& launch) {
  Parser parser;
  auto mod_tokens = parser.parse_ptx_text_tokens(ptx_text, "<ptx>");
  Binder binder;
  auto kernel_tokens = binder.bind_first_kernel(mod_tokens);

  PtxIsaRegistry isa;
  isa.load_json_text(ptx_isa_json_text);

  DescriptorRegistry reg;
  reg.load_json_text(inst_desc_json_text);

  return run_kernel_tokens_impl(cfg_, obs_, mem_, kernel_tokens, isa, std::move(reg), /*args=*/nullptr, launch);
}

RunOutputs Runtime::run_ptx_kernel_with_args_text_launch(const std::string& ptx_text,
                                                        const std::string& ptx_isa_json_text,
                                                        const std::string& inst_desc_json_text,
                                                        const KernelArgs& args,
                                                        const LaunchConfig& launch) {
  Parser parser;
  auto mod_tokens = parser.parse_ptx_text_tokens(ptx_text, "<ptx>");
  Binder binder;
  auto kernel_tokens = binder.bind_first_kernel(mod_tokens);

  PtxIsaRegistry isa;
  isa.load_json_text(ptx_isa_json_text);

  DescriptorRegistry reg;
  reg.load_json_text(inst_desc_json_text);

  return run_kernel_tokens_impl(cfg_, obs_, mem_, kernel_tokens, isa, std::move(reg), &args, launch);
}

RunOutputs Runtime::run_ptx_kernel_text_entry_launch(const std::string& ptx_text,
                                                    const std::string& ptx_isa_json_text,
                                                    const std::string& inst_desc_json_text,
                                                    const std::string& entry,
                                                    const LaunchConfig& launch) {
  Parser parser;
  auto mod_tokens = parser.parse_ptx_text_tokens(ptx_text, "<ptx>");
  Binder binder;
  KernelTokens kernel_tokens;
  try {
    kernel_tokens = binder.bind_kernel_by_name(mod_tokens, entry);
  } catch (const std::exception&) {
    RunOutputs out;
    out.sim.diag = Diagnostic{ "runtime", "E_ENTRY_NOT_FOUND", "kernel entry not found: " + entry, std::nullopt, entry, std::nullopt };
    out.sim.completed = false;
    out.sim.steps = 0;
    return out;
  }

  PtxIsaRegistry isa;
  isa.load_json_text(ptx_isa_json_text);

  DescriptorRegistry reg;
  reg.load_json_text(inst_desc_json_text);

  return run_kernel_tokens_impl(cfg_, obs_, mem_, kernel_tokens, isa, std::move(reg), /*args=*/nullptr, launch);
}

RunOutputs Runtime::run_ptx_kernel_with_args_text_entry_launch(const std::string& ptx_text,
                                                              const std::string& ptx_isa_json_text,
                                                              const std::string& inst_desc_json_text,
                                                              const std::string& entry,
                                                              const KernelArgs& args,
                                                              const LaunchConfig& launch) {
  Parser parser;
  auto mod_tokens = parser.parse_ptx_text_tokens(ptx_text, "<ptx>");
  Binder binder;
  KernelTokens kernel_tokens;
  try {
    kernel_tokens = binder.bind_kernel_by_name(mod_tokens, entry);
  } catch (const std::exception&) {
    RunOutputs out;
    out.sim.diag = Diagnostic{ "runtime", "E_ENTRY_NOT_FOUND", "kernel entry not found: " + entry, std::nullopt, entry, std::nullopt };
    out.sim.completed = false;
    out.sim.steps = 0;
    return out;
  }

  PtxIsaRegistry isa;
  isa.load_json_text(ptx_isa_json_text);

  DescriptorRegistry reg;
  reg.load_json_text(inst_desc_json_text);

  return run_kernel_tokens_impl(cfg_, obs_, mem_, kernel_tokens, isa, std::move(reg), &args, launch);
}

} // namespace gpusim
