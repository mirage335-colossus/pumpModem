set(DATAPUMP_TRIAD_OPENCL_NVFP4 "AUTO" CACHE STRING "Compile the Linux OpenCL SM120 NVFP4 projection (AUTO, ON, or OFF)")
set_property(CACHE DATAPUMP_TRIAD_OPENCL_NVFP4 PROPERTY STRINGS AUTO ON OFF)
set(DATAPUMP_TRIAD_OPENCL_LOADER_PATH "" CACHE FILEPATH "Absolute system OpenCL loader path for the standalone deployment")
if(DATAPUMP_TRIAD_OPENCL_NVFP4 STREQUAL "OFF")
  return()
elseif(NOT DATAPUMP_TRIAD_OPENCL_NVFP4 MATCHES "^(AUTO|ON)$")
  message(FATAL_ERROR "DATAPUMP_TRIAD_OPENCL_NVFP4 must be AUTO, ON, or OFF")
endif()

set(_triad_opencl_unavailable "")
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(_triad_opencl_unavailable "a Linux host is required")
else()
  find_program(DATAPUMP_TRIAD_OPENCL_NVCC nvcc HINTS "${CUDAToolkit_ROOT}/bin"
    "$ENV{CUDA_HOME}/bin" "$ENV{CUDA_PATH}/bin" PATHS /usr/local/cuda/bin)
  find_path(DATAPUMP_TRIAD_OPENCL_HEADERS NAMES CL/cl.h)
  find_package(Python3 QUIET COMPONENTS Interpreter)
  if(DATAPUMP_TRIAD_OPENCL_NVCC)
    execute_process(COMMAND "${DATAPUMP_TRIAD_OPENCL_NVCC}" --version
      OUTPUT_VARIABLE _triad_opencl_nvcc_version ERROR_QUIET RESULT_VARIABLE _triad_opencl_nvcc_status
      TIMEOUT 5 OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REGEX MATCH "V([0-9]+\\.[0-9]+\\.[0-9]+)" _triad_opencl_nvcc_match "${_triad_opencl_nvcc_version}")
    set(_triad_opencl_nvcc_version "${CMAKE_MATCH_1}")
  endif()
  if(NOT _triad_opencl_nvcc_status STREQUAL "0" OR NOT _triad_opencl_nvcc_match OR
      _triad_opencl_nvcc_version VERSION_LESS 12.8)
    set(_triad_opencl_unavailable "NVCC 12.8 or newer is required for the offline PTX build")
  elseif(NOT DATAPUMP_TRIAD_OPENCL_HEADERS OR NOT EXISTS "${DATAPUMP_TRIAD_OPENCL_HEADERS}/CL/cl_ext.h")
    set(_triad_opencl_unavailable "OpenCL C headers are required")
  elseif(NOT Python3_Interpreter_FOUND)
    set(_triad_opencl_unavailable "Python 3 is required to generate the embedded PTX")
  endif()
endif()
if(_triad_opencl_unavailable)
  if(DATAPUMP_TRIAD_OPENCL_NVFP4 STREQUAL "ON")
    message(FATAL_ERROR "OpenCL NVFP4 projection unavailable: ${_triad_opencl_unavailable}")
  endif()
  message(STATUS "OpenCL NVFP4 projection unavailable: ${_triad_opencl_unavailable}")
  return()
endif()

set(_triad_opencl_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/triad_opencl_nvfp4")
set(_triad_opencl_ptx "${_triad_opencl_dir}/triad_opencl_nvfp4.ptx")
set(_triad_opencl_header "${_triad_opencl_dir}/triad_opencl_nvfp4_ptx.hpp")
set(_triad_opencl_source "${PROJECT_SOURCE_DIR}/src/triad_opencl_nvfp4_kernels.cu")
set(_triad_opencl_embed "${PROJECT_SOURCE_DIR}/tools/triad_opencl_embed_ptx.py")
# Build trusted built-in device code offline. This target compiles only PTX
# and links neither a CUDA host runtime nor an OpenCL loader into the worker.
add_custom_command(OUTPUT "${_triad_opencl_header}"
  BYPRODUCTS "${_triad_opencl_ptx}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${_triad_opencl_dir}"
  COMMAND "${DATAPUMP_TRIAD_OPENCL_NVCC}" --ptx --std=c++17 -arch=sm_120a
    -o "${_triad_opencl_ptx}" "${_triad_opencl_source}"
  COMMAND "${Python3_EXECUTABLE}" "${_triad_opencl_embed}" "${_triad_opencl_ptx}" "${_triad_opencl_header}"
  DEPENDS "${_triad_opencl_source}" "${_triad_opencl_embed}" "${DATAPUMP_TRIAD_OPENCL_NVCC}"
  COMMENT "Compiling and embedding OpenCL NVFP4 projection PTX" VERBATIM)
add_custom_target(datapump_triad_opencl_nvfp4_ptx DEPENDS "${_triad_opencl_header}")
add_dependencies(datapump_triad_vulkan datapump_triad_opencl_nvfp4_ptx)
target_sources(datapump_triad_vulkan PRIVATE src/triad_opencl_nvfp4_projection.cpp)
target_include_directories(datapump_triad_vulkan PRIVATE "${_triad_opencl_dir}")
target_include_directories(datapump_triad_vulkan SYSTEM PRIVATE "${DATAPUMP_TRIAD_OPENCL_HEADERS}")
target_compile_definitions(datapump_triad_vulkan PRIVATE DATAPUMP_HAVE_TRIAD_OPENCL_NVFP4=1 CL_TARGET_OPENCL_VERSION=120)
if(DATAPUMP_TRIAD_OPENCL_LOADER_PATH)
  if(NOT IS_ABSOLUTE "${DATAPUMP_TRIAD_OPENCL_LOADER_PATH}" OR
      NOT EXISTS "${DATAPUMP_TRIAD_OPENCL_LOADER_PATH}" OR IS_DIRECTORY "${DATAPUMP_TRIAD_OPENCL_LOADER_PATH}")
    message(FATAL_ERROR "DATAPUMP_TRIAD_OPENCL_LOADER_PATH must name an existing absolute library path")
  endif()
  target_compile_definitions(datapump_triad_vulkan PRIVATE
    "DATAPUMP_TRIAD_OPENCL_LOADER_PATH=\"${DATAPUMP_TRIAD_OPENCL_LOADER_PATH}\"")
endif()
target_link_libraries(datapump_triad_vulkan PRIVATE ${CMAKE_DL_LIBS})

add_executable(test_triad_opencl_nvfp4 EXCLUDE_FROM_ALL tests/test_triad_opencl_nvfp4.cpp src/host/sandbox.cpp)
add_dependencies(test_triad_opencl_nvfp4 datapump_triad_opencl_nvfp4_ptx)
target_include_directories(test_triad_opencl_nvfp4 PRIVATE src include "${_triad_opencl_dir}")
target_link_libraries(test_triad_opencl_nvfp4 PRIVATE datapump_triad_vulkan Threads::Threads)
if(BUILD_TESTING AND DATAPUMP_TRIAD_OPENCL_NVFP4 STREQUAL "ON")
  add_test(NAME triad_opencl_nvfp4 COMMAND test_triad_opencl_nvfp4)
  set_tests_properties(triad_opencl_nvfp4 PROPERTIES LABELS "triad_gpu" TIMEOUT 30 RUN_SERIAL TRUE)
endif()
message(STATUS "OpenCL NVFP4 projection compiled for SM120 by NVCC ${_triad_opencl_nvcc_version}")
