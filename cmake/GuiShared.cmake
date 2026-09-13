# Exactly one shared application is linked by every adapter and by its tests.
# Native GUI profiles contain only their toolkit translation/platform sources.
if(NOT TARGET datapump_gui_application)
  add_library(datapump_gui_application STATIC
    src/gui/application.cpp src/gui/controller.cpp src/gui/gui_smoke.cpp
    src/gui/screen_console.cpp src/gui/plot_render.cpp src/gui/state.cpp
    src/gui/inspection_model.cpp)
  target_include_directories(datapump_gui_application PUBLIC src/gui)
  target_link_libraries(datapump_gui_application PUBLIC datapump)
  target_compile_definitions(datapump_gui_application PRIVATE DATAPUMP_VERSION="${PROJECT_VERSION}")
endif()
