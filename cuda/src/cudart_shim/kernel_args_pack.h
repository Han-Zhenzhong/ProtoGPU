#pragma once

#include "cudart_shim/cuda_abi_min.h"
#include "cudart_shim/device_memory.h"

#include "gpusim/abi.h"

#include <string>

namespace gpusim_cudart_shim {

// Packs kernelParams into gpusim::KernelArgs using PTX-derived param layout.
// Returns cudaSuccess on success; otherwise returns an error code and sets out_error.
cudaError_t pack_kernel_args_mvp(const std::vector<gpusim::ParamDesc>& layout,
                                void** kernelParams,
                                const DeviceMemory& device_memory,
                                gpusim::KernelArgs& out_args,
                                std::string& out_error);

} // namespace gpusim_cudart_shim
