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
  src/runtime/engines.cpp
  src/runtime/runtime.cpp
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
endif()
