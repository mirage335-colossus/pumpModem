cmake_minimum_required(VERSION 3.21)
project(NativePackagingFixture VERSION 1.0 LANGUAGES CXX)
set(DATAPUMP_PORTABLE ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
add_library(helper SHARED helper.cpp)
add_library(middle SHARED middle.cpp)
target_link_libraries(middle PRIVATE helper)
add_executable(pump main.cpp)
target_link_libraries(pump PRIVATE middle)
add_executable(datapump-gui main.cpp)
target_link_libraries(datapump-gui PRIVATE middle)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_library(audio_fixture SHARED audio.cpp)
  set_target_properties(audio_fixture PROPERTIES OUTPUT_NAME asound VERSION 2.0.0 SOVERSION 2)
  # Debian's development link can point straight at the full version, without
  # passing through the SONAME needed by dlopen("libasound.so.2").
  add_custom_command(TARGET audio_fixture POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E rm -f "$<TARGET_LINKER_FILE:audio_fixture>"
    COMMAND "${CMAKE_COMMAND}" -E create_symlink "$<TARGET_FILE_NAME:audio_fixture>"
      "$<TARGET_LINKER_FILE:audio_fixture>")
  set(DATAPUMP_ALSA_LIBRARY "$<TARGET_LINKER_FILE:audio_fixture>")
  foreach(executable pump datapump-gui)
    add_dependencies(${executable} audio_fixture)
    target_link_libraries(${executable} PRIVATE ${CMAKE_DL_LIBS})
  endforeach()
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/Portable.cmake")
datapump_install_native(TARGETS pump datapump-gui)
