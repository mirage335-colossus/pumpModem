# Configure-time provenance is a separate file, not a changing definition on
# every compilation unit. Reconfiguration updates it without recompiling DSP.
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
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/build-info.txt" CONTENT "${_build_info}" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/build-info.txt" DESTINATION share/doc/datapump)
message(STATUS "DataPump build: ${CMAKE_CURRENT_BINARY_DIR} (${CMAKE_BUILD_TYPE}, ${DATAPUMP_GUI_BACKEND}, portable=${DATAPUMP_PORTABLE})")
