# Generates a C++ translation unit that embeds JSON assets for the CUDA runtime shim.
# Inputs (cache variables):
#   OUT, CONFIG_JSON, PTX_ISA_JSON, INST_DESC_JSON

if (NOT DEFINED OUT)
  message(FATAL_ERROR "OUT is required")
endif()
if (NOT DEFINED CONFIG_JSON)
  message(FATAL_ERROR "CONFIG_JSON is required")
endif()
if (NOT DEFINED PTX_ISA_JSON)
  message(FATAL_ERROR "PTX_ISA_JSON is required")
endif()
if (NOT DEFINED INST_DESC_JSON)
  message(FATAL_ERROR "INST_DESC_JSON is required")
endif()

file(READ "${CONFIG_JSON}" _cfg)
file(READ "${PTX_ISA_JSON}" _isa)
file(READ "${INST_DESC_JSON}" _desc)

# Pick raw-string delimiters that are extremely unlikely to appear in JSON.
# Note: C++ raw-string delimiters are limited to 16 characters.
set(_D1 "GPUSIM_CFG")
set(_D2 "GPUSIM_ISA")
set(_D3 "GPUSIM_DESC")

set(_out "// Auto-generated. Do not edit.\n")
string(APPEND _out "#include <cstdint>\n\n")
string(APPEND _out "namespace gpusim_cudart_shim::embedded {\n")
string(APPEND _out "static const char kConfigJson[] = R\"${_D1}(\n${_cfg}\n)${_D1}\";\n\n")
string(APPEND _out "static const char kPtxIsaJson[] = R\"${_D2}(\n${_isa}\n)${_D2}\";\n\n")
string(APPEND _out "static const char kInstDescJson[] = R\"${_D3}(\n${_desc}\n)${_D3}\";\n\n")
string(APPEND _out "const char* config_json() { return kConfigJson; }\n")
string(APPEND _out "const char* ptx_isa_json() { return kPtxIsaJson; }\n")
string(APPEND _out "const char* inst_desc_json() { return kInstDescJson; }\n")
string(APPEND _out "} // namespace gpusim_cudart_shim::embedded\n")

file(WRITE "${OUT}" "${_out}")
