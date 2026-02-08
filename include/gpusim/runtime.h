#pragma once

#include "gpusim/abi.h"
#include "gpusim/frontend.h"
#include "gpusim/instruction_desc.h"
#include "gpusim/ptx_isa.h"
#include "gpusim/memory.h"
#include "gpusim/observability.h"
#include "gpusim/simt.h"

#include <optional>
#include <unordered_map>
#include <string>
#include <vector>

namespace gpusim {

struct AppConfig final {
  SimConfig sim;
  ObsConfig obs;
};

AppConfig load_app_config_json_text(const std::string& text);
AppConfig load_app_config_json_file(const std::string& path);

struct RunOutputs final {
  SimResult sim;
};

class Runtime final {
public:
  explicit Runtime(AppConfig cfg);

  RunOutputs run_ptx_kernel(const std::string& ptx_path, const std::string& ptx_isa_path, const std::string& inst_desc_path);
  RunOutputs run_ptx_kernel_with_args(const std::string& ptx_path, const std::string& ptx_isa_path, const std::string& inst_desc_path, const KernelArgs& args);

  RunOutputs run_ptx_kernel_launch(const std::string& ptx_path,
                                  const std::string& ptx_isa_path,
                                  const std::string& inst_desc_path,
                                  const LaunchConfig& launch);
  RunOutputs run_ptx_kernel_with_args_launch(const std::string& ptx_path,
                                             const std::string& ptx_isa_path,
                                             const std::string& inst_desc_path,
                                             const KernelArgs& args,
                                             const LaunchConfig& launch);

  RunOutputs run_ptx_kernel_entry_launch(const std::string& ptx_path,
                                        const std::string& ptx_isa_path,
                                        const std::string& inst_desc_path,
                                        const std::string& entry,
                                        const LaunchConfig& launch);
  RunOutputs run_ptx_kernel_with_args_entry_launch(const std::string& ptx_path,
                                                   const std::string& ptx_isa_path,
                                                   const std::string& inst_desc_path,
                                                   const std::string& entry,
                                                   const KernelArgs& args,
                                                   const LaunchConfig& launch);

  // In-memory overloads: provide PTX text and JSON text for assets.
  // These preserve the same execution semantics as the file-path APIs, but avoid depending on repo paths.
  RunOutputs run_ptx_kernel_text(const std::string& ptx_text,
                                const std::string& ptx_isa_json_text,
                                const std::string& inst_desc_json_text);
  RunOutputs run_ptx_kernel_with_args_text(const std::string& ptx_text,
                                          const std::string& ptx_isa_json_text,
                                          const std::string& inst_desc_json_text,
                                          const KernelArgs& args);

  RunOutputs run_ptx_kernel_text_launch(const std::string& ptx_text,
                                       const std::string& ptx_isa_json_text,
                                       const std::string& inst_desc_json_text,
                                       const LaunchConfig& launch);
  RunOutputs run_ptx_kernel_with_args_text_launch(const std::string& ptx_text,
                                                 const std::string& ptx_isa_json_text,
                                                 const std::string& inst_desc_json_text,
                                                 const KernelArgs& args,
                                                 const LaunchConfig& launch);

  RunOutputs run_ptx_kernel_text_entry_launch(const std::string& ptx_text,
                                             const std::string& ptx_isa_json_text,
                                             const std::string& inst_desc_json_text,
                                             const std::string& entry,
                                             const LaunchConfig& launch);
  RunOutputs run_ptx_kernel_with_args_text_entry_launch(const std::string& ptx_text,
                                                       const std::string& ptx_isa_json_text,
                                                       const std::string& inst_desc_json_text,
                                                       const std::string& entry,
                                                       const KernelArgs& args,
                                                       const LaunchConfig& launch);

  RunOutputs run_workload(const std::string& workload_json_path);

  HostBufId host_alloc(std::uint64_t bytes);
  void host_write(HostBufId id, std::uint64_t offset, const std::vector<std::uint8_t>& bytes);
  std::optional<std::vector<std::uint8_t>> host_read(HostBufId id, std::uint64_t offset, std::uint64_t bytes) const;

  DevicePtr device_malloc(std::uint64_t bytes, std::uint64_t align = 16);
  void memcpy_h2d(DevicePtr dst, HostBufId src, std::uint64_t src_offset, std::uint64_t bytes);
  void memcpy_d2h(HostBufId dst, std::uint64_t dst_offset, DevicePtr src, std::uint64_t bytes);

  const ObsControl& obs() const { return obs_; }
  ObsControl& obs() { return obs_; }

private:
  AppConfig cfg_;
  ObsControl obs_;
  AddrSpaceManager mem_;

  HostBufId next_host_buf_id_ = 1;
  std::unordered_map<HostBufId, std::vector<std::uint8_t>> host_bufs_;
};

} // namespace gpusim
