#pragma once
#include "live_helper.hpp"
#include "live_helper_prior.hpp"
#include "datapump/capture_transport.hpp"

namespace datapump::live::detail {
// Admission provenance, never a replacement for a per-row acquisition mask.
// Common-only references cannot feed generic phase, translated/shared views,
// compact receivers or any consumer retaining the separate physical-a algebra.
enum class HelperCohortQualification : std::uint8_t {
    unavailable,physical_audio,common_corrected_source_only
};
enum class HelperCohortReferenceTimebase : std::uint8_t { decoder_logical,captured_raw };
inline bool helper_common_source_native_domain(const modem::Config& c,
        const modem::PatternAcquisitionState& original,bool raw_input,bool phase_view,
        const audio::CaptureTransportRecipe& transport) noexcept {
    const bool nominal=transport.kind==audio::CaptureTransportKind::fixed_nominal_audio ||
        transport.kind==audio::CaptureTransportKind::sampled_physical_halfband;
    return raw_input && !phase_view && nominal && transport.raw_rate &&
        transport.logical_rate==c.sample_rate && c.pattern_symbols && c.scramble &&
        c.fake_hopping && c.fake_hopping->defer_symbols && !c.symbol_burst.enabled() &&
        !c.scheduled_hopping && c.oscillator_search &&
        c.oscillator_search->reference==modem::OscillatorReference::independent_audio &&
        original.supported && original.original_pairs && original.frequency_origin_hz==0 && !std::signbit(original.frequency_origin_hz) &&
        !original.qualified_clock_interval && original.admitted_clock_half_width_ppm &&
        original.admitted_clock_cell_half_width_ppm &&
        std::isfinite(*original.admitted_clock_half_width_ppm) && *original.admitted_clock_half_width_ppm>=0 &&
        *original.admitted_clock_half_width_ppm<=10000 &&
        std::isfinite(*original.admitted_clock_cell_half_width_ppm) && *original.admitted_clock_cell_half_width_ppm>=0;
}
// The current Data Pump producer composes helper and DATA at the SAME corrected
// source coordinate b=(1+a)*(1+c)-1. The original receiver's immutable DATA box
// already includes every admitted c. This CONDITIONAL same-transmitter relation
// grants neither a=0/c=0 nor clock/phase/UTC knowledge. A generic standalone
// helper beacon alone cannot establish it. Old physical-a queries stay separate.
inline helper::LinkResult helper_common_source_current_measurement(const modem::Config& c,double expected,
        const helper::Reference& reference,std::uint64_t captured_frontier,std::uint64_t capture_generation,
        const modem::PatternAcquisitionState& original,bool raw_input,bool phase_view,
        const audio::CaptureTransportRecipe& transport,
        HelperCohortReferenceTimebase timebase=HelperCohortReferenceTimebase::decoder_logical,
        const helper::LinkMapping* actual_raw_mapping=nullptr) noexcept {
    helper::LinkResult declined;declined.reason=helper::LinkReason::invalid_model;
    if(!helper_common_source_native_domain(c,original,raw_input,phase_view,transport))return declined;
    const auto reference_rate=timebase==HelperCohortReferenceTimebase::decoder_logical?c.sample_rate:
        timebase==HelperCohortReferenceTimebase::captured_raw?transport.raw_rate:0;
    if(!reference_rate || reference.sample_rate!=reference_rate)return declined;
    const auto& policy=*c.oscillator_search;
    const auto accuracy=[&](double ppm) {
        if(ppm==0)return 0.;
        const auto infinity=std::numeric_limits<double>::infinity();
        return std::nextafter(std::nextafter(ppm*policy.margin,infinity)*std::nextafter(1e-6,infinity),infinity);
    };
    helper::LinkModel model;model.topology=helper::LinkTopology::independent_audio;
    model.rf_accuracy_fraction=accuracy(policy.rf.accuracy_ppm);
    const auto b=*original.admitted_clock_half_width_ppm;
    model.audio_accuracy_fraction=b==0?0:std::nextafter(b/1e6,std::numeric_limits<double>::infinity());
    model.maximum_current_measurement_age_seconds=1; // Freshness only; evolution stays UNKNOWN.
    helper::LinkState current;if(!current.reset(model))return declined;
    helper::LinkMapping mapping;
    if(timebase==HelperCohortReferenceTimebase::captured_raw) {
        // A funded owned capsule already carries this ACTUAL settled mapping.
        // Failure to bind it must never fall back to a synthetic infinite map.
        // Caller additionally binds provider/gauge/recipe to its raw domain.
        declined.reason=helper::LinkReason::invalid_mapping;
        if(!actual_raw_mapping || actual_raw_mapping->capture_generation!=capture_generation ||
           actual_raw_mapping->sample_rate!=reference.sample_rate || actual_raw_mapping->helper_audio_hz!=expected ||
           actual_raw_mapping->data_audio_hz!=c.carrier_hz || actual_raw_mapping->actual_rf_shift_hz!=policy.rf_shift_hz ||
           actual_raw_mapping->sideband_sign!=(policy.sideband==modem::OscillatorSideband::upper?1:-1) ||
           actual_raw_mapping->mono_image_sign!=1)return declined;
        mapping=*actual_raw_mapping;
    } else {
        if(actual_raw_mapping)return declined;
        mapping.capture_generation=capture_generation;
        mapping.timebase_identity=capture_generation;mapping.reference_identity_known=true;mapping.reference_identity=0;
        mapping.sample_rate=reference.sample_rate;mapping.settled_end_sample=UINT64_MAX;
        mapping.actual_rf_shift_hz=policy.rf_shift_hz;mapping.helper_audio_hz=expected;mapping.data_audio_hz=c.carrier_hz;
        mapping.sideband_sign=policy.sideband==modem::OscillatorSideband::upper?1:-1;
        mapping.mono_image_sign=1;mapping.provenance=helper::LinkMappingProvenance::fixed_configuration;
    }
    return current.observe({reference,mapping,{}},captured_frontier);
}
// Coverage-only observer envelope. No measurement, receiver domain or common
// qualification is created here. Physical and corrected-source requests are
// UNIONED; caps/passbands and actual coarse-capture eligibility stay unchanged.
inline std::optional<double> helper_cohort_discovery_radius(const modem::Config& c,double expected,
        bool ordinary_utc_steering) noexcept {
    using namespace helper_prior_detail;
    if(!c.oscillator_search || !std::isfinite(expected) || !environment())return {};
    const auto& p=*c.oscillator_search;
    if((p.reference!=modem::OscillatorReference::shared_radio && p.reference!=modem::OscillatorReference::independent_audio) ||
       (p.sideband!=modem::OscillatorSideband::upper && p.sideband!=modem::OscillatorSideband::lower))return {};
    if(!std::isfinite(p.margin) || p.margin<1 || !std::isfinite(p.rf.accuracy_ppm) || p.rf.accuracy_ppm<0 ||
       !std::isfinite(p.lf.accuracy_ppm) || p.lf.accuracy_ppm<0 || !std::isfinite(p.rf_shift_hz) || p.rf_shift_hz<0)return {};
    const auto rf=ppm(product(p.rf.accuracy_ppm,p.margin)).upper;
    const auto shared=p.reference==modem::OscillatorReference::shared_radio && p.rf_shift_hz>0;
    const auto side=p.sideband==modem::OscillatorSideband::upper?1.:-1.;
    if(shared) {
        const auto coefficient=std::max(std::abs(sum(side*p.rf_shift_hz,expected).lower),
            std::abs(sum(side*p.rf_shift_hz,expected).upper));
        const auto radius=product(coefficient,rf).upper;
        return std::isfinite(radius)?std::optional<double>{radius}:std::nullopt;
    }
    const auto a=ppm(product(p.lf.accuracy_ppm,p.margin)).upper;
    auto b=a;
    if(ordinary_utc_steering)try {
        const auto correction=modem::utc_transmit_rate_limit(c);
        b=sum(sum(a,correction).upper,product(a,correction).upper).upper;
    }catch(...){return {};}
    const auto radius=sum(product(p.rf_shift_hz,rf).upper,product(std::abs(expected),std::max(a,b)).upper).upper;
    return std::isfinite(radius)?std::optional<double>{radius}:std::nullopt;
}
// Match the actual narrow observer constructor and the cohort's admission
// gate. A larger union is only a coarse discovery request; no +/-100-Hz
// observer or unavailable independent wide-input path is silently expanded.
inline double helper_cohort_tracker_radius(const modem::Config& c,double expected,
        double original_request,bool ordinary_utc_steering) noexcept {
    if(!std::isfinite(original_request) || original_request<0)return 0;
    auto radius=std::min(100.,original_request);
    if(const auto discovery=helper_cohort_discovery_radius(c,expected,ordinary_utc_steering))
        radius=std::max(radius,std::min(100.,*discovery));
    return radius;
}
}
