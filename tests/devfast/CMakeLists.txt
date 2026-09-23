cmake_minimum_required(VERSION 3.21)
project(DataPumpFocusedDiagnostics LANGUAGES CXX)

# Compile the production Legacy controller/session and the existing real-time
# fixture without configuring unrelated crypto, compression or native widgets.
# This is a diagnostic build, never an application package or certification.
get_filename_component(SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
find_package(Threads REQUIRED)
enable_testing()
add_library(legacy_diagnostic_common STATIC
  "${SOURCE_ROOT}/src/gui/legacy/controller.cpp"
  "${SOURCE_ROOT}/src/gui/legacy/plots.cpp"
  "${SOURCE_ROOT}/src/legacy/modem.cpp"
  "${SOURCE_ROOT}/src/legacy/psk.cpp"
  "${SOURCE_ROOT}/src/legacy/olivia.cpp")
add_executable(test_gui_legacy_live
  "${SOURCE_ROOT}/tests/test_gui_legacy_live.cpp"
  "${SOURCE_ROOT}/src/legacy/session.cpp")
add_executable(test_gui_legacy_poll
  "${SOURCE_ROOT}/tests/test_gui_legacy_poll.cpp")
foreach(target IN ITEMS legacy_diagnostic_common test_gui_legacy_live test_gui_legacy_poll)
  set_target_properties(${target} PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_include_directories(${target} PRIVATE
    "${SOURCE_ROOT}/include" "${SOURCE_ROOT}/src/gui")
  target_link_libraries(${target} PRIVATE Threads::Threads)
  if(MSVC)
    target_compile_options(${target} PRIVATE /utf-8 /permissive-)
  else()
    target_compile_options(${target} PRIVATE -ffp-contract=off)
  endif()
endforeach()
target_link_libraries(test_gui_legacy_live PRIVATE legacy_diagnostic_common)
target_link_libraries(test_gui_legacy_poll PRIVATE legacy_diagnostic_common)
add_test(NAME gui_legacy_poll COMMAND test_gui_legacy_poll)
add_test(NAME gui_legacy_live COMMAND test_gui_legacy_live)
