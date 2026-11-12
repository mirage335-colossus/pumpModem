#pragma once

#include <algorithm>

namespace datapump::fast::detail {

inline constexpr double ssb_legacy_calibration_guard = .015;

// Unit-energy square QAM has nearest-neighbor distance squared 6/(M-1).
// Keep the legacy guard unless that distance calls for a smaller allowance.
constexpr double ssb_demapper_variance_guard(unsigned constellation) noexcept {
    return std::min(ssb_legacy_calibration_guard, 6. / (16. * (constellation - 1)));
}

} // namespace datapump::fast::detail
