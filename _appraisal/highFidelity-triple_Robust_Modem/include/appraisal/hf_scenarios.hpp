#pragma once

#include "appraisal/channel.hpp"

#include <optional>
#include <string>

namespace appraisal {

// Additional source-defined HF comparators that are kept separate from the
// primary scenario catalogue. An empty optional means this provider does not
// own the requested ID.
std::optional<Scenario> make_additional_hf_scenario(const std::string& id);

}
