#pragma once
#include "datapump/capture_transport.hpp"
#include <cmath>
#include <cstdint>
#include <optional>

namespace datapump::live::detail {
// Translate an actual raw capture coordinate to the bank's dated logical
// sample clock. This is advisory metadata only, never receiver timing authority.
// Limit extrapolation from a freshly dated capture block to two seconds; an
// absent, stale, transformed, or discontinuous mapping fails closed.
inline std::optional<double> helper_capture_utc(
        audio::CaptureTransportRecipe recipe,std::uint32_t raw_rate,std::uint32_t logical_rate,
        std::uint64_t raw_sample,std::uint64_t raw_first,std::uint64_t raw_end,
        std::uint64_t bank_origin,std::uint64_t observed_sample,
        std::optional<long double> observed_utc,long double seconds_per_frame) noexcept {
    if((recipe.kind!=audio::CaptureTransportKind::fixed_nominal_audio &&
        recipe.kind!=audio::CaptureTransportKind::sampled_physical_halfband) ||
       !raw_rate || !logical_rate || recipe.raw_rate!=raw_rate || recipe.logical_rate!=logical_rate ||
       raw_first>raw_end || raw_sample<raw_first || raw_sample>raw_end ||
       !observed_utc || !std::isfinite(*observed_utc) ||
       !std::isfinite(seconds_per_frame) || seconds_per_frame<=0)return {};
    if(recipe.kind==audio::CaptureTransportKind::sampled_physical_halfband) {
        if(raw_rate%logical_rate)return {};
        const auto factor=raw_rate/logical_rate;
        if(factor!=4 && factor!=8 && factor!=16 && factor!=32)return {};
    }
    const long double logical=static_cast<long double>(recipe.logical_zero)+
        (static_cast<long double>(raw_sample)-static_cast<long double>(recipe.raw_zero))*logical_rate/raw_rate-
        static_cast<long double>(bank_origin);
    if(!std::isfinite(logical) || logical<0)return {};
    const auto age=(logical-static_cast<long double>(observed_sample))*seconds_per_frame;
    if(!std::isfinite(age) || std::abs(age)>2)return {};
    const auto utc=*observed_utc+age;
    if(!std::isfinite(utc) || utc<0 || utc>=4294967296.L)return {};
    return static_cast<double>(utc);
}
}
