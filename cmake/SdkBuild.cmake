# Configure after project()/BuildDependencies and before dependency discovery.
# A development executable must also work with the SDK's newer C++ compiler on
# Bookworm; the host's libstdc++ and development libraries need not be installed.
include_guard(GLOBAL)
if(NOT DATAPUMP_SDK_ROOT)
  return()
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT IS_DIRECTORY "${CMAKE_SYSROOT}")
  message(FATAL_ERROR "SDK builds require a Linux target and a prepared CMAKE_SYSROOT")
endif()
set(OPENSSL_USE_STATIC_LIBS ON CACHE BOOL "Use SDK OpenSSL without host ABI dependencies" FORCE)
set(OPENSSL_USE_STATIC_LIBS ON)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  add_link_options(-static-libstdc++ -static-libgcc)
endif()

# These are compiler-default target directories, even if an older CMake failed
# to recognize the sysroot prefix in GCC's implicit-search diagnostic. Listing
# them prevents automatic build RPATHs into a directory containing target libc.
foreach(language C CXX)
  foreach(directory lib lib64 usr/lib usr/lib64)
    list(APPEND CMAKE_${language}_IMPLICIT_LINK_DIRECTORIES "${CMAKE_SYSROOT}/${directory}")
  endforeach()
  list(REMOVE_DUPLICATES CMAKE_${language}_IMPLICIT_LINK_DIRECTORIES)
endforeach()

if(DATAPUMP_SANITIZERS)
  file(REAL_PATH "${CMAKE_SYSROOT}" _datapump_sanitizer_root)
  foreach(runtime libasan.so libubsan.so)
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" "-print-file-name=${runtime}"
      RESULT_VARIABLE status OUTPUT_VARIABLE path OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    file(REAL_PATH "${path}" resolved)
    cmake_path(IS_PREFIX _datapump_sanitizer_root "${resolved}" NORMALIZE inside)
    if(NOT status STREQUAL "0" OR NOT inside OR NOT EXISTS "${resolved}")
      message(FATAL_ERROR "This SDK does not provide ${runtime} in its target sysroot for isolated sanitizer runs. Use ./build.sh sanitize without --sdk, or prepare an SDK with target sanitizer runtimes.")
    endif()
  endforeach()
endif()

function(datapump_setup_sdk_build_runtime)
  if(NOT EXISTS "${CMAKE_OBJDUMP}")
    message(FATAL_ERROR "SDK builds need the compiler toolchain's objdump for runtime staging")
  endif()
  get_property(targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
  foreach(target IN LISTS targets)
    get_target_property(type "${target}" TYPE)
    if(NOT type STREQUAL "EXECUTABLE")
      continue()
    endif()
    # Project shared libraries (notably the ALSA contract stub) take priority
    # over an SDK library with the same SONAME. Follow project target links,
    # never ambient host library directories, to enumerate this explicit set.
    set(queue "${target}")
    set(visited)
    set(project_libraries)
    set(project_sonames)
    set(project_directories)
    while(queue)
      list(POP_FRONT queue linked)
      if(NOT TARGET "${linked}" OR linked IN_LIST visited)
        continue()
      endif()
      list(APPEND visited "${linked}")
      get_target_property(imported "${linked}" IMPORTED)
      if(imported)
        continue()
      endif()
      get_target_property(linked_type "${linked}" TYPE)
      if(linked_type STREQUAL "SHARED_LIBRARY")
        list(APPEND project_libraries "$<TARGET_FILE:${linked}>")
        list(APPEND project_sonames "$<TARGET_SONAME_FILE_NAME:${linked}>")
        list(APPEND project_directories "$<TARGET_FILE_DIR:${linked}>")
      endif()
      foreach(property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(children "${linked}" "${property}")
        foreach(child IN LISTS children)
          # CMake represents private static-library dependencies this way.
          if(child MATCHES "^\\$<LINK_ONLY:([^>]+)>$")
            set(child "${CMAKE_MATCH_1}")
          endif()
          if(TARGET "${child}")
            list(APPEND queue "${child}")
          endif()
        endforeach()
      endforeach()
    endwhile()
    list(REMOVE_DUPLICATES project_directories)
    # RPATH is inherited by dependencies; RUNPATH would not resolve the second
    # level of a copied X11/font/audio dependency chain.
    set_property(TARGET "${target}" APPEND PROPERTY BUILD_RPATH ${project_directories} "$ORIGIN/sdk-runtime")
    target_link_options("${target}" PRIVATE "LINKER:--disable-new-dtags")
    add_custom_command(TARGET "${target}" POST_BUILD
      COMMAND "${CMAKE_COMMAND}"
        "-DSDK_SYSROOT=${CMAKE_SYSROOT}" "-DOBJDUMP=${CMAKE_OBJDUMP}"
        "-DEXECUTABLE=$<TARGET_FILE:${target}>"
        "-DBUILD_ROOT=${CMAKE_BINARY_DIR}"
        "-DPROJECT_LIBRARIES=${project_libraries}" "-DPROJECT_SONAMES=${project_sonames}"
        "-DDESTINATION=$<TARGET_FILE_DIR:${target}>/sdk-runtime"
        -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/stage-sdk-runtime.cmake"
      COMMENT "Staging SDK runtime libraries for ${target}" VERBATIM)
  endforeach()
endfunction()

# The application's executables and tests are defined in the root directory.
# Vendor tools in subdirectories remain outside the application build.
cmake_language(DEFER CALL datapump_setup_sdk_build_runtime)
