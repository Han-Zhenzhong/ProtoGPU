#include "gpusim/runtime.h"

#include "gpusim/observability.h"

#include <filesystem>
#include <fstream>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

struct Args final {
  std::string ptx = "assets/ptx/demo_kernel.ptx";
  std::string ptx_isa = "assets/ptx_isa/demo_ptx8.json";
  std::string inst_desc = "assets/inst_desc/demo_desc.json";
  std::string config = "assets/configs/demo_config.json";
  std::string trace_out = "out/trace.jsonl";
  std::string stats_out = "out/stats.json";
  bool io_demo = false;
};

Args parse_args(int argc, char** argv) {
  Args a;
  for (int i = 1; i < argc; i++) {
    std::string k = argv[i];
    auto need = [&](const char* flag) {
      if (i + 1 >= argc) throw std::runtime_error(std::string("missing value for ") + flag);
      return std::string(argv[++i]);
    };
    if (k == "--ptx") a.ptx = need("--ptx");
    else if (k == "--ptx-isa") a.ptx_isa = need("--ptx-isa");
    else if (k == "--inst-desc") a.inst_desc = need("--inst-desc");
    else if (k == "--desc") a.inst_desc = need("--desc");
    else if (k == "--config") a.config = need("--config");
    else if (k == "--trace") a.trace_out = need("--trace");
    else if (k == "--stats") a.stats_out = need("--stats");
    else if (k == "--io-demo") a.io_demo = true;
    else if (k == "--help" || k == "-h") {
      std::cout << "gpu-sim-cli --ptx <file> --ptx-isa <file> --inst-desc <file> --config <file> --trace <file> --stats <file> [--io-demo]\n";
      std::exit(0);
    }
  }
  return a;
}

} // namespace

int main(int argc, char** argv) {
  try {
    auto args = parse_args(argc, argv);
    auto cfg = gpusim::load_app_config_json_file(args.config);
    gpusim::Runtime rt(cfg);

    gpusim::RunOutputs outputs;
    if (args.io_demo) {
      // IO demo expects the kernel to have a single param: .param .u64 out_ptr
      // Example PTX: assets/ptx/write_out.ptx
      auto dev_out = rt.device_malloc(4, 4);
      auto host_out = rt.host_alloc(4);

      gpusim::KernelArgs ka;
      ka.blob.resize(8, 0);
      for (int b = 0; b < 8; b++) {
        ka.blob[static_cast<std::size_t>(b)] = static_cast<std::uint8_t>((dev_out >> (8 * b)) & 0xFFu);
      }

      outputs = rt.run_ptx_kernel_with_args(args.ptx, args.ptx_isa, args.inst_desc, ka);
      rt.memcpy_d2h(host_out, 0, dev_out, 4);
      auto bytes = rt.host_read(host_out, 0, 4);
      if (bytes && bytes->size() == 4) {
        std::uint32_t v = static_cast<std::uint32_t>((*bytes)[0]) |
                          (static_cast<std::uint32_t>((*bytes)[1]) << 8) |
                          (static_cast<std::uint32_t>((*bytes)[2]) << 16) |
                          (static_cast<std::uint32_t>((*bytes)[3]) << 24);
        std::cout << "io-demo u32 result: " << v << "\n";
      }
    } else {
      outputs = rt.run_ptx_kernel(args.ptx, args.ptx_isa, args.inst_desc);
    }

    std::filesystem::create_directories(std::filesystem::path(args.trace_out).parent_path());
    std::filesystem::create_directories(std::filesystem::path(args.stats_out).parent_path());

    {
      std::ofstream trace(args.trace_out, std::ios::binary);
      for (const auto& e : rt.obs().trace_snapshot()) {
        trace << gpusim::event_to_json_line(e);
      }
    }
    {
      std::ofstream stats(args.stats_out, std::ios::binary);
      stats << gpusim::stats_to_json(rt.obs().counters_snapshot());
    }

    if (outputs.sim.diag) {
      std::cerr << "Simulation error: " << outputs.sim.diag->module << ":" << outputs.sim.diag->code << " "
                << outputs.sim.diag->message << "\n";
      if (outputs.sim.diag->location) {
        const auto& loc = *outputs.sim.diag->location;
        std::cerr << "  at " << loc.file << ":" << loc.line << ":" << loc.column << "\n";
      }
      if (outputs.sim.diag->inst_index) {
        std::cerr << "  inst_index=" << *outputs.sim.diag->inst_index << "\n";
      }
      return 2;
    }
    std::cout << "Simulation completed in " << outputs.sim.steps << " steps\n";
    return outputs.sim.completed ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "Fatal: " << e.what() << "\n";
    return 3;
  }
}
