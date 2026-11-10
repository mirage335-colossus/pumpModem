if(DATAPUMP_BUILD_WASM)
  return()
endif()

# Bind transitive source conservatively, including native audio, crypto,
# scheduler, codec, and the vendored receive decoder. Keep this identity
# independent of which optional backend is compiled.
file(GLOB_RECURSE _triad_probe_sources CONFIGURE_DEPENDS RELATIVE "${PROJECT_SOURCE_DIR}"
  "src/*.cpp" "src/*.c" "src/*.hpp" "src/*.h"
  "include/datapump/*.hpp" "include/datapump/*.h" "cmake/*.cmake"
  "third_party/xz/src/*.c" "third_party/xz/src/*.h" "tools/triad_probe_*.hpp")
list(APPEND _triad_probe_sources
  CMakeLists.txt third_party/xz/UPSTREAM.sha256
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
string(SHA256 _triad_probe_build_sha
  "${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}/${CMAKE_CXX_COMPILER_ID}/${CMAKE_CXX_COMPILER_VERSION}/${CMAKE_CXX_FLAGS}/${CMAKE_CXX_FLAGS_DEBUG}/${CMAKE_CXX_FLAGS_RELEASE}/${CMAKE_CXX_FLAGS_RELWITHDEBINFO}/${CMAKE_CXX_FLAGS_MINSIZEREL}/${CMAKE_BUILD_TYPE}/${DATAPUMP_SANITIZERS}/${DATAPUMP_TRIAD_TENSOR}/${DATAPUMP_PORTABLE}")
if(NOT DEFINED _source_commit)
  set(_source_commit "unavailable")
endif()

add_executable(triad_receiver_probe EXCLUDE_FROM_ALL tools/triad-receiver-probe.cpp)
target_include_directories(triad_receiver_probe PRIVATE src tools)
target_link_libraries(triad_receiver_probe PRIVATE datapump_robust_session)
target_compile_definitions(triad_receiver_probe PRIVATE
  DATAPUMP_TRIAD_PROBE_SOURCE_SHA="${_triad_probe_source_sha}"
  DATAPUMP_TRIAD_PROBE_SOURCE_COMMIT="${_source_commit}"
  DATAPUMP_TRIAD_PROBE_COMPILER="${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}"
  DATAPUMP_TRIAD_PROBE_CONFIG="$<CONFIG>"
  DATAPUMP_TRIAD_PROBE_BUILD_SHA="${_triad_probe_build_sha}")

if(BUILD_TESTING AND Python3_Interpreter_FOUND)
  add_test(NAME triad_receiver_probe COMMAND "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/test_triad_receiver_probe.py" "$<TARGET_FILE:triad_receiver_probe>")
  add_custom_target(test_triad_receiver_probe DEPENDS triad_receiver_probe)
  set_tests_properties(triad_receiver_probe PROPERTIES LABELS "triad" TIMEOUT 50)
endif()
