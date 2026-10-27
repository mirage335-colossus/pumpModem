#pragma once
#include "datapump/pattern_receiver.hpp"
#include "datapump/pattern_search.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace datapump::modem {
// Search coordinates AFTER the synchronous helper translation. This is not a
// waveform/configuration change or evidence that the helper is locked. Every
// physical LF lane and the supplied peer UTC-rate allowance remain admitted.
struct HelperReferenceSearch {
    OscillatorPatternSearch catalogue;
    double rf_half_width_hz=0,clock_frequency_hz=0,residual_center_hz=0;
};

inline HelperReferenceSearch helper_reference_search(const Config& config,
        double uncertainty_hz,double helper_hz,double residual_center_hz,
        double utc_rate_limit) {
    if(!config.oscillator_search || !std::isfinite(uncertainty_hz) || uncertainty_hz<0 ||
       !std::isfinite(helper_hz) || !std::isfinite(residual_center_hz) ||
       !std::isfinite(utc_rate_limit) || utc_rate_limit<0)
        throw Error("realtime helper search requires finite RF/clock bounds");
    validate(config);
    auto corrected=config;
    auto& policy=*corrected.oscillator_search;
    const auto original_lf=policy.reference==OscillatorReference::shared_radio && policy.rf_shift_hz!=0?
        policy.rf.accuracy_ppm:policy.lf.accuracy_ppm;
    const auto infinity=std::numeric_limits<long double>::infinity();
    const auto up=[&](long double x){return std::nextafter(x,infinity);};
    long double width=uncertainty_hz;
    if(utc_rate_limit!=0) {
        const auto physical_lf=up(up(static_cast<long double>(policy.margin)*original_lf)*1e-6L);
        // Physical helper LF a and DATA LF b=(1+a)(1+c)-1 can differ.
        // D*b-H*a=(D-H)*b + H*c*(1+a). Do not silently remove c.
        width=up(width+up(up(std::abs(static_cast<long double>(helper_hz))*utc_rate_limit)*up(1+physical_lf)));
    }
    if(!std::isfinite(width) || width>std::numeric_limits<double>::max())
        throw Error("realtime helper UTC RF uncertainty exceeds numeric range");
    auto rf_width=static_cast<double>(width);
    if(static_cast<long double>(rf_width)<width)
        rf_width=std::nextafter(rf_width,std::numeric_limits<double>::infinity());
    policy.reference=OscillatorReference::independent_audio;
    policy.lf.accuracy_ppm=original_lf;
    // A search-local scale synthesizes U Hz even for an audio-only link.
    // It is never retained as an RF mapping or used for waveform generation.
    if(policy.rf_shift_hz==0)policy.rf_shift_hz=std::max(1.,rf_width*1e6/(5000*policy.margin));
    const auto denominator=policy.rf_shift_hz*policy.margin;
    if(!std::isfinite(denominator) || denominator<=0)
        throw Error("realtime helper residual search has an invalid RF scale");
    policy.rf.accuracy_ppm=rf_width*1e6/denominator;
    PatternSearchResolution resolution;
    resolution.clock_frequency_hz=config.carrier_hz-helper_hz;
    HelperReferenceSearch result;
    result.catalogue=oscillator_pattern_search(corrected,utc_rate_limit,resolution);
    result.rf_half_width_hz=rf_width;result.clock_frequency_hz=*resolution.clock_frequency_hz;
    result.residual_center_hz=residual_center_hz;
    for(auto& pair:result.catalogue.hypotheses) {
        pair.frequency_offset_hz+=residual_center_hz;
        if(!std::isfinite(pair.frequency_offset_hz))
            throw Error("realtime helper residual search exceeds numeric range");
    }
    return result;
}

inline void apply_helper_reference_search(HelperReferenceSearch&& reference,PatternSearch& search) {
    const auto& residual=reference.catalogue;
    search.hypotheses=std::move(reference.catalogue.hypotheses);
    search.frequency_offsets_hz.clear();search.couple_clock_to_carrier=false;
    search.admitted_frequency_half_width_hz=residual.frequency.requested_half_width_hz;
    search.admitted_clock_half_width_ppm=residual.requested_clock_half_width_ppm;
    search.admitted_clock_cell_half_width_ppm=residual.clock_step_ppm/2;
    search.complex_frequency_coverage=!residual.frequency.limited;
    search.complex_clock_coverage=residual.clock_half_width_ppm>=residual.requested_clock_half_width_ppm;
}
}
