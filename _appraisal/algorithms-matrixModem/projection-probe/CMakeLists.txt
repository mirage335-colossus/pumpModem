cmake_minimum_required(VERSION 3.24)
project(matrix_projection_probe LANGUAGES CXX CUDA)

if(CMAKE_CUDA_COMPILER_VERSION VERSION_LESS 12.8)
  message(FATAL_ERROR "The reused NVFP4 projection requires CUDA Toolkit 12.8 or newer")
endif()
find_package(CUDAToolkit 12.8 REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

set(REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." CACHE PATH "pumpModem source tree")
add_executable(matrix_projection_probe
  projection_probe.cu
  "${REPOSITORY_ROOT}/src/triad_nvfp4_projection.cu")
target_include_directories(matrix_projection_probe PRIVATE "${REPOSITORY_ROOT}/src")
target_link_libraries(matrix_projection_probe PRIVATE CUDA::cudart CUDA::cublasLt CUDA::cufft)

if(NOT CMAKE_CUDA_ARCHITECTURES)
  set_property(TARGET matrix_projection_probe PROPERTY CUDA_ARCHITECTURES 120)
endif()

