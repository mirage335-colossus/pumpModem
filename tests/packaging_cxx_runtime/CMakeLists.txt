cmake_minimum_required(VERSION 3.21)
project(PortableCxxRuntimeFixture VERSION 1.0 LANGUAGES CXX)
option(DATAPUMP_PORTABLE "Apply portable policy" ON)
option(DATAPUMP_SANITIZERS "Keep sanitizer runtime policy" OFF)
option(APPLY_RUNTIME_POLICY "Apply the production runtime link policy" ON)
set(CMAKE_CXX_STANDARD 17)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/StaticCxxRuntime.cmake")
add_executable(runtime_probe main.cpp)
target_link_libraries(runtime_probe PRIVATE ${CMAKE_DL_LIBS})
# Deliberately export application symbols: the private runtime must remain
# hidden even when an executable needs to export its own plugin API.
target_link_options(runtime_probe PRIVATE "LINKER:--export-dynamic")
if(APPLY_RUNTIME_POLICY)
  datapump_link_static_cxx_runtime(runtime_probe PRIVATE)
endif()
add_library(host_plugin MODULE plugin.cpp)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/policy.txt"
  CONTENT "enabled=${DATAPUMP_STATIC_GNU_RUNTIME}\noptions=$<TARGET_PROPERTY:runtime_probe,LINK_OPTIONS>\n")
# No audio is exercised by this fixture; do not add a host ALSA dlopen root.
set(DATAPUMP_ALSA_LIBRARY "${CMAKE_CURRENT_BINARY_DIR}/unused-alsa")
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/Portable.cmake")
datapump_install_native(TARGETS runtime_probe)
