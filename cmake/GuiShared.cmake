# Exactly one shared application is linked by every adapter and by its tests.
# Native GUI profiles contain only their toolkit translation/platform sources.
if(NOT TARGET datapump_gui_application)
  # Enforce the facade even in production builds with BUILD_TESTING=OFF.
  add_custom_target(datapump_gui_boundary
    COMMAND "${CMAKE_COMMAND}" "-DROOT=${CMAKE_CURRENT_SOURCE_DIR}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/gui_adapter_boundary.cmake"
    COMMENT "Checking the shared GUI/native adapter boundary" VERBATIM)
  add_library(datapump_gui_application STATIC
    src/gui/application.cpp src/gui/controller.cpp src/gui/gui_smoke.cpp src/gui/launch_command.cpp
    src/gui/screen_console.cpp src/gui/plot_render.cpp src/gui/state.cpp
    src/gui/fast/controller.cpp src/gui/fast/screen.cpp src/gui/fast/plots.cpp
    src/gui/inspection_model.cpp src/gui/link_planner_model.cpp src/gui/link_planner_page.cpp)
  add_dependencies(datapump_gui_application datapump_gui_boundary)
  target_include_directories(datapump_gui_application PUBLIC src/gui)
  # Static-library link dependencies still reach the executable, but modem
  # headers and other domain compile requirements stop at this implementation.
  target_link_libraries(datapump_gui_application PRIVATE datapump datapump_fast)
  if(DATAPUMP_SANITIZERS AND NOT MSVC)
    # Keep native adapters instrumented without exporting domain include paths.
    target_compile_options(datapump_gui_application INTERFACE
      -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
  endif()
  target_compile_definitions(datapump_gui_application PRIVATE DATAPUMP_VERSION="${PROJECT_VERSION}")
endif()
