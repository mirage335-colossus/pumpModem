#pragma once
#include "pattern_fft_window.hpp"

namespace datapump::modem::detail {
// Borrowed for one joined acquisition call; never retained by a decoder or a
// payload track. Source-period coordinates and physical input coordinates are
// deliberately separate. The rate interval contains ALL original LF cells.
struct PatternFftPeriodicTiming {
    PatternPeriodicTimingPrior prior;
    long double minimum_rate=0,maximum_rate=0;
};
struct PatternFftPeriodicSelection {
    PatternFftStartSelection starts;
    bool declined=false;
};
inline PatternFftPeriodicSelection pattern_fft_periodic_select_starts(
        const PatternFftStartSelection& original,const PatternFftPeriodicTiming& timing,
        std::uint64_t source_slot,std::uint64_t first_bin,std::size_t starts,std::size_t bin_samples) {
    const auto fallback=[&]{return PatternFftPeriodicSelection{original,true};};
    const auto& p=timing.prior;
    if(!starts || !bin_samples || !std::isfinite(p.observed_sample) || !std::isfinite(p.source_phase_samples) ||
       !std::isfinite(p.period_samples) || p.period_samples<1 || !std::isfinite(p.half_width_samples) || p.half_width_samples<0 ||
       !std::isfinite(timing.minimum_rate) || !std::isfinite(timing.maximum_rate) ||
       timing.minimum_rate<=0 || timing.maximum_rate<timing.minimum_rate)return fallback();
    const auto first=static_cast<long double>(first_bin)*bin_samples;
    const auto last=(static_cast<long double>(first_bin)+starts-1)*bin_samples;
    const auto q=static_cast<long double>(source_slot)-p.source_phase_samples;
    const auto magnitude=std::max({1.L,std::abs(first),std::abs(last),std::abs(p.observed_sample),
        std::abs(q),p.period_samples,p.half_width_samples});
    if(!std::isfinite(magnitude) || magnitude>=1e15L)return fallback();
    // Match the original arrival-cell cover: half a bin and both two-bin
    // refinements. Wider arithmetic padding retains uncertain endpoints.
    const auto margin=2.5L*bin_samples+128*std::numeric_limits<double>::epsilon()*magnitude;
    const auto low_delta=first-margin-p.observed_sample-p.half_width_samples;
    const auto high_delta=last+margin-p.observed_sample+p.half_width_samples;
    const std::array<long double,4> source_deltas{low_delta*timing.minimum_rate,low_delta*timing.maximum_rate,
        high_delta*timing.minimum_rate,high_delta*timing.maximum_rate};
    const auto source_low=*std::min_element(source_deltas.begin(),source_deltas.end());
    const auto source_high=*std::max_element(source_deltas.begin(),source_deltas.end());
    const auto source_pad=128*std::numeric_limits<double>::epsilon()*std::max({magnitude,
        std::abs(source_low),std::abs(source_high)});
    const auto low_alias=std::ceil(std::nextafter((source_low-q-source_pad)/p.period_samples,
        -std::numeric_limits<long double>::infinity()));
    const auto high_alias=std::floor(std::nextafter((source_high-q+source_pad)/p.period_samples,
        std::numeric_limits<long double>::infinity()));
    // Stay far inside the integer conversion boundary even on platforms whose
    // long double has only double precision at INT64_MAX.
    if(!std::isfinite(low_alias) || !std::isfinite(high_alias) ||
       std::abs(low_alias)>=1e15L || std::abs(high_alias)>=1e15L)return fallback();
    PatternFftStartSelection periodic;periodic.full=false;
    if(high_alias>=low_alias) {
        // Bound work independently of elapsed capture/epoch time. Capacity or
        // numerical uncertainty declines this optional selector, not a lane.
        if(high_alias-low_alias>=periodic.ranges.size())return fallback();
        const auto append=[&](long double lo,long double hi) {
            const auto a=std::ceil(std::nextafter((lo-margin-first)/bin_samples,
                -std::numeric_limits<long double>::infinity()));
            const auto z=std::floor(std::nextafter((hi+margin-first)/bin_samples,
                std::numeric_limits<long double>::infinity()))+1;
            if(!std::isfinite(a) || !std::isfinite(z))return false;
            if(z<=0 || a>=starts || z<=a)return true;
            const auto begin=static_cast<std::size_t>(std::max(0.L,a));
            const auto end=static_cast<std::size_t>(std::min(static_cast<long double>(starts),z));
            if(periodic.range_count && begin<=periodic.ranges[periodic.range_count-1].first+
                    periodic.ranges[periodic.range_count-1].count) {
                auto& previous=periodic.ranges[periodic.range_count-1];
                previous.count=std::max(previous.first+previous.count,end)-previous.first;
            } else {
                if(periodic.range_count==periodic.ranges.size())return false;
                periodic.ranges[periodic.range_count++]={begin,end-begin};
            }
            return true;
        };
        const auto end=static_cast<std::int64_t>(high_alias);
        for(auto k=static_cast<std::int64_t>(low_alias);; ++k) {
            const auto d=q+static_cast<long double>(k)*p.period_samples;
            const auto a=d/timing.minimum_rate,b=d/timing.maximum_rate;
            const auto pad=128*std::numeric_limits<double>::epsilon()*std::max({magnitude,std::abs(d),std::abs(a),std::abs(b)});
            if(!std::isfinite(d) || !std::isfinite(a) || !std::isfinite(b) ||
               !append(p.observed_sample-p.half_width_samples+std::min(a,b)-pad,
                       p.observed_sample+p.half_width_samples+std::max(a,b)+pad))return fallback();
            if(k==end)break;
        }
    }
    PatternFftStartSelection result;result.full=false;
    const auto include=[&](std::size_t lo,std::size_t hi) {
        if(lo>=hi)return true;
        if(result.range_count==result.ranges.size())return false;
        result.ranges[result.range_count++]={lo,hi-lo};result.selected_count+=hi-lo;return true;
    };
    for(std::size_t pidx=0,oidx=0;pidx<periodic.range_count;) {
        const auto p_range=periodic.ranges[pidx];
        if(original.full) {
            if(!include(p_range.first,p_range.first+p_range.count))return fallback();
            ++pidx;continue;
        }
        if(oidx==original.range_count)break;
        const auto o_range=original.ranges[oidx];
        if(!include(std::max(p_range.first,o_range.first),
                    std::min(p_range.first+p_range.count,o_range.first+o_range.count)))return fallback();
        if(p_range.first+p_range.count<=o_range.first+o_range.count)++pidx;else ++oidx;
    }
    result.full=result.selected_count==starts;
    return {result,false};
}
} // namespace datapump::modem::detail
