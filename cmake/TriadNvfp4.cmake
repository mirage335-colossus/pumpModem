set(DATAPUMP_TRIAD_NVFP4 "AUTO" CACHE STRING "Build the SM120 NVFP4 receiver projection (AUTO, ON, or OFF)")
set_property(CACHE DATAPUMP_TRIAD_NVFP4 PROPERTY STRINGS AUTO ON OFF)

if(DATAPUMP_TRIAD_NVFP4 STREQUAL "OFF")
  return()
elseif(DATAPUMP_TRIAD_NVFP4 STREQUAL "AUTO")
  # Do not probe for or link CUDA in a normal CPU or Vulkan build.
  if(NOT (DATAPUMP_TRIAD_TENSOR AND CAFFE2_USE_CUDA AND NOT CAFFE2_USE_ROCM))
    return()
  endif()
elseif(NOT DATAPUMP_TRIAD_NVFP4 STREQUAL "ON")
  message(FATAL_ERROR "DATAPUMP_TRIAD_NVFP4 must be AUTO, ON, or OFF")
endif()

if(DATAPUMP_BUILD_WASM)
  message(FATAL_ERROR "NVFP4 projection requires a native CUDA build")
endif()
find_package(CUDAToolkit 12.8 QUIET)
if(NOT CUDAToolkit_FOUND OR NOT TARGET CUDA::cublasLt)
  if(DATAPUMP_TRIAD_NVFP4 STREQUAL "ON")
    message(FATAL_ERROR "NVFP4 projection requires CUDA 12.8 or newer and cuBLASLt")
  endif()
  message(STATUS "NVFP4 projection unavailable: CUDA 12.8 and cuBLASLt are required")
  return()
endif()

if(NOT CMAKE_CUDA_COMPILER_LOADED)
  if(NOT CMAKE_CUDA_COMPILER AND "$ENV{CUDACXX}" STREQUAL "")
    set(CMAKE_CUDA_COMPILER "${CUDAToolkit_NVCC_EXECUTABLE}")
  endif()
  enable_language(CUDA)
endif()
if(NOT CMAKE_CUDA_COMPILER_ID STREQUAL "NVIDIA" OR CMAKE_CUDA_COMPILER_VERSION VERSION_LESS 12.8)
  if(DATAPUMP_TRIAD_NVFP4 STREQUAL "ON")
    message(FATAL_ERROR "NVFP4 projection requires NVCC 12.8 or newer")
  endif()
  message(STATUS "NVFP4 projection unavailable: NVCC 12.8 or newer is required")
  return()
endif()

include(CheckCXXSourceCompiles)
include(CMakePushCheckState)
cmake_push_check_state(RESET)
set(CMAKE_REQUIRED_LIBRARIES CUDA::cublasLt)
# The CUDA 12.8 GA API exposes the FP4 enum but SM120 support starts at
# CUDA 12.8 Update 1, which supplies cuBLASLt 12.8.4.
check_cxx_source_compiles([=[
  #include <cublasLt.h>
  #if !defined(CUBLAS_VERSION) || CUBLAS_VERSION < 120804
    #error cuBLASLt 12.8.4 is required for SM120 NVFP4
  #endif
  int main() {
    return cublasLtGetVersion() < 120804 ||
           CUBLASLT_MATMUL_MATRIX_SCALE_VEC16_UE4M3 == CUBLASLT_MATMUL_MATRIX_SCALE_SCALAR_32F;
  }
]=] DATAPUMP_HAVE_SM120_CUBLASLT)
cmake_pop_check_state()
if(NOT DATAPUMP_HAVE_SM120_CUBLASLT)
  if(DATAPUMP_TRIAD_NVFP4 STREQUAL "ON")
    message(FATAL_ERROR "NVFP4 projection requires cuBLASLt 12.8.4 or newer (CUDA 12.8 Update 1)")
  endif()
  message(STATUS "NVFP4 projection unavailable: cuBLASLt 12.8.4 or newer is required")
  return()
endif()

add_library(datapump_triad_nvfp4 STATIC src/triad_nvfp4_projection.cu)
set_target_properties(datapump_triad_nvfp4 PROPERTIES
  CUDA_STANDARD 17 CUDA_STANDARD_REQUIRED ON CUDA_ARCHITECTURES OFF CUDA_RUNTIME_LIBRARY None)
# Use the architecture-specific Blackwell conversion instructions. Keeping
# this flag on this target preserves other CUDA backends' architecture lists.
set(DATAPUMP_TRIAD_NVFP4_SM "120a")
target_compile_options(datapump_triad_nvfp4 PRIVATE
  "--generate-code=arch=compute_${DATAPUMP_TRIAD_NVFP4_SM},code=sm_${DATAPUMP_TRIAD_NVFP4_SM}")
target_link_libraries(datapump_triad_nvfp4 PRIVATE CUDA::cudart CUDA::cublasLt)
if(TARGET datapump_triad_tensor AND CAFFE2_USE_CUDA AND NOT CAFFE2_USE_ROCM)
  target_link_libraries(datapump_triad_tensor PRIVATE datapump_triad_nvfp4)
  target_compile_definitions(datapump_triad_tensor PRIVATE DATAPUMP_HAVE_TRIAD_NVFP4=1)
endif()

add_executable(test_triad_nvfp4_projection EXCLUDE_FROM_ALL tests/test_triad_nvfp4_projection.cpp)
set_target_properties(test_triad_nvfp4_projection PROPERTIES CUDA_RUNTIME_LIBRARY None)
target_include_directories(test_triad_nvfp4_projection PRIVATE src)
target_link_libraries(test_triad_nvfp4_projection PRIVATE datapump_triad_nvfp4 CUDA::cudart CUDA::cublasLt)
if(BUILD_TESTING AND DATAPUMP_TRIAD_NVFP4 STREQUAL "ON")
  add_test(NAME triad_nvfp4_projection COMMAND test_triad_nvfp4_projection)
  set_tests_properties(triad_nvfp4_projection PROPERTIES LABELS "triad_gpu" TIMEOUT 30 RUN_SERIAL TRUE)
endif()
message(STATUS "NVFP4 projection compiled for SM120")
