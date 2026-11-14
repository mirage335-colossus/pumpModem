cmake_minimum_required(VERSION 3.21)
if(NOT TARGET datapump_triad_receiver)
  message(FATAL_ERROR "Use this directory's build.sh; the appraisal links the canonical root targets.")
endif()

set(_hf_root "${CMAKE_CURRENT_LIST_DIR}")
add_library(high_fidelity_channel STATIC "${_hf_root}/src/channel.cpp" "${_hf_root}/src/scenarios.cpp"
  "${_hf_root}/src/hf_scenarios.cpp" "${_hf_root}/src/hf_ionosphere.cpp" "${_hf_root}/src/hf_design.cpp"
  "${_hf_root}/src/hf_history.cpp" "${_hf_root}/src/hf_phase_bank.cpp" "${_hf_root}/src/hf_public_statistics.cpp"
  "${_hf_root}/src/hf_phase_refinement.cpp"
  "${_hf_root}/src/hf_replay.cpp" "${_hf_root}/src/lunar_spectral.cpp")
target_include_directories(high_fidelity_channel PUBLIC "${_hf_root}/include" PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(high_fidelity_channel PUBLIC datapump_triad)
add_library(high_fidelity_gui_profile STATIC "${_hf_root}/src/hf_gui_profile.cpp")
target_include_directories(high_fidelity_gui_profile PUBLIC "${_hf_root}/include")
target_link_libraries(high_fidelity_gui_profile PUBLIC datapump_robust_session)
add_library(high_fidelity_fullwave STATIC "${_hf_root}/src/hf_fullwave.cpp" "${_hf_root}/src/hf_fullwave_kernel.cpp")
target_include_directories(high_fidelity_fullwave PUBLIC "${_hf_root}/include")
target_compile_features(high_fidelity_fullwave PUBLIC cxx_std_20)
add_library(high_fidelity_public_receiver STATIC "${_hf_root}/src/hf_public_diagnostic.cpp"
  "${_hf_root}/src/hf_gui_diagnostic.cpp" "${_hf_root}/src/hf_phase_filter.cpp")
target_include_directories(high_fidelity_public_receiver PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/tools")
target_link_libraries(high_fidelity_public_receiver PUBLIC high_fidelity_channel datapump_triad_receiver)
add_executable(high_fidelity_appraisal "${_hf_root}/src/main.cpp" "${_hf_root}/src/vlf_diagnostic.cpp")
target_include_directories(high_fidelity_appraisal PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/tools")
target_link_libraries(high_fidelity_appraisal PRIVATE high_fidelity_public_receiver high_fidelity_gui_profile)
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
add_executable(high_fidelity_fullwave_probe "${_hf_root}/src/hf_fullwave_probe.cpp")
target_include_directories(high_fidelity_fullwave_probe PRIVATE "${PROJECT_SOURCE_DIR}/tools")
target_link_libraries(high_fidelity_fullwave_probe PRIVATE high_fidelity_fullwave)
target_compile_definitions(high_fidelity_fullwave_probe PRIVATE ${_hf_identity} APPRAISAL_SOURCE_SHA="${_hf_source_sha}")
set_target_properties(high_fidelity_fullwave_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
add_executable(high_fidelity_fullwave_kernel_probe "${_hf_root}/src/hf_fullwave_kernel_probe.cpp")
target_include_directories(high_fidelity_fullwave_kernel_probe PRIVATE "${PROJECT_SOURCE_DIR}/tools")
target_link_libraries(high_fidelity_fullwave_kernel_probe PRIVATE high_fidelity_fullwave)
target_compile_definitions(high_fidelity_fullwave_kernel_probe PRIVATE ${_hf_identity} APPRAISAL_SOURCE_SHA="${_hf_source_sha}")
set_target_properties(high_fidelity_fullwave_kernel_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(high_fidelity_channel PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(high_fidelity_public_receiver PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(high_fidelity_appraisal PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(high_fidelity_gui_profile PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(high_fidelity_fullwave PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(high_fidelity_fullwave_probe PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
  foreach(_hf_test channel scenarios hf_scenarios hf_ionosphere hf_design hf_history hf_phase_bank hf_public_statistics hf_phase_refinement hf_replay impairment_observables lunar_spectral)
    add_executable(high_fidelity_test_${_hf_test} "${_hf_root}/tests/test_${_hf_test}.cpp")
    target_include_directories(high_fidelity_test_${_hf_test} PRIVATE "${PROJECT_SOURCE_DIR}/src" "${_hf_root}/src")
    target_link_libraries(high_fidelity_test_${_hf_test} PRIVATE high_fidelity_channel)
    add_test(NAME high_fidelity_${_hf_test} COMMAND high_fidelity_test_${_hf_test})
    set_tests_properties(high_fidelity_${_hf_test} PROPERTIES LABELS "appraisal" TIMEOUT 45)
  endforeach()
  foreach(_hf_extra gui_profile fullwave fullwave_kernel)
    add_executable(high_fidelity_test_hf_${_hf_extra} "${_hf_root}/tests/test_hf_${_hf_extra}.cpp")
    if(_hf_extra STREQUAL "fullwave_kernel")
      target_link_libraries(high_fidelity_test_hf_${_hf_extra} PRIVATE high_fidelity_fullwave)
    else()
      target_link_libraries(high_fidelity_test_hf_${_hf_extra} PRIVATE high_fidelity_${_hf_extra})
    endif()
    add_test(NAME high_fidelity_hf_${_hf_extra} COMMAND high_fidelity_test_hf_${_hf_extra})
    set_tests_properties(high_fidelity_hf_${_hf_extra} PROPERTIES LABELS "appraisal" TIMEOUT 45)
  endforeach()
  add_executable(high_fidelity_test_hf_native_supplement "${_hf_root}/tests/test_hf_native_supplement.cpp")
  target_include_directories(high_fidelity_test_hf_native_supplement PRIVATE "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(high_fidelity_test_hf_native_supplement PRIVATE datapump_triad_receiver)
  add_test(NAME high_fidelity_hf_native_supplement COMMAND high_fidelity_test_hf_native_supplement)
  set_tests_properties(high_fidelity_hf_native_supplement PROPERTIES LABELS "appraisal" TIMEOUT 45)
  foreach(_hf_extra admitted_evidence phase_filter)
    add_executable(high_fidelity_test_hf_${_hf_extra} "${_hf_root}/tests/test_hf_${_hf_extra}.cpp")
    target_include_directories(high_fidelity_test_hf_${_hf_extra} PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/tools")
    if(_hf_extra STREQUAL "admitted_evidence")
      target_link_libraries(high_fidelity_test_hf_${_hf_extra} PRIVATE datapump_triad_receiver)
    else()
      target_link_libraries(high_fidelity_test_hf_${_hf_extra} PRIVATE high_fidelity_public_receiver)
    endif()
    add_test(NAME high_fidelity_hf_${_hf_extra} COMMAND high_fidelity_test_hf_${_hf_extra})
    set_tests_properties(high_fidelity_hf_${_hf_extra} PROPERTIES LABELS "appraisal" TIMEOUT 90)
  endforeach()
  add_executable(high_fidelity_test_hf_public_receiver "${_hf_root}/tests/test_hf_public_receiver.cpp")
  target_include_directories(high_fidelity_test_hf_public_receiver PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/tools")
  target_link_libraries(high_fidelity_test_hf_public_receiver PRIVATE high_fidelity_public_receiver)
  add_test(NAME high_fidelity_hf_public_receiver COMMAND high_fidelity_test_hf_public_receiver)
  set_tests_properties(high_fidelity_hf_public_receiver PROPERTIES LABELS "appraisal" TIMEOUT 45)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(NAME high_fidelity_integration COMMAND "${Python3_EXECUTABLE}"
    "${_hf_root}/tests/test_integration.py" "$<TARGET_FILE:high_fidelity_appraisal>")
  set_tests_properties(high_fidelity_integration PROPERTIES LABELS "appraisal" TIMEOUT 180)
  add_test(NAME high_fidelity_runner COMMAND "${Python3_EXECUTABLE}"
    "${_hf_root}/tests/test_runner.py")
  set_tests_properties(high_fidelity_runner PROPERTIES LABELS "appraisal" TIMEOUT 45)
  add_test(NAME high_fidelity_ensemble COMMAND "${Python3_EXECUTABLE}"
    "${_hf_root}/tests/test_ensemble.py")
  set_tests_properties(high_fidelity_ensemble PROPERTIES LABELS "appraisal" TIMEOUT 45)
  add_test(NAME high_fidelity_coverage_design COMMAND "${Python3_EXECUTABLE}"
    "${_hf_root}/tests/test_coverage_design.py")
  set_tests_properties(high_fidelity_coverage_design PROPERTIES LABELS "appraisal" TIMEOUT 45)
  add_custom_target(high_fidelity_checks DEPENDS high_fidelity_appraisal high_fidelity_fullwave_probe high_fidelity_fullwave_kernel_probe
    high_fidelity_test_channel high_fidelity_test_scenarios
    high_fidelity_test_hf_scenarios high_fidelity_test_hf_ionosphere high_fidelity_test_hf_design high_fidelity_test_hf_history
    high_fidelity_test_hf_gui_profile high_fidelity_test_hf_fullwave high_fidelity_test_hf_fullwave_kernel
    high_fidelity_test_hf_phase_bank high_fidelity_test_hf_public_statistics high_fidelity_test_hf_native_supplement
    high_fidelity_test_hf_phase_refinement
    high_fidelity_test_hf_admitted_evidence high_fidelity_test_hf_phase_filter
    high_fidelity_test_hf_public_receiver high_fidelity_test_hf_replay
    high_fidelity_test_impairment_observables high_fidelity_test_lunar_spectral)
endif()

include("${_hf_root}/competitor/CMakeLists.txt")
