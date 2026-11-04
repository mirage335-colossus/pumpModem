#pragma once
#include "datapump/pattern_receiver.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace datapump::live::detail {
// Presentation support for one continuous, uniformly clocked source. The caller
// excludes Symbol_Burst and scheduled/deferred-symbol calendars. No received
// payload length, bit value, source interpretation or physical-END decision is
// used. Unknown slots count in the source span but add no raw support.
//
// A drain can contain several chunks of one physical stream. Their support is
// cumulative through the LAST confirmed symbol, even on earlier chunks. Use
// the same complete drain envelope for every chunk, rather than letting an
// earlier short chunk permanently cache an unnormalized raw maximum.
inline std::optional<double> nominal_reception_support(
        const modem::PatternBurst& selected,std::span<const modem::PatternBurst> drained,
        std::uint64_t nominal_symbol_samples) noexcept {
    if(!nominal_symbol_samples)return std::nullopt;
    const auto first=selected.stream_first_sample,source_first=selected.stream_first_symbol;
    std::uint64_t end=first,next=source_first;
    double support=0;
    bool found=false;
    for(const auto& burst:drained) {
        if(burst.stream_first_sample!=first || burst.stream_first_symbol!=source_first)continue;
        found=true;
        if(burst.first_stream_symbol<source_first || burst.end_sample<first ||
           !std::isfinite(burst.support_samples) || burst.support_samples<0 ||
           (!burst.bits.empty() && burst.missing_slots))return std::nullopt;
        if(burst.bits.size()>UINT64_MAX-burst.first_stream_symbol)return std::nullopt;
        auto after=burst.first_stream_symbol+static_cast<std::uint64_t>(burst.bits.size());
        if(burst.missing_slots>UINT64_MAX-after)return std::nullopt;
        after+=burst.missing_slots;
        end=std::max(end,burst.end_sample);next=std::max(next,after);
        support=std::max(support,burst.support_samples);
    }
    if(!found || end<=first || next<=source_first)return std::nullopt;
    const auto symbols=next-source_first,physical_span=end-first;
    if(symbols>UINT64_MAX/nominal_symbol_samples)return std::nullopt;
    const auto nominal_span=symbols*nominal_symbol_samples;
    const auto physical=static_cast<long double>(physical_span);
    if(static_cast<long double>(support)>physical)return std::nullopt;
    // Divide first: the fraction is in [0,1], avoids intermediate overflow and
    // maps fully supported windows of T +/- a bin exactly onto nominal n*T.
    const auto value=(static_cast<long double>(support)/physical)*nominal_span;
    const auto result=static_cast<double>(value);
    if(!std::isfinite(value) || !std::isfinite(result) || result<0)return std::nullopt;
    return result;
}
inline double reception_arbitration_support(const modem::PatternBurst& selected,
        std::span<const modem::PatternBurst> drained,std::uint64_t nominal_symbol_samples,
        bool normalize=false) noexcept {
    if(normalize)if(const auto value=nominal_reception_support(selected,drained,nominal_symbol_samples))
        return *value;
    return selected.support_samples;
}
}
