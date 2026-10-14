#pragma once
#include <cstdlib>
#include <cstddef>
#include <string_view>
namespace datapump::modem::detail::scheduled_acquisition_policy {
// One policy across planning, immutable layout and runtime admission. Automatic
// permits the supported bounded representation; geometry/owner/precision checks
// still decide eligibility. This is not a calibrated realtime or probability
// promise. Explicit zero retains the preceding dense diagnostic implementation.
enum class Mode { disabled, automatic, forced };
inline Mode mode() noexcept {
    const auto* value=std::getenv("DATAPUMP_SCHED_LAZY_ACQUISITION");
    if(!value || std::string_view(value)=="auto")return Mode::automatic;
    return std::string_view(value)=="1"?Mode::forced:Mode::disabled;
}
inline bool select(std::size_t dense_fixed_lower_bound,std::size_t workspace) noexcept {
    const auto value=mode();
    // Availability fallback only. Retain the existing optimized representation
    // where its necessary dense state can fit; no unmeasured speed crossover.
    return value==Mode::forced || (value==Mode::automatic && dense_fixed_lower_bound>=workspace);
}
inline bool requested() noexcept {
    const auto* mode=std::getenv("DATAPUMP_SCHED_LAZY_ACQUISITION");
    return !mode || std::string_view(mode)=="auto" || std::string_view(mode)=="1";
}
inline bool colored_requested() noexcept {
    const auto* mode=std::getenv("DATAPUMP_SCHED_COLD_COLORED");
    return !mode || std::string_view(mode)=="1";
}
inline bool demand_requested() noexcept {
    const auto* mode=std::getenv("DATAPUMP_SCHED_COLD_COLORED_ON_DEMAND");
    return !mode || std::string_view(mode)=="1";
}
} // namespace datapump::modem::detail::scheduled_acquisition_policy
