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
add_executable(datapump-gui WIN32 src/gui/backend_fltk.cpp)
target_link_libraries(datapump-gui PRIVATE fltk::fltk)
if(BUILD_TESTING)
  add_executable(test_fltk_adapter tests/test_fltk_adapter.cpp)
  target_include_directories(test_fltk_adapter PRIVATE src/gui)
  target_link_libraries(test_fltk_adapter PRIVATE datapump_gui_application fltk::fltk)
  add_executable(test_fltk_document tests/test_fltk_document.cpp)
  target_include_directories(test_fltk_document PRIVATE src/gui)
  target_link_libraries(test_fltk_document PRIVATE datapump_gui_application fltk::fltk)
  # These native widget/focus probes require a display and run explicitly under
  # Xvfb on Linux; ordinary CTest remains usable without a GUI session.
endif()
install(FILES third_party/fltk/COPYING DESTINATION share/doc/datapump RENAME FLTK-LICENSE)
