# Select before configuring any toolkit: a build contains exactly one adapter
# and its dependencies. Advertise only implemented adapters, never placeholders.
include("${CMAKE_CURRENT_LIST_DIR}/GuiShared.cmake")
if(DATAPUMP_GUI_BACKEND STREQUAL "fltk")
  include("${CMAKE_CURRENT_LIST_DIR}/GuiFltk.cmake")
elseif(DATAPUMP_GUI_BACKEND STREQUAL "rev")
  include("${CMAKE_CURRENT_LIST_DIR}/GuiRev.cmake")
else()
  message(FATAL_ERROR
    "DATAPUMP_GUI_BACKEND must name exactly one implemented backend. "
    "Received '${DATAPUMP_GUI_BACKEND}'. Available backends: fltk, rev. "
    "Use -DDATAPUMP_BUILD_GUI=OFF for a CLI-only build.")
endif()
message(STATUS "DataPump GUI backend: ${DATAPUMP_GUI_BACKEND}")

target_include_directories(datapump-gui PRIVATE src/gui)
target_link_libraries(datapump-gui PRIVATE datapump_gui_application)
target_compile_definitions(datapump-gui PRIVATE DATAPUMP_VERSION="${PROJECT_VERSION}"
  DATAPUMP_GUI_BACKEND="${DATAPUMP_GUI_BACKEND}")
if(MSVC)
  target_compile_options(datapump-gui PRIVATE /W4 /permissive-)
  target_link_options(datapump-gui PRIVATE "/MANIFEST:EMBED" "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/src/pump.manifest")
else()
  target_compile_options(datapump-gui PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()
