#pragma once
#include "../ui_contract.hpp"
namespace datapump::gui::legacy_ui {
const std::vector<ui::Control>& screen();
bool owns(ui::Field);
bool owns(ui::Command);
}
