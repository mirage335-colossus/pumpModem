#pragma once
#include "datapump/pattern_receiver.hpp"
#include "datapump/pattern_search.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace datapump::live::detail {
// A not-yet-admitted view may wait for a better reference; don't freeze a
// broad immutable original basis merely because the local timer elapsed.
inline bool helper_frontend_basis_useful(const modem::Config& config,double measured_radius,
        bool original_retained,bool all_profiles_minimum,double minimum_profile_cell) {
    if(!std::isfinite(measured_radius)||measured_radius<0)return false;
    if(!original_retained)return true;
    if(!config.oscillator_search||!all_profiles_minimum)return false;
    return std::isfinite(minimum_profile_cell)&&minimum_profile_cell>0&&measured_radius<=minimum_profile_cell;
}
// Cheap sufficient proof that the ordinary builder's original frequency
// lattice has at most its center and two endpoints. Any UTC union or expanded
// search is deliberately left unproved; the later actual catalogue remains
// authoritative. No candidates or PCM are allocated by this check.
inline bool helper_profile_original_minimum(const modem::Config& c,double utc_correction) {
    if(!c.oscillator_search||utc_correction!=0||c.frequency_search_multiplier!=1)return false;
    const auto& p=*c.oscillator_search;
    const bool shared=p.reference==modem::OscillatorReference::shared_radio&&p.rf_shift_hz>0;
    const auto side=p.sideband==modem::OscillatorSideband::upper?1.L:-1.L;
    const auto clock=static_cast<long double>(p.margin)*(shared?p.rf.accuracy_ppm:p.lf.accuracy_ppm)*1e-6L;
    const auto rf=shared?0.L:static_cast<long double>(p.margin)*p.rf_shift_hz*p.rf.accuracy_ppm*1e-6L;
    const auto coupling=shared?std::abs(side*p.rf_shift_hz+c.carrier_hz):std::abs(static_cast<long double>(c.carrier_hz));
    const auto bound=rf+coupling*clock;
    const auto step=2*modem::native_pattern_frequency_cell_radius(c);
    return std::isfinite(bound)&&bound>=0&&std::isfinite(step)&&step>0&&(bound==0||bound<step);
}
// This gate runs on an actual prepared search before a new ordinary receiver is
// constructed. Counts are descriptor evidence, not an execution-cost forecast.
// Timing/clock coverage is checked independently and is never narrowed here.
inline bool helper_acquisition_useful(const modem::Config& config,double helper_hz,
        double measured_radius,const modem::PatternSearch& selected,
        const std::optional<modem::OscillatorPatternSearch>& original,bool original_retained) {
    if(!std::isfinite(measured_radius)||measured_radius<0||!std::isfinite(helper_hz))return false;
    const auto cell=modem::native_pattern_frequency_cell_radius(config);
    if(!std::isfinite(cell)||cell<=0)return false;
    // The default, no-policy receiver has hidden coupled/uncoupled timing
    // lanes. Until an explicit joint constructor receipt preserves all of
    // them, it cannot qualify merely from a scalar measured radius.
    if(!config.oscillator_search)return false;
    if(!original||selected.hypotheses.empty()||!selected.complex_frequency_coverage.value_or(false)||
       !selected.complex_clock_coverage.value_or(false))return false;
    // The ordinary symmetric lattice builder uses center and both endpoints
    // for every nonzero domain; <=3 is its least nonzero frequency lattice.
    // A precise measured interval must still fit within one native cell.
    if(original->frequency.count>0 && original->frequency.count<=3 && measured_radius<=cell &&
       selected.hypotheses.size()<=original->hypotheses.size()) {
        // Count actual RF centers rather than assuming fewer pairs implies
        // fewer frequencies when the clock lane geometry has changed.
        std::array<double,3> centers{};std::size_t count=0;
        for(const auto& item:selected.hypotheses) {
            if(!std::isfinite(item.frequency_offset_hz))return false;
            bool found=false;for(std::size_t i=0;i<count;++i)found|=centers[i]==item.frequency_offset_hz;
            if(!found){if(count==original->frequency.count || count==centers.size())return false;centers[count++]=item.frequency_offset_hz;}
        }
        return count!=0;
    }
    if(original_retained || selected.hypotheses.size()>=original->hypotheses.size())return false;
    // The Helper-enabled Bank reserves this complete serialized audit scratch.
    constexpr std::size_t capacity=modem::maximum_pattern_frequency_rate_hypotheses;
    if(selected.hypotheses.size()>capacity)return false;
    std::array<double,capacity> centers{};
    for(std::size_t i=0;i<selected.hypotheses.size();++i) {
        centers[i]=selected.hypotheses[i].frequency_offset_hz;
        if(!std::isfinite(centers[i]))return false;
    }
    const auto end=centers.begin()+static_cast<std::ptrdiff_t>(selected.hypotheses.size());
    std::sort(centers.begin(),end);
    const auto distinct=static_cast<std::size_t>(std::unique(centers.begin(),end)-centers.begin());
    if(!distinct || distinct>=original->frequency.count)return false;
    const auto& p=*config.oscillator_search;
    const bool shared=p.reference==modem::OscillatorReference::shared_radio&&p.rf_shift_hz>0;
    const auto side=p.sideband==modem::OscillatorSideband::upper?1.:-1.;
    const auto clock=(shared?p.rf.accuracy_ppm:p.lf.accuracy_ppm)*p.margin*1e-6;
    const auto rf=shared?0.:p.rf_shift_hz*p.rf.accuracy_ppm*p.margin*1e-6;
    const auto coupling=shared?std::abs(side*p.rf_shift_hz+config.carrier_hz):std::abs(config.carrier_hz);
    const auto extra=(rf+coupling*clock)*std::max(0.,config.frequency_search_multiplier-1);
    // The shared source's high pilot observes the coupled RF/audio parameter;
    // for independent clocks only the common RF displacement is compared.
    const auto original_rf=shared?std::abs(side*p.rf_shift_hz+helper_hz)*clock+extra:rf+extra;
    // Include the ordinary constructor's additional coverage cell; no width or
    // descriptor comparison removes an unresolved clock or UTC alternative.
    const auto requested=measured_radius+cell;
    return std::isfinite(original_rf)&&std::isfinite(requested)&&
        (requested<original_rf || measured_radius<=cell);
}
}
