if(DATAPUMP_BUILD_WASM)
  return()
endif()

# Bind transitive source conservatively, including native audio, crypto,
# scheduler, codec, and the vendored receive decoder. Keep this identity
# independent of which optional backend is compiled.
file(GLOB_RECURSE _triad_probe_sources CONFIGURE_DEPENDS RELATIVE "${PROJECT_SOURCE_DIR}"
  "src/*.cpp" "src/*.c" "src/*.hpp" "src/*.h" "src/*.cu" "src/*.cuh"
  "include/datapump/*.hpp" "include/datapump/*.h" "cmake/*.cmake"
  "third_party/xz/src/*.c" "third_party/xz/src/*.h" "tools/triad_probe_*.hpp"
  "third_party/vkfft/vkFFT/*.h")
list(APPEND _triad_probe_sources
  CMakeLists.txt third_party/xz/UPSTREAM.sha256 third_party/vkfft/UPSTREAM.sha256
  tools/triad-receiver-probe.cpp)
list(REMOVE_DUPLICATES _triad_probe_sources)
list(SORT _triad_probe_sources)
set(_triad_probe_manifest "")
foreach(_triad_probe_file IN LISTS _triad_probe_sources)
  set(_triad_probe_absolute "${PROJECT_SOURCE_DIR}/${_triad_probe_file}")
  file(SHA256 "${_triad_probe_absolute}" _triad_probe_file_sha)
  string(APPEND _triad_probe_manifest "${_triad_probe_file_sha}  ${_triad_probe_file}\n")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_triad_probe_absolute}")
endforeach()
string(SHA256 _triad_probe_source_sha "${_triad_probe_manifest}")
set(_triad_probe_build_identity
  "${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}/${CMAKE_CXX_COMPILER_ID}/${CMAKE_CXX_COMPILER_VERSION}/${CMAKE_CXX_FLAGS}/${CMAKE_CXX_FLAGS_DEBUG}/${CMAKE_CXX_FLAGS_RELEASE}/${CMAKE_CXX_FLAGS_RELWITHDEBINFO}/${CMAKE_CXX_FLAGS_MINSIZEREL}/${CMAKE_BUILD_TYPE}/${DATAPUMP_SANITIZERS}/${DATAPUMP_TRIAD_TENSOR}/${DATAPUMP_PORTABLE}")
if(DATAPUMP_TRIAD_VULKAN)
  string(APPEND _triad_probe_build_identity
    "/Vulkan-${Vulkan_VERSION}/glslang-${glslang_VERSION}/${DATAPUMP_TRIAD_VULKAN_REQUIRE_DEVICE}")
endif()
if(CMAKE_CUDA_COMPILER_LOADED AND DATAPUMP_TRIAD_TENSOR)
  string(APPEND _triad_probe_build_identity
    "/${CMAKE_CUDA_COMPILER_ID}/${CMAKE_CUDA_COMPILER_VERSION}/${CMAKE_CUDA_ARCHITECTURES}/${CMAKE_CUDA_FLAGS}/${CMAKE_CUDA_FLAGS_DEBUG}/${CMAKE_CUDA_FLAGS_RELEASE}/${CMAKE_CUDA_FLAGS_RELWITHDEBINFO}/${CMAKE_CUDA_FLAGS_MINSIZEREL}")
endif()
string(SHA256 _triad_probe_build_sha "${_triad_probe_build_identity}")
if(NOT DEFINED _source_commit)
  set(_source_commit "unavailable")
endif()

add_executable(triad_receiver_probe EXCLUDE_FROM_ALL tools/triad-receiver-probe.cpp)
target_include_directories(triad_receiver_probe PRIVATE src tools)
target_link_libraries(triad_receiver_probe PRIVATE datapump_robust_session)
set(_triad_probe_identity_definitions
  DATAPUMP_TRIAD_PROBE_SOURCE_SHA="${_triad_probe_source_sha}"
  DATAPUMP_TRIAD_PROBE_SOURCE_COMMIT="${_source_commit}"
  DATAPUMP_TRIAD_PROBE_COMPILER="${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}"
  DATAPUMP_TRIAD_PROBE_CONFIG="$<CONFIG>"
  DATAPUMP_TRIAD_PROBE_BUILD_SHA="${_triad_probe_build_sha}")
target_compile_definitions(triad_receiver_probe PRIVATE ${_triad_probe_identity_definitions})

# Component measurements exercise the same production libraries, but also
# bind the separate numerical fixture and timing boundary to its exact source.
if(TARGET test_triad_tensor)
  set(_triad_component_source "${PROJECT_SOURCE_DIR}/tests/test_triad_tensor.cpp")
  file(SHA256 "${_triad_component_source}" _triad_component_source_sha)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_triad_component_source}")
  target_include_directories(test_triad_tensor PRIVATE tools)
  target_compile_definitions(test_triad_tensor PRIVATE ${_triad_probe_identity_definitions}
    DATAPUMP_TRIAD_COMPONENT_SOURCE_SHA="${_triad_component_source_sha}")
endif()

if(TARGET test_triad_vulkan)
  set(_triad_vulkan_component_source "${PROJECT_SOURCE_DIR}/tests/test_triad_vulkan.cpp")
  file(SHA256 "${_triad_vulkan_component_source}" _triad_vulkan_component_source_sha)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_triad_vulkan_component_source}")
  target_compile_definitions(test_triad_vulkan PRIVATE ${_triad_probe_identity_definitions}
    DATAPUMP_TRIAD_COMPONENT_SOURCE_SHA="${_triad_vulkan_component_source_sha}")
endif()

if(BUILD_TESTING AND Python3_Interpreter_FOUND)
  add_test(NAME triad_receiver_probe COMMAND "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/test_triad_receiver_probe.py" "$<TARGET_FILE:triad_receiver_probe>")
  add_custom_target(test_triad_receiver_probe DEPENDS triad_receiver_probe)
  set_tests_properties(triad_receiver_probe PROPERTIES LABELS "triad" TIMEOUT 50)
endif()
