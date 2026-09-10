# The pinned toolkit is part of the source checkout. Never fetch it at configure time.
foreach(feature BUILD_SHARED_LIBS BUILD_TEST BUILD_EXAMPLES BUILD_FLUID BUILD_FLTK_OPTIONS
                BUILD_FORMS BUILD_GL BUILD_HTML_DOCS BUILD_PDF_DOCS BUILD_FLUID_DOCS
                BACKEND_WAYLAND GRAPHICS_CAIRO USE_PANGO OPTION_CAIRO_WINDOW OPTION_CAIRO_EXT
                OPTION_PRINT_SUPPORT OPTION_SVG)
  set(FLTK_${feature} OFF CACHE BOOL "Data Pump's minimal native GUI configuration" FORCE)
endforeach()
foreach(codec LIBJPEG LIBPNG ZLIB)
  set(FLTK_USE_SYSTEM_${codec} OFF CACHE BOOL "Use pinned FLTK image codecs" FORCE)
endforeach()
if(MSVC)
  # Match the application even when a developer explicitly selects /MD.
  if(NOT CMAKE_MSVC_RUNTIME_LIBRARY OR CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL")
    set(FLTK_MSVC_RUNTIME_DLL ON CACHE BOOL "Match the application's MSVC runtime" FORCE)
  else()
    set(FLTK_MSVC_RUNTIME_DLL OFF CACHE BOOL "Match the application's MSVC runtime" FORCE)
  endif()
endif()
# EXCLUDE_FROM_ALL prevents upstream developer tools, headers, and libraries from
# becoming part of the application's install/package.
add_subdirectory(third_party/fltk EXCLUDE_FROM_ALL)
add_executable(datapump-gui WIN32 src/gui/main.cpp src/gui/state.cpp)
target_include_directories(datapump-gui PRIVATE src/gui)
target_link_libraries(datapump-gui PRIVATE datapump fltk::fltk)
target_compile_definitions(datapump-gui PRIVATE DATAPUMP_VERSION="${PROJECT_VERSION}")
if(MSVC)
  target_compile_options(datapump-gui PRIVATE /W4 /permissive-)
  target_link_options(datapump-gui PRIVATE "/MANIFEST:EMBED" "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/src/pump.manifest")
else()
  target_compile_options(datapump-gui PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()
