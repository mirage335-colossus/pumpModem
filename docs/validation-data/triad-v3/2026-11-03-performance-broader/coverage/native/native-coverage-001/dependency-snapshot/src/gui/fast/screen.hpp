#pragma once
#include "../ui_contract.hpp"
namespace datapump::gui::fast_ui {
const std::vector<ui::Control>& screen();
bool owns(ui::Field field);
bool owns(ui::Command command);
}
