#pragma once

#include "datapump/pattern_receiver.hpp"
#include "pattern_fft_batch.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace datapump::modem::detail {
struct PatternFftStartSelection {
    // Bounded stack scratch, independent of hypothesis/epoch count. An
    // unsupported or numerically uncertain union keeps the original job.
    std::array<FftStartRange,64> ranges{};
    std::size_t range_count=0,selected_count=0;
    bool full=true;
};
// Bounded union of timing cells which can compete for one acquisition peak.
// Shared by execution and the work model; coordinates remain on the original
// logical hop. An unsupported capacity/diameter keeps the original scheduling.
struct PatternFftComponents {
    std::array<FftStartRange,256> ranges{};
    std::size_t count=0;
    bool valid=true;
    void include(const PatternFftStartSelection& selected,std::size_t bin_samples,std::uint64_t radius) {
        if(selected.full || !bin_samples || !radius){valid=false;return;}
        for(std::size_t r=0;valid && r<selected.range_count;++r) {
            auto low=selected.ranges[r].first,high=low+selected.ranges[r].count;
            std::size_t begin=0;
            while(begin<count && ranges[begin].first+ranges[begin].count<=low &&
                static_cast<long double>(low-(ranges[begin].first+ranges[begin].count-1))*bin_samples>=radius)++begin;
            auto end=begin;
            while(end<count && (ranges[end].first<high ||
                static_cast<long double>(ranges[end].first-(high-1))*bin_samples<radius)) {
                low=std::min(low,ranges[end].first);high=std::max(high,ranges[end].first+ranges[end].count);++end;
            }
            if(begin==end) {
                if(count==ranges.size()){valid=false;return;}
                std::move_backward(ranges.begin()+begin,ranges.begin()+count,ranges.begin()+count+1);++count;
            } else {
                std::move(ranges.begin()+end,ranges.begin()+count,ranges.begin()+begin+1);count-=end-begin-1;
            }
            ranges[begin]={low,high-low};
        }
    }
    bool eligible(std::size_t bin_samples,std::uint64_t radius,std::size_t candidate_limit,bool competing)const {
        if(!valid || !count || count>candidate_limit || !competing)return false;
        for(std::size_t i=0;i<count;++i)
            if(static_cast<long double>(ranges[i].count-1)*bin_samples>=radius)return false;
        return true;
    }
};
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
        std::uint64_t first_bin,std::size_t starts,std::size_t bin_samples,
        long double minimum_rate=.99L,long double maximum_rate=1.01L) {
    if(!starts || !bin_samples || !symbol_samples || phase_lower>phase_upper ||
       !std::isfinite(minimum_rate) || !std::isfinite(maximum_rate) || minimum_rate<=0 || maximum_rate<minimum_rate ||
       !pattern_fft_window_finite(window,phase_upper))return true;
    const auto offset=static_cast<long double>(stream_index)*symbol_samples;
    const auto lower=window.epoch_origin_samples+window.phase_scale*phase_lower-
        window.half_width_samples+offset/maximum_rate;
    const auto upper=window.epoch_origin_samples+window.phase_scale*phase_upper+
        window.half_width_samples+offset/minimum_rate;
    const auto first=static_cast<long double>(first_bin)*bin_samples;
    const auto last=(static_cast<long double>(first_bin)+starts-1)*bin_samples;
    const auto magnitude=std::max({1.L,std::abs(window.epoch_origin_samples),
        std::abs(window.phase_scale*phase_upper),std::abs(offset/minimum_rate),
        std::abs(lower),std::abs(upper),std::abs(first),std::abs(last)});
    if(!std::isfinite(magnitude) || magnitude>=1e15L)return true;
    // Runtime oscillator ratios are doubles. Inflate at their precision even
    // when the UTC mapping arithmetic has a wider long-double representation.
    const auto margin=2.5L*bin_samples+64*std::numeric_limits<double>::epsilon()*magnitude;
    return last+margin>=lower && first-margin<=upper;
}

// Intersect each ORIGINAL canonical phase with the original FFT start cells.
// Disjoint phases must not be replaced by their broad group envelope. Keep the
// authoritative bank's complete clock envelope and the preceding fractional/
// refinement margin. Default bounds preserve the legacy +/-1% domain. Ranges
// only select work, never translate cells or shrink any retained rate lane.
inline PatternFftStartSelection pattern_fft_select_starts(const PatternStartWindow& window,
        std::uint64_t symbol_samples,std::uint64_t stream_index,
        std::uint64_t phase_lower,std::uint64_t phase_upper,std::uint64_t phase_step,
        std::uint64_t first_bin,std::size_t starts,std::size_t bin_samples,
        long double minimum_rate=.99L,long double maximum_rate=1.01L) {
    PatternFftStartSelection result;result.selected_count=starts;
    if(!starts || !bin_samples || !symbol_samples || !phase_step || phase_lower>phase_upper ||
       !std::isfinite(minimum_rate) || !std::isfinite(maximum_rate) || minimum_rate<=0 || maximum_rate<minimum_rate ||
       !pattern_fft_window_finite(window,phase_upper))return result;
    const auto offset=static_cast<long double>(stream_index)*symbol_samples;
    const auto first=static_cast<long double>(first_bin)*bin_samples;
    const auto last=(static_cast<long double>(first_bin)+starts-1)*bin_samples;
    const auto base_lower=window.epoch_origin_samples-window.half_width_samples+offset/maximum_rate;
    const auto base_upper=window.epoch_origin_samples+window.half_width_samples+offset/minimum_rate;
    const auto lower=base_lower+window.phase_scale*phase_lower;
    const auto upper=base_upper+window.phase_scale*phase_upper;
    const auto magnitude=std::max({1.L,std::abs(window.epoch_origin_samples),
        std::abs(window.phase_scale*phase_upper),std::abs(offset/minimum_rate),
        std::abs(lower),std::abs(upper),std::abs(first),std::abs(last)});
    if(!std::isfinite(magnitude) || magnitude>=1e15L)return result;
    const auto margin=2.5L*bin_samples+64*std::numeric_limits<double>::epsilon()*magnitude;
    const auto append=[&](long double lo,long double hi) {
        // Inflate the arithmetic endpoint before integer rounding. Uncertain
        // edge cells are retained; selection cannot shrink the stated prior.
        const auto a=std::ceil(std::nextafter((lo-margin-first)/bin_samples,
            -std::numeric_limits<long double>::infinity()));
        const auto z=std::floor(std::nextafter((hi+margin-first)/bin_samples,
            std::numeric_limits<long double>::infinity()))+1;
        if(z<=0 || a>=starts || z<=a)return true;
        const auto begin=static_cast<std::size_t>(std::max(0.L,a));
        const auto end=static_cast<std::size_t>(std::min(static_cast<long double>(starts),z));
        if(result.range_count && begin<=result.ranges[result.range_count-1].first+
                result.ranges[result.range_count-1].count) {
            auto& previous=result.ranges[result.range_count-1];
            previous.count=std::max(previous.first+previous.count,end)-previous.first;
        } else {
            if(result.range_count==result.ranges.size())return false;
            result.ranges[result.range_count++]={begin,end-begin};
        }
        return true;
    };
    result.full=false;result.selected_count=0;
    const auto spacing=window.phase_scale*phase_step;
    const auto phase_intervals=(phase_upper-phase_lower)/phase_step;
    if(phase_intervals==std::numeric_limits<std::uint64_t>::max())return PatternFftStartSelection{{},0,starts,true};
    const auto phases=phase_intervals+1;
    if(spacing<=base_upper-base_lower+2*margin) {
        if(!append(lower,upper))return PatternFftStartSelection{{},0,starts,true};
    } else {
        // Bound iteration by phases that can intersect this batch. The union
        // storage remains fixed even for a very large canonical phase lattice.
        const auto first_phase=std::max(0.L,std::ceil((first-margin-base_upper-
            window.phase_scale*phase_lower)/spacing));
        const auto last_phase=std::min(static_cast<long double>(phases-1),std::floor((last+margin-base_lower-
            window.phase_scale*phase_lower)/spacing));
        if(last_phase>=first_phase) {
            if(last_phase-first_phase>=result.ranges.size())return PatternFftStartSelection{{},0,starts,true};
            for(auto phase=static_cast<std::uint64_t>(first_phase);
                phase<=static_cast<std::uint64_t>(last_phase);++phase) {
                const auto shift=window.phase_scale*(static_cast<long double>(phase_lower)+
                    static_cast<long double>(phase)*phase_step);
                if(!append(base_lower+shift,base_upper+shift))return PatternFftStartSelection{{},0,starts,true};
            }
        }
    }
    for(std::size_t i=0;i<result.range_count;++i)result.selected_count+=result.ranges[i].count;
    if(result.selected_count==starts)result.full=true;
    return result;
}
}
