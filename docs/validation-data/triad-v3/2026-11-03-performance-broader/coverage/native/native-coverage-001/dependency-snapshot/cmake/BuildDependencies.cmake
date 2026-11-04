include_guard(GLOBAL)

# This optional native SDK contains development headers/linker aliases only.
# A system package installation remains the normal dependency provider.
set(_datapump_local_prefix "${CMAKE_CURRENT_SOURCE_DIR}/third_party/build-support/cache/sysroot/usr")
set(DATAPUMP_DEPENDENCY_PREFIX "" CACHE PATH "Optional existing native dependency prefix (not a cross-compilation sysroot)")
if(DATAPUMP_SDK_ROOT AND DATAPUMP_DEPENDENCY_PREFIX)
  message(FATAL_ERROR "A source SDK cannot be combined with a native dependency prefix. Use a fresh --build-dir and remove DATAPUMP_DEPENDENCY_PREFIX.")
endif()
if(NOT DATAPUMP_DEPENDENCY_PREFIX AND NOT DATAPUMP_SDK_ROOT AND NOT CMAKE_CROSSCOMPILING
    AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$"
    AND EXISTS "${_datapump_local_prefix}/../prepared.json")
  set(DATAPUMP_DEPENDENCY_PREFIX "${_datapump_local_prefix}" CACHE PATH "Optional existing native dependency prefix" FORCE)
endif()
if(DATAPUMP_DEPENDENCY_PREFIX)
  if(NOT IS_DIRECTORY "${DATAPUMP_DEPENDENCY_PREFIX}/include")
    message(FATAL_ERROR "Dependency prefix is missing: ${DATAPUMP_DEPENDENCY_PREFIX}. See third_party/build-support/README.md; dependency preparation is separate from building.")
  endif()
  list(PREPEND CMAKE_PREFIX_PATH "${DATAPUMP_DEPENDENCY_PREFIX}")
  message(STATUS "DataPump native dependency prefix: ${DATAPUMP_DEPENDENCY_PREFIX}")
endif()
if(DEFINED DATAPUMP_CONFIGURED_DEPENDENCY_PREFIX AND
    NOT DATAPUMP_CONFIGURED_DEPENDENCY_PREFIX STREQUAL DATAPUMP_DEPENDENCY_PREFIX)
  message(FATAL_ERROR "Dependency prefix changed in a configured tree. Use a fresh --build-dir so cached package include/library paths cannot mix SDKs.")
endif()
set(DATAPUMP_CONFIGURED_DEPENDENCY_PREFIX "${DATAPUMP_DEPENDENCY_PREFIX}" CACHE INTERNAL "Native dependency prefix used by this build" FORCE)

option(DATAPUMP_COMPILER_CACHE "Use ccache when available, without requiring it" ON)
if(DATAPUMP_COMPILER_CACHE AND CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
  find_program(DATAPUMP_CCACHE_PROGRAM ccache)
  if(DATAPUMP_CCACHE_PROGRAM)
    foreach(language C CXX)
      # Respect explicitly configured launchers, including other cache tools.
      if(NOT CMAKE_${language}_COMPILER_LAUNCHER)
        set(CMAKE_${language}_COMPILER_LAUNCHER "${DATAPUMP_CCACHE_PROGRAM}")
      endif()
    endforeach()
    message(STATUS "DataPump compiler cache: ${DATAPUMP_CCACHE_PROGRAM}")
  else()
    message(STATUS "DataPump compiler cache: ccache unavailable (optional)")
  endif()
endif()
