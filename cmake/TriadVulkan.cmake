option(DATAPUMP_TRIAD_VULKAN "Build the optional native Vulkan receiver backend" OFF)
option(DATAPUMP_TRIAD_VULKAN_REQUIRE_DEVICE "Require actual GPU execution in the Vulkan receiver check" OFF)

if(DATAPUMP_TRIAD_VULKAN_REQUIRE_DEVICE AND NOT DATAPUMP_TRIAD_VULKAN)
  message(FATAL_ERROR "DATAPUMP_TRIAD_VULKAN_REQUIRE_DEVICE requires DATAPUMP_TRIAD_VULKAN")
endif()
if(NOT DATAPUMP_TRIAD_VULKAN)
  return()
endif()
if(DATAPUMP_BUILD_WASM)
  message(FATAL_ERROR "The Vulkan receiver backend requires a native build")
endif()

# Development packages are supplied explicitly. Configuration never downloads
# shaders, drivers or libraries, and the operating path compiles GLSL in-process.
find_package(Vulkan REQUIRED)
find_package(SPIRV-Tools CONFIG REQUIRED)
find_package(SPIRV-Tools-opt CONFIG REQUIRED)
find_package(SPIRV-Tools-link CONFIG REQUIRED)
# Debian's static package exports this name while glslang refers to SPIRV-Tools.
if(NOT TARGET SPIRV-Tools AND TARGET SPIRV-Tools-static)
  add_library(SPIRV-Tools ALIAS SPIRV-Tools-static)
endif()
find_package(glslang CONFIG REQUIRED)
find_path(DATAPUMP_GLSLANG_C_INCLUDE_DIR glslang_c_interface.h
  PATH_SUFFIXES glslang/Include REQUIRED)

set(_triad_vkfft_root "${PROJECT_SOURCE_DIR}/third_party/vkfft")
file(STRINGS "${_triad_vkfft_root}/UPSTREAM.sha256" _triad_vkfft_inventory)
foreach(_entry IN LISTS _triad_vkfft_inventory)
  if(NOT _entry MATCHES "^([0-9a-f]+)  (LICENSE|vkFFT/[A-Za-z0-9_./-]+\\.h)$")
    message(FATAL_ERROR "Malformed vendored VkFFT inventory entry")
  endif()
  set(_expected "${CMAKE_MATCH_1}")
  set(_relative "${CMAKE_MATCH_2}")
  if(_relative MATCHES "(^|/)\\.\\.(/|$)")
    message(FATAL_ERROR "Redirected vendored VkFFT inventory path")
  endif()
  set(_path "${_triad_vkfft_root}/${_relative}")
  if(NOT EXISTS "${_path}" OR IS_SYMLINK "${_path}")
    message(FATAL_ERROR "Missing or redirected vendored VkFFT file: ${_path}")
  endif()
  file(SHA256 "${_path}" _actual)
  if(NOT _actual STREQUAL _expected)
    message(FATAL_ERROR "Vendored VkFFT checksum mismatch: ${_path}")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_path}")
endforeach()

# The backend, its shared device core and the fine-stage device sessions.
add_library(datapump_triad_vulkan STATIC src/triad_vulkan_backend.cpp src/triad_vulkan_core.cpp
  src/triad_vulkan_gain.cpp src/triad_vulkan_drift.cpp src/triad_vulkan_varying.cpp)
target_include_directories(datapump_triad_vulkan PUBLIC include PRIVATE src)
target_include_directories(datapump_triad_vulkan SYSTEM PRIVATE
  "${_triad_vkfft_root}/vkFFT" "${DATAPUMP_GLSLANG_C_INCLUDE_DIR}")
target_compile_features(datapump_triad_vulkan PUBLIC cxx_std_20)
target_compile_definitions(datapump_triad_vulkan PRIVATE VKFFT_BACKEND=0)
include(cmake/TriadOpenClNvfp4.cmake)
target_link_libraries(datapump_triad_vulkan PRIVATE Vulkan::Vulkan
  glslang::glslang glslang::SPIRV glslang::glslang-default-resource-limits)
target_link_libraries(datapump_triad_receiver PRIVATE datapump_triad_vulkan)
target_compile_definitions(datapump_triad_receiver PUBLIC DATAPUMP_HAVE_TRIAD_VULKAN=1)
if(CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
  target_compile_options(datapump_triad_vulkan PRIVATE -ffp-contract=off -fno-fast-math)
elseif(MSVC)
  target_compile_options(datapump_triad_vulkan PRIVATE /fp:strict)
endif()

add_executable(test_triad_vulkan EXCLUDE_FROM_ALL tests/test_triad_vulkan.cpp
  tests/triad_vulkan_stage_checks.hpp tests/triad_signed_gain_fixture.hpp tests/triad_vulkan_gain_checks.cpp
  tests/triad_vulkan_drift_checks.cpp tests/triad_vulkan_varying_checks.cpp)
target_include_directories(test_triad_vulkan PRIVATE src tools)
target_link_libraries(test_triad_vulkan PRIVATE datapump_triad_receiver)
if(BUILD_TESTING)
  add_test(NAME triad_vulkan_plan COMMAND test_triad_vulkan --plan)
  add_custom_target(test_triad_vulkan_plan DEPENDS test_triad_vulkan)
  set_tests_properties(triad_vulkan_plan PROPERTIES LABELS "triad" TIMEOUT 30)
  if(DATAPUMP_TRIAD_VULKAN_REQUIRE_DEVICE)
    add_test(NAME triad_vulkan COMMAND test_triad_vulkan)
    add_test(NAME triad_vulkan_host_scoring COMMAND test_triad_vulkan --host-scoring)
    add_test(NAME triad_vulkan_workspace_admission COMMAND test_triad_vulkan --workspace-admission)
    add_test(NAME triad_vulkan_segmented COMMAND test_triad_vulkan --segmented)
    # The core's fine-stage generalizations and the fine-stage device
    # sessions on the device. The device-free stage policy check,
    # triad_vulkan_stage_policy, is in every build (CMakeLists.txt).
    add_test(NAME triad_vulkan_core COMMAND test_triad_vulkan --core)
    add_test(NAME triad_vulkan_gain_rank COMMAND test_triad_vulkan --gain-rank)
    add_test(NAME triad_vulkan_drift COMMAND test_triad_vulkan --drift)
    add_test(NAME triad_vulkan_varying COMMAND test_triad_vulkan --varying)
    add_custom_target(test_triad_vulkan_host_scoring DEPENDS test_triad_vulkan)
    set_tests_properties(triad_vulkan triad_vulkan_host_scoring triad_vulkan_workspace_admission triad_vulkan_segmented
      triad_vulkan_core triad_vulkan_gain_rank triad_vulkan_drift triad_vulkan_varying
      PROPERTIES LABELS "triad_gpu" TIMEOUT 120 RUN_SERIAL TRUE)
  endif()
endif()
