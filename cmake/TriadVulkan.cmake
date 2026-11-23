set(_triad_vulkan_supported OFF)
if(NOT DATAPUMP_BUILD_WASM AND CMAKE_SYSTEM_NAME MATCHES "^(Linux|Windows)$")
  set(_triad_vulkan_supported ON)
endif()
set(DATAPUMP_TRIAD_VULKAN "${_triad_vulkan_supported}" CACHE STRING
  "Build the native Vulkan receiver backend (ON, OFF for nonportable development, AUTO for the platform default)")
set_property(CACHE DATAPUMP_TRIAD_VULKAN PROPERTY STRINGS AUTO ON OFF)
if(NOT DATAPUMP_TRIAD_VULKAN MATCHES "^(AUTO|ON|OFF)$")
  message(FATAL_ERROR "DATAPUMP_TRIAD_VULKAN must be AUTO, ON, or OFF (got '${DATAPUMP_TRIAD_VULKAN}')")
endif()
option(DATAPUMP_TRIAD_VULKAN_REQUIRE_DEVICE "Require actual GPU execution in the Vulkan receiver check" OFF)

if(DATAPUMP_TRIAD_VULKAN_REQUIRE_DEVICE AND DATAPUMP_TRIAD_VULKAN STREQUAL "OFF")
  message(FATAL_ERROR "DATAPUMP_TRIAD_VULKAN_REQUIRE_DEVICE requires DATAPUMP_TRIAD_VULKAN")
endif()
if(NOT _triad_vulkan_supported)
  if(DATAPUMP_TRIAD_VULKAN STREQUAL "ON" OR DATAPUMP_TRIAD_VULKAN_REQUIRE_DEVICE)
    message(FATAL_ERROR "DATAPUMP_TRIAD_VULKAN=ON requires a supported native Linux or Windows build; browser Wasm and this target platform cannot load the native Vulkan backend")
  endif()
  set(DATAPUMP_TRIAD_VULKAN_ENABLED OFF)
  return()
endif()
if(DATAPUMP_TRIAD_VULKAN STREQUAL "OFF")
  if(DATAPUMP_PORTABLE)
    message(FATAL_ERROR "Portable Linux and Windows builds require Vulkan receiver support. Select CPU processing at runtime, or set DATAPUMP_PORTABLE=OFF for a CPU-only development build.")
  endif()
  set(DATAPUMP_TRIAD_VULKAN_ENABLED OFF)
  return()
endif()

# Development packages are supplied explicitly. Configuration never downloads
# shaders, drivers or libraries, and the operating path compiles GLSL in-process.
set(_triad_saved_cmake_prefix_path "${CMAKE_PREFIX_PATH}")
set(_triad_vulkan_default_sysroot
  "${PROJECT_SOURCE_DIR}/third_party/build-support/cache/sysroot/usr")
if(NOT DATAPUMP_SDK_ROOT AND NOT CMAKE_CROSSCOMPILING
    AND CMAKE_SYSTEM_NAME STREQUAL "Linux"
    AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$"
    AND (NOT DATAPUMP_DEPENDENCY_PREFIX OR DATAPUMP_DEPENDENCY_PREFIX STREQUAL _triad_vulkan_default_sysroot))
  # Native x86_64 diagnostics may use the already prepared host prefix. Never
  # apply its libraries to Windows, another architecture, a cross build or SDK.
  set(_triad_vulkan_local_prefix
    "${PROJECT_SOURCE_DIR}/third_party/build-support/cache/vulkan-native/usr-prefix/usr")
  if(EXISTS "${_triad_vulkan_local_prefix}/include/vulkan/vulkan.h")
    # Append after caller-provided CMAKE_PREFIX_PATH entries so explicit
    # dependency choices take precedence over this optional local fallback.
    list(APPEND CMAKE_PREFIX_PATH "${_triad_vulkan_local_prefix}")
  endif()
endif()
if(NOT DATAPUMP_SDK_ROOT AND NOT CMAKE_CROSSCOMPILING)
  # Do not use an environment Vulkan SDK to populate a cross or relocatable
  # source-SDK build. Native local installations may provide their own prefix.
  set(_triad_vulkan_header_env_hints "$ENV{VULKAN_SDK}" "$ENV{VULKAN_SDK}/Include")
else()
  set(_triad_vulkan_header_env_hints "")
endif()
set(_triad_vulkan_header_paths "")
if(DATAPUMP_DEPENDENCY_PREFIX)
  list(APPEND _triad_vulkan_header_paths "${DATAPUMP_DEPENDENCY_PREFIX}")
endif()
if(_triad_vulkan_local_prefix)
  list(APPEND _triad_vulkan_header_paths "${_triad_vulkan_local_prefix}")
endif()
find_path(DATAPUMP_VULKAN_INCLUDE_DIR NAMES vulkan/vulkan.h
  HINTS ${_triad_vulkan_header_env_hints}
  PATHS ${_triad_vulkan_header_paths} PATH_SUFFIXES include Include)
set(DATAPUMP_VULKAN_HEADERS_VERSION "unknown")
if(DATAPUMP_VULKAN_INCLUDE_DIR AND EXISTS "${DATAPUMP_VULKAN_INCLUDE_DIR}/vulkan/vulkan_core.h")
  file(STRINGS "${DATAPUMP_VULKAN_INCLUDE_DIR}/vulkan/vulkan_core.h"
    _triad_vulkan_header_version REGEX "^#define VK_HEADER_VERSION ")
  if(_triad_vulkan_header_version MATCHES "VK_HEADER_VERSION ([0-9]+)")
    set(DATAPUMP_VULKAN_HEADERS_VERSION "${CMAKE_MATCH_1}")
  endif()
endif()
find_package(SPIRV-Tools CONFIG QUIET)
find_package(SPIRV-Tools-opt CONFIG QUIET)
find_package(SPIRV-Tools-link CONFIG QUIET)
find_package(glslang CONFIG QUIET)
# Ubuntu 22.04 ships glslang 11.8 as generated target exports without a
# package config file. Load those exports directly when they are discoverable
# under the selected native dependency prefix or the system multiarch path.
if(NOT glslang_FOUND AND NOT TARGET glslang::glslang
    AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_CROSSCOMPILING
    AND NOT DATAPUMP_SDK_ROOT)
  set(_triad_glslang_cmake_paths "${DATAPUMP_DEPENDENCY_PREFIX}")
  if(_triad_vulkan_local_prefix)
    list(APPEND _triad_glslang_cmake_paths "${_triad_vulkan_local_prefix}")
  endif()
  list(APPEND _triad_glslang_cmake_paths "${CMAKE_PREFIX_PATH}" /usr)
  find_path(_triad_glslang_targets_dir NAMES glslangTargets.cmake
    PATHS ${_triad_glslang_cmake_paths}
    PATH_SUFFIXES lib/${CMAKE_LIBRARY_ARCHITECTURE}/cmake lib64/cmake lib/cmake)
  if(_triad_glslang_targets_dir)
    foreach(_legacy_export OSDependentTargets.cmake OGLCompilerTargets.cmake
        glslangTargets.cmake SPIRVTargets.cmake
        glslang-default-resource-limitsTargets.cmake)
      if(NOT EXISTS "${_triad_glslang_targets_dir}/${_legacy_export}")
        set(_triad_glslang_targets_dir "")
        break()
      endif()
    endforeach()
  endif()
  if(_triad_glslang_targets_dir)
    include("${_triad_glslang_targets_dir}/OSDependentTargets.cmake")
    include("${_triad_glslang_targets_dir}/OGLCompilerTargets.cmake")
    include("${_triad_glslang_targets_dir}/glslangTargets.cmake")
    include("${_triad_glslang_targets_dir}/SPIRVTargets.cmake")
    include("${_triad_glslang_targets_dir}/glslang-default-resource-limitsTargets.cmake")
    foreach(_legacy_target glslang SPIRV glslang-default-resource-limits
        MachineIndependent OSDependent OGLCompiler GenericCodeGen SPVRemapper)
      if(TARGET "${_legacy_target}" AND NOT TARGET "glslang::${_legacy_target}")
        add_library("glslang::${_legacy_target}" ALIAS "${_legacy_target}")
      endif()
    endforeach()
    set(glslang_FOUND TRUE)
  endif()
endif()
set(CMAKE_PREFIX_PATH "${_triad_saved_cmake_prefix_path}")
set(_triad_glslang_include_hints "")
if(TARGET glslang::glslang)
  get_target_property(_triad_glslang_include_hints glslang::glslang INTERFACE_INCLUDE_DIRECTORIES)
endif()
find_path(DATAPUMP_GLSLANG_C_INCLUDE_DIR glslang_c_interface.h
  HINTS ${_triad_glslang_include_hints}
  PATHS ${_triad_vulkan_header_paths} PATH_SUFFIXES glslang/Include)
set(_triad_vulkan_missing "")
if(NOT DATAPUMP_VULKAN_INCLUDE_DIR)
  list(APPEND _triad_vulkan_missing "Vulkan headers (vulkan/vulkan.h)")
endif()
foreach(_package SPIRV-Tools SPIRV-Tools-opt SPIRV-Tools-link glslang)
  if(NOT ${_package}_FOUND)
    list(APPEND _triad_vulkan_missing "${_package} CMake package")
  endif()
endforeach()
if(NOT DATAPUMP_GLSLANG_C_INCLUDE_DIR)
  list(APPEND _triad_vulkan_missing "glslang_c_interface.h")
endif()
if(_triad_vulkan_missing)
  message(FATAL_ERROR "The Vulkan receiver build requires ${_triad_vulkan_missing}; install the matching Vulkan headers, glslang and SPIRV-Tools development packages")
endif()
# Debian's static package exports this name while glslang refers to SPIRV-Tools.
if(NOT TARGET SPIRV-Tools AND TARGET SPIRV-Tools-static)
  add_library(SPIRV-Tools ALIAS SPIRV-Tools-static)
endif()
foreach(_target glslang::glslang glslang::SPIRV glslang::glslang-default-resource-limits
    SPIRV-Tools SPIRV-Tools-opt SPIRV-Tools-link)
  if(NOT TARGET ${_target})
    list(APPEND _triad_vulkan_missing "imported target ${_target}")
  endif()
endforeach()
if(_triad_vulkan_missing)
  message(FATAL_ERROR "The Vulkan receiver build requires ${_triad_vulkan_missing}; install compatible glslang and SPIRV-Tools development packages")
endif()
set(DATAPUMP_TRIAD_VULKAN_ENABLED ON)

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
  src/triad_vulkan_loader.cpp
  src/triad_vulkan_gain.cpp src/triad_vulkan_drift.cpp src/triad_vulkan_varying.cpp)
target_include_directories(datapump_triad_vulkan PUBLIC include PRIVATE src)
target_include_directories(datapump_triad_vulkan SYSTEM PRIVATE
  "${_triad_vkfft_root}/vkFFT" "${DATAPUMP_GLSLANG_C_INCLUDE_DIR}" "${DATAPUMP_VULKAN_INCLUDE_DIR}")
target_compile_features(datapump_triad_vulkan PUBLIC cxx_std_20)
target_compile_definitions(datapump_triad_vulkan PRIVATE VKFFT_BACKEND=0)
include(cmake/TriadOpenClNvfp4.cmake)
target_link_libraries(datapump_triad_vulkan PRIVATE
  glslang::glslang glslang::SPIRV glslang::glslang-default-resource-limits)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(datapump_triad_vulkan PRIVATE ${CMAKE_DL_LIBS})
endif()
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
