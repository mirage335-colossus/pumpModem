cmake_minimum_required(VERSION 3.21)
if(NOT TARGET datapump_triad_receiver)
  message(FATAL_ERROR "Use this directory's build.sh; the appraisal links the canonical root targets.")
endif()

set(_hf_root "${CMAKE_CURRENT_LIST_DIR}")
add_library(high_fidelity_channel STATIC "${_hf_root}/src/channel.cpp" "${_hf_root}/src/scenarios.cpp"
  "${_hf_root}/src/hf_scenarios.cpp" "${_hf_root}/src/hf_ionosphere.cpp"
  "${_hf_root}/src/hf_history.cpp" "${_hf_root}/src/hf_phase_bank.cpp"
  "${_hf_root}/src/hf_replay.cpp" "${_hf_root}/src/lunar_spectral.cpp")
target_include_directories(high_fidelity_channel PUBLIC "${_hf_root}/include" PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(high_fidelity_channel PUBLIC datapump_triad)
add_executable(high_fidelity_appraisal "${_hf_root}/src/main.cpp" "${_hf_root}/src/vlf_diagnostic.cpp")
target_include_directories(high_fidelity_appraisal PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/tools")
target_link_libraries(high_fidelity_appraisal PRIVATE high_fidelity_channel datapump_triad_receiver)
get_target_property(_hf_identity triad_receiver_probe COMPILE_DEFINITIONS)
target_compile_definitions(high_fidelity_appraisal PRIVATE ${_hf_identity})

file(GLOB_RECURSE _hf_sources CONFIGURE_DEPENDS RELATIVE "${_hf_root}"
  "${_hf_root}/src/*.cpp" "${_hf_root}/src/*.hpp" "${_hf_root}/include/*.hpp" "${_hf_root}/cmake/*.cmake" "${_hf_root}/tests/*.cpp" "${_hf_root}/tests/*.py"
  "${_hf_root}/fixtures/*.hpp")
file(GLOB _hf_scripts CONFIGURE_DEPENDS RELATIVE "${_hf_root}" "${_hf_root}/*.py")
list(APPEND _hf_sources ${_hf_scripts})
file(GLOB_RECURSE _hf_comparison_sources CONFIGURE_DEPENDS RELATIVE "${_hf_root}"
  "${_hf_root}/competitor/*.cpp" "${_hf_root}/competitor/*.c" "${_hf_root}/competitor/*.hpp"
  "${_hf_root}/competitor/*.py" "${_hf_root}/competitor/*.cmake" "${_hf_root}/competitor/*.json"
  "${_hf_root}/competitor/CMakeLists.txt")
list(APPEND _hf_sources ${_hf_comparison_sources})
list(APPEND _hf_sources CMakeLists.txt build.sh)
list(SORT _hf_sources)
set(_hf_manifest "")
foreach(_hf_file IN LISTS _hf_sources)
  file(SHA256 "${_hf_root}/${_hf_file}" _hf_hash)
  string(APPEND _hf_manifest "${_hf_hash}  ${_hf_file}\n")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_hf_root}/${_hf_file}")
endforeach()
string(SHA256 _hf_source_sha "${_hf_manifest}")
target_compile_definitions(high_fidelity_appraisal PRIVATE APPRAISAL_SOURCE_SHA="${_hf_source_sha}")
set_target_properties(high_fidelity_appraisal PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(high_fidelity_channel PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(high_fidelity_appraisal PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
  foreach(_hf_test channel scenarios hf_scenarios hf_ionosphere hf_history hf_phase_bank hf_replay impairment_observables lunar_spectral)
    add_executable(high_fidelity_test_${_hf_test} "${_hf_root}/tests/test_${_hf_test}.cpp")
    target_include_directories(high_fidelity_test_${_hf_test} PRIVATE "${PROJECT_SOURCE_DIR}/src" "${_hf_root}/src")
    target_link_libraries(high_fidelity_test_${_hf_test} PRIVATE high_fidelity_channel)
    add_test(NAME high_fidelity_${_hf_test} COMMAND high_fidelity_test_${_hf_test})
    set_tests_properties(high_fidelity_${_hf_test} PROPERTIES LABELS "appraisal" TIMEOUT 45)
  endforeach()
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(NAME high_fidelity_integration COMMAND "${Python3_EXECUTABLE}"
    "${_hf_root}/tests/test_integration.py" "$<TARGET_FILE:high_fidelity_appraisal>")
  set_tests_properties(high_fidelity_integration PROPERTIES LABELS "appraisal" TIMEOUT 180)
  add_test(NAME high_fidelity_runner COMMAND "${Python3_EXECUTABLE}"
    "${_hf_root}/tests/test_runner.py")
  set_tests_properties(high_fidelity_runner PROPERTIES LABELS "appraisal" TIMEOUT 45)
  add_custom_target(high_fidelity_checks DEPENDS high_fidelity_appraisal
    high_fidelity_test_channel high_fidelity_test_scenarios
    high_fidelity_test_hf_scenarios high_fidelity_test_hf_ionosphere high_fidelity_test_hf_history
    high_fidelity_test_hf_phase_bank high_fidelity_test_hf_replay
    high_fidelity_test_impairment_observables high_fidelity_test_lunar_spectral)
endif()

include("${_hf_root}/competitor/CMakeLists.txt")
