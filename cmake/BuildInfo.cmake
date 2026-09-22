# Configure-time provenance is a separate file, not a changing definition on
# every compilation unit. Reconfiguration updates it without recompiling DSP.
function(datapump_check_speculation_capabilities)
  include(CheckCSourceCompiles)
  include(CMakePushCheckState)
  cmake_push_check_state(RESET)
  # Compile only: this also works when the target cannot execute on the host.
  set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
  if(CMAKE_BUILD_TYPE)
    set(CMAKE_TRY_COMPILE_CONFIGURATION "${CMAKE_BUILD_TYPE}")
  endif()
  set(CMAKE_REQUIRED_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/include")
  set(CMAKE_REQUIRED_QUIET TRUE)
  file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/include/datapump/speculation.h" _header_hash)
  set(_probe_key "${_header_hash};${CMAKE_C_COMPILER};${CMAKE_C_COMPILER_VERSION};${CMAKE_C_COMPILER_TARGET};${CMAKE_SYSROOT};${CMAKE_C_FLAGS};${CMAKE_C_FLAGS_DEBUG};${CMAKE_C_FLAGS_RELEASE};${CMAKE_C_FLAGS_RELWITHDEBINFO};${CMAKE_C_FLAGS_MINSIZEREL};${CMAKE_BUILD_TYPE}")
  if(NOT "${DATAPUMP_SPECULATION_PROBE_KEY}" STREQUAL "${_probe_key}")
    unset(DATAPUMP_HAVE_INDEX_NOSPEC CACHE)
    unset(DATAPUMP_HAVE_SPECULATION_BARRIER CACHE)
    set(DATAPUMP_SPECULATION_PROBE_KEY "${_probe_key}" CACHE INTERNAL "Receive mitigation compiler probe inputs" FORCE)
  endif()
  check_c_source_compiles("#include <datapump/speculation.h>
#if !DATAPUMP_INDEX_NOSPEC_SUPPORTED
#error No supported index mitigation for this compiler target
#endif
size_t probe(size_t index,size_t extent) { return datapump_index_nospec(index,extent); }
" DATAPUMP_HAVE_INDEX_NOSPEC)
  check_c_source_compiles("#include <datapump/speculation.h>
#if !DATAPUMP_SPECULATION_BARRIER_SUPPORTED
#error No supported validation barrier for this compiler target
#endif
void probe(void) { datapump_speculation_barrier(); }
" DATAPUMP_HAVE_SPECULATION_BARRIER)
  cmake_pop_check_state()
endfunction()
datapump_check_speculation_capabilities()
set(_index_mitigation "unsupported - architectural bounds only")
set(_validation_barrier "unsupported - no CPU speculation barrier")
if(DATAPUMP_HAVE_INDEX_NOSPEC)
  set(_index_mitigation "compiler target supported")
endif()
if(DATAPUMP_HAVE_SPECULATION_BARRIER)
  set(_validation_barrier "compiler target supported")
endif()
set(_helper_target "${CMAKE_C_COMPILER_TARGET}")
if(NOT _helper_target)
  set(_helper_target "compiler default")
endif()
find_package(Git QUIET)
set(_revision "source archive (Git unavailable)")
if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
  execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE _revision OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=normal
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE _changes ERROR_QUIET)
  if(_changes)
    string(APPEND _revision " (working tree modified)")
  endif()
endif()
set(_build_info "DataPump ${PROJECT_VERSION}\nRevision at configuration: ${_revision}\nSystem: ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}\nCompiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\nGenerator: ${CMAKE_GENERATOR}\nBuild type: ${CMAKE_BUILD_TYPE}\nGUI: ${DATAPUMP_BUILD_GUI} (${DATAPUMP_GUI_BACKEND})\nPortable: ${DATAPUMP_PORTABLE}\nSanitizers: ${DATAPUMP_SANITIZERS}\nTests configured: ${BUILD_TESTING}\nDependency prefix: ${DATAPUMP_DEPENDENCY_PREFIX}\nC++ launcher: ${CMAKE_CXX_COMPILER_LAUNCHER}\n")
string(APPEND _build_info "Receive helper C compiler: ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION} (target: ${_helper_target})\nTargeted receive index mitigation: ${_index_mitigation}\nTargeted receive validation barrier: ${_validation_barrier}\nMitigation coverage: selected receive accesses and validation boundaries only; no whole-program, CPU, firmware or OS mitigation guarantee\n")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/build-info.txt" CONTENT "${_build_info}" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/build-info.txt" DESTINATION share/doc/datapump)
message(STATUS "DataPump build: ${CMAKE_CURRENT_BINARY_DIR} (${CMAKE_BUILD_TYPE}, ${DATAPUMP_GUI_BACKEND}, portable=${DATAPUMP_PORTABLE})")
message(STATUS "DataPump targeted receive mitigation: index=${_index_mitigation}; barrier=${_validation_barrier}")
