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
inline bool select(std::size_t dense_mandatory_bytes,std::size_t workspace) noexcept {
    const auto value=mode();
    // Availability fallback only. Retain the existing optimized representation
    // where its complete mandatory dense state can fit; no unmeasured speed crossover.
    return value==Mode::forced || (value==Mode::automatic && dense_mandatory_bytes>=workspace);
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
// Diagnostic overrides retain their preceding meaning: exact=1 requests the
// original-row Direct reference, while ANY other explicit value (notably0)
// keeps proxy-only historical behavior. Only an unset override admits the new
// integral-proxy bridge, currently opt-in pending complete execution evidence.
enum class ColoredDemandPath { unavailable, fractional_proxy, original_direct, integral_proxy };
inline bool integral_proxy_requested() noexcept {
    const auto* value=std::getenv("DATAPUMP_SCHED_COLORED_INTEGRAL_PROXY");
    return value && std::string_view(value)=="1";
}
inline constexpr ColoredDemandPath colored_demand_path(bool exact_override_present,
        bool exact_override_one,bool integral_bridge,bool integral_proof,bool fractional_proof) noexcept {
    if(exact_override_one && integral_proof)return ColoredDemandPath::original_direct;
    if(fractional_proof)return ColoredDemandPath::fractional_proxy;
    if(!exact_override_present && integral_bridge && integral_proof)return ColoredDemandPath::integral_proxy;
    return ColoredDemandPath::unavailable;
}
} // namespace datapump::modem::detail::scheduled_acquisition_policy
