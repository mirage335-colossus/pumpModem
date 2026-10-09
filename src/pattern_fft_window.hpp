#pragma once

#include "datapump/pattern_receiver.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace datapump::modem::detail {
inline bool pattern_fft_window_finite(const PatternStartWindow& window,std::uint64_t maximum_phase) {
    return std::isfinite(window.epoch_origin_samples) && std::isfinite(window.phase_scale) &&
        window.phase_scale>0 && std::isfinite(window.half_width_samples) && window.half_width_samples>=0 &&
        std::abs(window.epoch_origin_samples)+window.phase_scale*maximum_phase+window.half_width_samples<1e15L;
}

// Conservative intersection of the ORIGINAL FFT start cells with one complete
// cryptographic phase group. No start or phase is translated/rounded. Mixed
// batches remain unchanged. Bound the symbol offset over the complete clock
// domain validated by PatternReceiver, rather than assuming its nearest lane
// is the exact physical clock. Retain the existing two-bin timing refinements
// and half of a coverage cell on either side. Uncertain numerics keep the work.
inline bool pattern_fft_batch_may_intersect(const PatternStartWindow& window,
        std::uint64_t symbol_samples,std::uint64_t stream_index,
        std::uint64_t phase_lower,std::uint64_t phase_upper,
        std::uint64_t first_bin,std::size_t starts,std::size_t bin_samples) {
    if(!starts || !bin_samples || !symbol_samples || phase_lower>phase_upper ||
       !pattern_fft_window_finite(window,phase_upper))return true;
    const auto offset=static_cast<long double>(stream_index)*symbol_samples;
    const auto lower=window.epoch_origin_samples+window.phase_scale*phase_lower-
        window.half_width_samples+offset/1.01L;
    const auto upper=window.epoch_origin_samples+window.phase_scale*phase_upper+
        window.half_width_samples+offset/.99L;
    const auto first=static_cast<long double>(first_bin)*bin_samples;
    const auto last=(static_cast<long double>(first_bin)+starts-1)*bin_samples;
    const auto magnitude=std::max({1.L,std::abs(window.epoch_origin_samples),
        std::abs(window.phase_scale*phase_upper),std::abs(offset/.99L),
        std::abs(lower),std::abs(upper),std::abs(first),std::abs(last)});
    if(!std::isfinite(magnitude) || magnitude>=1e15L)return true;
    // Runtime oscillator ratios are doubles. Inflate at their precision even
    // when the UTC mapping arithmetic has a wider long-double representation.
    const auto margin=2.5L*bin_samples+64*std::numeric_limits<double>::epsilon()*magnitude;
    return last+margin>=lower && first-margin<=upper;
}
}
