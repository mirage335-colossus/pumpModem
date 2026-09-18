#pragma once
#include "link_planner_model.hpp"
#include "ui_document.hpp"
#include <string>

namespace datapump::gui::planner_page {
// Native text and controls share one immutable document in both GUI adapters.
ui::DocumentNode build(const planner::Model& model, float width, bool details,
                       bool use_draft, std::string error = {});
}
