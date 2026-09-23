cmake_minimum_required(VERSION 3.28)
project(DataPumpRevStyleProbe LANGUAGES CXX)

# Compile actual production modules with no display or graphics/crypto packages.
# This narrow compiler regression does not replace the application GUI tests.
get_filename_component(SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(REV_ROOT "${SOURCE_ROOT}/third_party/rev")
enable_testing()
add_library(rev_style_probe STATIC)
target_sources(rev_style_probe PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES
  BASE_DIRS "${REV_ROOT}/source" FILES
  "${REV_ROOT}/source/Core/DirtyFlag.ixx"
  "${REV_ROOT}/source/Core/Color.ixx"
  "${REV_ROOT}/source/Core/Resource.ixx"
  "${REV_ROOT}/source/Element/Style/Style.ixx")
target_include_directories(rev_style_probe PUBLIC "${REV_ROOT}/external")
target_compile_definitions(rev_style_probe PRIVATE PROJECT_ROOT="${REV_ROOT}")
add_executable(test_rev_style "${SOURCE_ROOT}/tests/test_rev_style.cpp")
target_link_libraries(test_rev_style PRIVATE rev_style_probe)
foreach(target IN ITEMS rev_style_probe test_rev_style)
  set_target_properties(${target} PROPERTIES
    CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO
    CXX_SCAN_FOR_MODULES ON
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  if(MSVC)
    target_compile_options(${target} PRIVATE /utf-8 /permissive-)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Clang accepts this invalid C++20 overload set as an extension by default;
    # MSVC correctly rejects it. Keep the same regression strict on both.
    target_compile_options(${target} PRIVATE -Werror=ambiguous-reversed-operator)
  endif()
endforeach()
add_test(NAME rev_style COMMAND test_rev_style)
