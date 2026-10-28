#pragma once
#include "datapump/modem.hpp"
#include "datapump/symbol_schedule.hpp"
#include <algorithm>
#include <cmath>

namespace datapump::live::detail {
// Round the sum upward under the repository's ordinary IEEE/nearest DSP
// arithmetic. TwoSum retains a positive rounding remainder even when the cell
// is much smaller than the measured value; no loop or policy inflation.
inline double helper_constructor_outward_add(double measured,double cell) noexcept {
    if(!(std::isfinite(measured)&&measured>0&&std::isfinite(cell)&&cell>0))return measured;
    const auto sum=measured+cell;
    if(!std::isfinite(sum))return sum;
    const auto cell_part=sum-measured;
    const auto remainder=(measured-(sum-cell_part))+(cell-cell_part);
    return remainder>0?std::nextafter(sum,INFINITY):sum;
}
// Fund one ADDITIONAL ordinary RF cell radius in the ACTUAL constructor, not in the measured
// reference. Only continuous, unscheduled source geometry is covered here.
// Both the builder and its immutable receipt must use this same value.
inline double helper_score_constructor_radius(const modem::Config& config,double measured) {
    if(!(std::isfinite(measured) && measured>0) || !config.pattern_symbols ||
       config.symbol_burst.enabled() || config.scheduled_hopping || modem::deferred_symbols(config))return measured;
    const auto samples=modem::symbol_sample_count(config);
    if(!samples || !config.sample_rate)return measured;
    return helper_constructor_outward_add(measured,.125*static_cast<double>(config.sample_rate)/samples);
}
}
