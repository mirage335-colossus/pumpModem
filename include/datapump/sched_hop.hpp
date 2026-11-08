#pragma once

#include "datapump/types.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace datapump::modem {

inline unsigned sched_hop_multiplier(std::string_view value) {
    // External 0 and historical disabled/off/1 all mean the same identity map.
    // Retired enabled multipliers are rejected, never rounded to another calendar.
    if (value == "0" || value == "disabled" || value == "off" || value == "1") return 1;
    if (value == "3") return 3;
    if (value == "5") return 5;
    throw Error("Sched_Hop accepts 0, 3 or 5; retired enabled multipliers require an explicit new choice");
}
inline std::string_view sched_hop_id(unsigned multiplier) {
    switch (multiplier) {
    case 1: return "0";
    case 3: return "3";
    case 5: return "5";
    default: throw Error("invalid scheduled-hop time multiplier");
    }
}

// Public controls use 0 for the undiluted schedule; internal group_hops stays
// one. Historical 1 has exactly that meaning and exports canonically as 0.
inline unsigned sched_hop_active_hops(std::string_view value) {
    if (value == "0" || value == "1") return 1;
    if (value == "3") return 3;
    throw Error("Sched_Hop dilution accepts 0 or 3; unsupported legacy dilution requires an explicit new choice");
}
inline std::string_view sched_hop_dilution_id(unsigned active_hops) {
    if (active_hops == 1) return "0";
    if (active_hops == 3) return "3";
    throw Error("unsupported public Sched_Hop dilution");
}

} // namespace datapump::modem
