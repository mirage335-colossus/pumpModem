# Host emulation of the SM120 OpenCL kernels, for development hosts without
# the NVIDIA toolkit. The device test then runs against a loadable stand-in
# that executes the kernel source on the CPU. It checks kernel logic and host
# orchestration. It is never a device result.
if(NOT BUILD_TESTING OR DATAPUMP_SANITIZERS OR NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
    NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
  return()
endif()
set(_triad_emulation_dir "${PROJECT_SOURCE_DIR}/tests/triad_kernel_emulation")
set(_triad_emulation_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/triad_opencl_emulation")
configure_file("${_triad_emulation_dir}/triad_opencl_nvfp4_ptx.hpp.in"
  "${_triad_emulation_generated}/triad_opencl_nvfp4_ptx.hpp" COPYONLY)

add_library(datapump_triad_opencl_emulation SHARED EXCLUDE_FROM_ALL "${_triad_emulation_dir}/opencl_emulation.cpp")
target_include_directories(datapump_triad_opencl_emulation PRIVATE "${_triad_emulation_dir}" src)
target_compile_features(datapump_triad_opencl_emulation PRIVATE cxx_std_20)
target_compile_definitions(datapump_triad_opencl_emulation PRIVATE DATAPUMP_TRIAD_KERNEL_EMULATION=1)
target_compile_options(datapump_triad_opencl_emulation PRIVATE
  -ffp-contract=off -fno-fast-math -fvisibility=hidden -Wno-unknown-pragmas)

# The production host sources, compiled against the declared OpenCL subset
# and bound to the emulation library in place of the system loader.
add_library(datapump_triad_opencl_emulated_host STATIC EXCLUDE_FROM_ALL
  src/triad_opencl_nvfp4_projection.cpp src/triad_opencl_nvfp4_coarse.cpp src/triad_opencl_coarse_bulk.cpp
  src/triad_opencl_fine_bulk.cpp)
add_dependencies(datapump_triad_opencl_emulated_host datapump_triad_opencl_emulation)
target_include_directories(datapump_triad_opencl_emulated_host PUBLIC src include "${_triad_emulation_generated}")
target_include_directories(datapump_triad_opencl_emulated_host SYSTEM PUBLIC "${_triad_emulation_dir}")
target_compile_features(datapump_triad_opencl_emulated_host PUBLIC cxx_std_20)
target_compile_definitions(datapump_triad_opencl_emulated_host PRIVATE CL_TARGET_OPENCL_VERSION=120
  "DATAPUMP_TRIAD_OPENCL_LOADER_PATH=\"$<TARGET_FILE:datapump_triad_opencl_emulation>\"")
target_link_libraries(datapump_triad_opencl_emulated_host PUBLIC datapump_triad_receiver ${CMAKE_DL_LIBS})

add_executable(test_triad_opencl_nvfp4_emulated EXCLUDE_FROM_ALL
  tests/test_triad_opencl_nvfp4.cpp src/host/sandbox.cpp)
target_include_directories(test_triad_opencl_nvfp4_emulated PRIVATE tools)
target_link_libraries(test_triad_opencl_nvfp4_emulated PRIVATE datapump_triad_opencl_emulated_host Threads::Threads)
add_test(NAME triad_opencl_nvfp4_emulated COMMAND test_triad_opencl_nvfp4_emulated)
set_tests_properties(triad_opencl_nvfp4_emulated PROPERTIES LABELS "triad" TIMEOUT 300
  ENVIRONMENT "DATAPUMP_TRIAD_EMULATION_DEFER=0")

add_executable(test_triad_opencl_coarse_session_emulated EXCLUDE_FROM_ALL
  tests/test_triad_opencl_coarse_session.cpp src/host/sandbox.cpp)
target_include_directories(test_triad_opencl_coarse_session_emulated PRIVATE tests)
target_link_libraries(test_triad_opencl_coarse_session_emulated PRIVATE
  datapump_triad_opencl_emulated_host Threads::Threads)
add_test(NAME triad_opencl_coarse_session_emulated COMMAND test_triad_opencl_coarse_session_emulated)
set_tests_properties(triad_opencl_coarse_session_emulated PROPERTIES LABELS "triad" TIMEOUT 180
  ENVIRONMENT "DATAPUMP_TRIAD_EMULATION_DEFER=0")
# The one-ray session of the default spread and the named fallback reasons.
add_test(NAME triad_opencl_coarse_session_emulated_one_ray COMMAND test_triad_opencl_coarse_session_emulated one-ray)
set_tests_properties(triad_opencl_coarse_session_emulated_one_ray PROPERTIES LABELS "triad" TIMEOUT 180
  ENVIRONMENT "DATAPUMP_TRIAD_EMULATION_DEFER=0")

add_executable(test_triad_opencl_fine_session_emulated EXCLUDE_FROM_ALL
  tests/test_triad_opencl_fine_session.cpp src/host/sandbox.cpp)
target_include_directories(test_triad_opencl_fine_session_emulated PRIVATE tests)
target_link_libraries(test_triad_opencl_fine_session_emulated PRIVATE
  datapump_triad_opencl_emulated_host Threads::Threads)
add_test(NAME triad_opencl_fine_session_emulated COMMAND test_triad_opencl_fine_session_emulated)
set_tests_properties(triad_opencl_fine_session_emulated PROPERTIES LABELS "triad" TIMEOUT 300
  ENVIRONMENT "DATAPUMP_TRIAD_EMULATION_DEFER=0")
# The same session with every command that does not block deferred to the
# next synchronization point, as late as an asynchronous device may run it.
add_test(NAME triad_opencl_nvfp4_emulated_deferred COMMAND test_triad_opencl_nvfp4_emulated resident)
set_tests_properties(triad_opencl_nvfp4_emulated_deferred PROPERTIES LABELS "triad" TIMEOUT 300
  ENVIRONMENT "DATAPUMP_TRIAD_EMULATION_DEFER=1")
add_test(NAME triad_opencl_fine_session_emulated_deferred COMMAND test_triad_opencl_fine_session_emulated)
set_tests_properties(triad_opencl_fine_session_emulated_deferred PROPERTIES LABELS "triad" TIMEOUT 300
  ENVIRONMENT "DATAPUMP_TRIAD_EMULATION_DEFER=1")
