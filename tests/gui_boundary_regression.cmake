cmake_minimum_required(VERSION 3.21)
# Prove the architecture guard follows helper dependencies and discovers new
# adapter files, rather than only rejecting names in the current entry points.
file(REMOVE_RECURSE "${BINARY_DIR}/gui-boundary-fixture")
set(fixture "${BINARY_DIR}/gui-boundary-fixture")
file(MAKE_DIRECTORY "${fixture}/src" "${fixture}/cmake")
file(COPY "${ROOT}/src/gui" DESTINATION "${fixture}/src")
file(COPY "${ROOT}/cmake/GuiFltk.cmake" "${ROOT}/cmake/GuiRev.cmake" DESTINATION "${fixture}/cmake")

function(check_guard should_pass expected)
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DROOT=${fixture}" -P "${ROOT}/tests/gui_adapter_boundary.cmake"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(should_pass AND NOT result EQUAL 0)
    message(FATAL_ERROR "Clean GUI boundary fixture failed: ${output}${error}")
  elseif(NOT should_pass AND (result EQUAL 0 OR NOT "${output}${error}" MATCHES "${expected}"))
    message(FATAL_ERROR "GUI boundary escape was not detected: ${output}${error}")
  endif()
endfunction()

check_guard(TRUE "")
file(READ "${fixture}/src/gui/theme.hpp" clean_theme)
file(APPEND "${fixture}/src/gui/theme.hpp" "\n#include <controller.hpp>\n")
check_guard(FALSE "controller.hpp.*outside the public")
file(WRITE "${fixture}/src/gui/theme.hpp" "${clean_theme}")
file(WRITE "${fixture}/src/gui/backend_extra.cpp" "auto command = ui::Command::transmit;\n")
check_guard(FALSE "application binding Command::transmit")
file(REMOVE "${fixture}/src/gui/backend_extra.cpp")

# Allowed public helpers are generic vocabulary, not an escape hatch for native
# code to hide feature decisions behind an otherwise permitted include.
file(APPEND "${fixture}/src/gui/theme.hpp" "\ninline auto hidden_command() { return ui::Command::transmit; }\n")
check_guard(FALSE "theme.hpp contains application binding Command::transmit")
file(WRITE "${fixture}/src/gui/theme.hpp" "${clean_theme}")
file(READ "${fixture}/src/gui/control_layout.hpp" clean_layout)
file(APPEND "${fixture}/src/gui/control_layout.hpp" "\ninline auto hidden_field() { return ui::Field::message; }\n")
check_guard(FALSE "control_layout.hpp contains application binding Field::message")
file(WRITE "${fixture}/src/gui/control_layout.hpp" "${clean_layout}")

# Formatting and local type aliases must preserve the same boundary. None/count
# remain legal sentinels, including when the public types are abbreviated.
file(WRITE "${fixture}/src/gui/backend_extra.cpp" "using Action = ui::Command; using Another = Action; auto command = Another :: none;\n")
check_guard(TRUE "")
file(APPEND "${fixture}/src/gui/backend_extra.cpp" "auto hidden = Another :: transmit;\n")
check_guard(FALSE "application binding Another::transmit")
file(WRITE "${fixture}/src/gui/backend_extra.cpp" "typedef ui::Field Binding; auto hidden = Binding :: message;\n")
check_guard(FALSE "application binding Binding::message")
file(WRITE "${fixture}/src/gui/backend_extra.cpp" "auto hidden = ui::Command \n :: \n transmit;\n")
check_guard(FALSE "application binding Command::transmit")
file(WRITE "${fixture}/src/gui/backend_extra.cpp" "auto hidden = ui::Menu::keyfile;\n")
check_guard(FALSE "application binding Menu::keyfile")
file(WRITE "${fixture}/src/gui/backend_extra.cpp" "using enum ui::Command; auto hidden = transmit;\n")
check_guard(FALSE "imports application bindings with using enum")
file(REMOVE "${fixture}/src/gui/backend_extra.cpp")

file(APPEND "${fixture}/src/gui/application.hpp" "\n#include <FL/Fl.H>\n")
check_guard(FALSE "depends on a native toolkit")
file(COPY "${ROOT}/src/gui/application.hpp" DESTINATION "${fixture}/src/gui")
file(APPEND "${fixture}/src/gui/screen_console.cpp" "\n#include <FL/Fl.H>\n")
check_guard(FALSE "depends on a native adapter/toolkit")
file(REMOVE_RECURSE "${fixture}")
