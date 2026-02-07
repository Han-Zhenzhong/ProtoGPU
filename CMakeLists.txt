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
  src/simt/simt.cpp
  src/runtime/deps.cpp
  src/runtime/cta_scheduler.cpp
  src/runtime/engines.cpp
  src/runtime/runtime.cpp
  src/runtime/workload.cpp
  src/simt/warp_scheduler.cpp
)

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

include(CTest)
if (BUILD_TESTING)
  add_executable(gpu-sim-tests
    tests/ptx_isa_mapper_tests.cpp
  )
  target_link_libraries(gpu-sim-tests PRIVATE gpusim_core)
  add_test(NAME gpu-sim-tests COMMAND gpu-sim-tests)

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
