cmake_minimum_required(VERSION 3.20)

project(gpu_sim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(gpusim_core
  src/common/json.cpp
  src/common/contracts.cpp
  src/observability/observability.cpp
  src/instruction/descriptor_registry.cpp
  src/instruction/ptx_isa.cpp
  src/instruction/expander.cpp
  src/frontend/parser.cpp
  src/frontend/binder.cpp
  src/memory/memory.cpp
  src/units/exec_core.cpp
  src/units/control_unit.cpp
  src/units/mem_unit.cpp
  src/simt/control_flow_analysis.cpp
  src/simt/simt.cpp
  src/runtime/deps.cpp
  src/runtime/cta_scheduler.cpp
  src/runtime/engines.cpp
  src/runtime/runtime.cpp
  src/runtime/workload.cpp
  src/simt/warp_scheduler.cpp
)

# gpusim_core is linked into shared libraries (e.g. the CUDA runtime shim),
# so it must be built as position-independent code on ELF platforms.
set_target_properties(gpusim_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(gpusim_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(gpusim_core PRIVATE
  GPUSIM_VERSION="0.1"
)

add_executable(gpu-sim-cli
  src/apps/cli/main.cpp
)

target_link_libraries(gpu-sim-cli PRIVATE gpusim_core)

# --- CUDA Runtime Shim (MVP) ---
# A minimal libcudart replacement that routes selected CUDA Runtime APIs to gpu-sim.
set(GPUSIM_CUDART_SHIM_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/cudart_shim")
set(GPUSIM_CUDART_SHIM_EMBED_CPP "${GPUSIM_CUDART_SHIM_GEN_DIR}/assets_embedded.cpp")

add_custom_command(
  OUTPUT ${GPUSIM_CUDART_SHIM_EMBED_CPP}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${GPUSIM_CUDART_SHIM_GEN_DIR}
  COMMAND ${CMAKE_COMMAND}
          -DOUT=${GPUSIM_CUDART_SHIM_EMBED_CPP}
          -DCONFIG_JSON=${CMAKE_CURRENT_SOURCE_DIR}/assets/configs/demo_config.json
          -DPTX_ISA_JSON=${CMAKE_CURRENT_SOURCE_DIR}/assets/ptx_isa/demo_ptx64.json
          -DINST_DESC_JSON=${CMAKE_CURRENT_SOURCE_DIR}/assets/inst_desc/demo_desc.json
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_cudart_shim_assets.cmake
  DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_cudart_shim_assets.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/configs/demo_config.json
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/ptx_isa/demo_ptx64.json
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/inst_desc/demo_desc.json
  VERBATIM
)

add_library(cudart-shim SHARED
  ${GPUSIM_CUDART_SHIM_EMBED_CPP}
  cuda/src/cudart_shim/exports.cpp
  cuda/src/cudart_shim/error_state.cpp
  cuda/src/cudart_shim/assets_provider.cpp
  cuda/src/cudart_shim/runtime_context.cpp
  cuda/src/cudart_shim/device_memory.cpp
  cuda/src/cudart_shim/launch_config_stack.cpp
  cuda/src/cudart_shim/fatbin_registry.cpp
  cuda/src/cudart_shim/kernel_registry.cpp
  cuda/src/cudart_shim/ptx_entry_validate.cpp
  cuda/src/cudart_shim/kernel_args_pack.cpp
  cuda/src/cudart_shim/stream_scheduler.cpp
)

target_include_directories(cudart-shim PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/cuda/src
  ${GPUSIM_CUDART_SHIM_GEN_DIR}
)

target_link_libraries(cudart-shim PRIVATE gpusim_core)

# Match the loader name expected by Linux demo binaries: libcudart.so.12
set_target_properties(cudart-shim PROPERTIES
  OUTPUT_NAME "cudart"
  SOVERSION 12
  VERSION 12.0
)

# Provide ELF symbol versions expected by some clang/NVIDIA-linked demo binaries.
if (UNIX AND NOT APPLE)
  target_link_options(cudart-shim PRIVATE
    "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cuda/src/cudart_shim/libcudart.so.12.map"
  )
endif()

include(CTest)
if (BUILD_TESTING)
  add_executable(gpu-sim-tests
    tests/ptx_isa_mapper_tests.cpp
  )
  target_link_libraries(gpu-sim-tests PRIVATE gpusim_core)
  add_test(NAME gpu-sim-tests COMMAND gpu-sim-tests)

  add_executable(cudart-shim-smoke-tests
    tests/cudart_shim_memory_smoke_tests.cpp
  )
  target_include_directories(cudart-shim-smoke-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/cuda/src
  )
  target_link_libraries(cudart-shim-smoke-tests PRIVATE cudart-shim)
  add_test(NAME cudart-shim-smoke-tests COMMAND cudart-shim-smoke-tests)

  add_executable(cudart-shim-ptx-override-tests
    cuda/src/cudart_shim/fatbin_registry.cpp
    cuda/src/cudart_shim/ptx_entry_validate.cpp
    tests/cudart_shim_ptx_override_tests.cpp
  )
  target_include_directories(cudart-shim-ptx-override-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/cuda/src
  )
  add_test(NAME cudart-shim-ptx-override-tests COMMAND cudart-shim-ptx-override-tests)

  # Streaming shim test (no CUDA toolkit required): exercises cudaStream* + cudaMemcpyAsync.
  add_executable(cudart-shim-streaming-tests
    tests/cudart_shim_streaming_tests.cpp
  )
  target_link_libraries(cudart-shim-streaming-tests PRIVATE cudart-shim)
  add_test(
    NAME cudart-shim-streaming-tests
    COMMAND ${CMAKE_COMMAND} -E env
            LD_LIBRARY_PATH=$<TARGET_FILE_DIR:cudart-shim>
            $<TARGET_FILE:cudart-shim-streaming-tests>
  )
  set_tests_properties(cudart-shim-streaming-tests PROPERTIES
    PASS_REGULAR_EXPRESSION "OK"
  )

  # End-to-end shim integration: compile demo.cu when the CUDA toolchain is available,
  # otherwise fall back to the prebuilt demo binary/PTX artifacts.
  # This is Linux/WSL only.
  if (UNIX AND NOT APPLE)
    add_test(
      NAME cudart-shim-demo-integration
      COMMAND ${CMAKE_COMMAND} -E env
              GPUSIM_TEST_SKIP_RC=125
              GPUSIM_CUDART_SHIM_LOG=0
              CONFIG=$<CONFIG>
                  bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_cuda_shim_e2e_demo_integration.sh
                   ${CMAKE_BINARY_DIR}
    )
    set_tests_properties(cudart-shim-demo-integration PROPERTIES
      PASS_REGULAR_EXPRESSION "OK"
      SKIP_RETURN_CODE 125
    )

    add_test(
      NAME cudart-shim-multi-ptx-demo-integration
      COMMAND ${CMAKE_COMMAND} -E env
              GPUSIM_TEST_SKIP_RC=125
              GPUSIM_CUDART_SHIM_LOG=0
              CONFIG=$<CONFIG>
                  bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_cuda_shim_e2e_multi_ptx_demo_integration.sh
                   ${CMAKE_BINARY_DIR}
    )
    set_tests_properties(cudart-shim-multi-ptx-demo-integration PROPERTIES
      PASS_REGULAR_EXPRESSION "OK"
      SKIP_RETURN_CODE 125
    )

    add_test(
      NAME cudart-shim-warp-reduce-add-demo-integration
      COMMAND ${CMAKE_COMMAND} -E env
              GPUSIM_TEST_SKIP_RC=125
              GPUSIM_CUDART_SHIM_LOG=0
              CONFIG=$<CONFIG>
                  bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_cuda_shim_e2e_warp_reduce_add_demo_cu.sh
                   ${CMAKE_BINARY_DIR}
    )
    set_tests_properties(cudart-shim-warp-reduce-add-demo-integration PROPERTIES
      PASS_REGULAR_EXPRESSION "OK"
      SKIP_RETURN_CODE 125
    )

    add_test(
      NAME cudart-shim-streaming-demo-integration
      COMMAND ${CMAKE_COMMAND} -E env
              GPUSIM_TEST_SKIP_RC=125
              GPUSIM_CUDART_SHIM_LOG=0
              CONFIG=$<CONFIG>
                  bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_cuda_shim_e2e_streaming_demo_cu.sh
                   ${CMAKE_BINARY_DIR}
    )
    set_tests_properties(cudart-shim-streaming-demo-integration PROPERTIES
      PASS_REGULAR_EXPRESSION "OK"
      SKIP_RETURN_CODE 125
    )
  endif()

  add_executable(gpu-sim-inst-desc-tests
    tests/inst_desc_registry_tests.cpp
  )
  target_link_libraries(gpu-sim-inst-desc-tests PRIVATE gpusim_core)
  add_test(NAME gpu-sim-inst-desc-tests COMMAND gpu-sim-inst-desc-tests)

  add_executable(gpu-sim-simt-tests
    tests/simt_predication_controlflow_tests.cpp
  )
  target_link_libraries(gpu-sim-simt-tests PRIVATE gpusim_core)
  add_test(NAME gpu-sim-simt-tests COMMAND gpu-sim-simt-tests)

  add_executable(gpu-sim-memory-tests
    tests/memory_no_cache_addrspace_tests.cpp
  )
  target_link_libraries(gpu-sim-memory-tests PRIVATE gpusim_core)
  add_test(NAME gpu-sim-memory-tests COMMAND gpu-sim-memory-tests)

  add_executable(gpu-sim-observability-contract-tests
    tests/observability_output_contract_tests.cpp
  )
  target_link_libraries(gpu-sim-observability-contract-tests PRIVATE gpusim_core)
  add_test(NAME gpu-sim-observability-contract-tests COMMAND gpu-sim-observability-contract-tests)

  add_executable(gpu-sim-public-api-tests
    tests/public_api_in_memory_tests.cpp
  )
  target_link_libraries(gpu-sim-public-api-tests PRIVATE gpusim_core)
  add_test(NAME gpu-sim-public-api-tests COMMAND gpu-sim-public-api-tests)

  add_executable(gpu-sim-builtins-tests
    tests/builtins_tests.cpp
  )
  target_link_libraries(gpu-sim-builtins-tests PRIVATE gpusim_core)
  add_test(NAME gpu-sim-builtins-tests COMMAND gpu-sim-builtins-tests)

  add_executable(gpu-sim-config-parse-tests
    tests/config_parse_tests.cpp
  )
  target_link_libraries(gpu-sim-config-parse-tests PRIVATE gpusim_core)
  add_test(NAME gpu-sim-config-parse-tests COMMAND gpu-sim-config-parse-tests)

  add_executable(gpu-sim-tiny-gpt2-mincov-tests
    tests/tiny_gpt2_minimal_coverage_tests.cpp
  )
  target_link_libraries(gpu-sim-tiny-gpt2-mincov-tests PRIVATE gpusim_core)
  add_test(
    NAME gpu-sim-tiny-gpt2-mincov-tests
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_CURRENT_SOURCE_DIR}
            $<TARGET_FILE:gpu-sim-tiny-gpt2-mincov-tests>
  )

  add_test(
    NAME gpu-sim-workload-smoke-single-stream
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_CURRENT_SOURCE_DIR}
            $<TARGET_FILE:gpu-sim-cli>
            --config assets/configs/demo_config.json
            --workload assets/workloads/smoke_single_stream.json
            --trace out/workload_smoke_single_stream.trace.jsonl
            --stats out/workload_smoke_single_stream.stats.json
  )

  add_test(
    NAME gpu-sim-workload-smoke-two-stream-event
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_CURRENT_SOURCE_DIR}
            $<TARGET_FILE:gpu-sim-cli>
            --config assets/configs/demo_config.json
            --workload assets/workloads/smoke_two_stream_event.json
            --trace out/workload_smoke_two_stream_event.trace.jsonl
            --stats out/workload_smoke_two_stream_event.stats.json
  )
endif()
