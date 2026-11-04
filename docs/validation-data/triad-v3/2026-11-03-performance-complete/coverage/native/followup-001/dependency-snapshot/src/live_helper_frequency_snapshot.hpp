#pragma once

#include "live_helper_frequency_publication.hpp"
#include "live_helper_clock.hpp"

namespace datapump::live::detail {

// Small call-scope raw FREQUENCY frame. It has no marker history, phase or
// punctuation clock and cannot be passed to the joint clock context. Its
// borrowed mapping belongs to the immutable cohort capsule through the joined
// call; neither the frame nor a pointer escapes into pending work or tracks.
struct HelperOwnedFrequencyFrame {
    helper::PunctuationClockDomain domain;
    audio::CaptureTransportRecipe transport_recipe;
    HelperMeasurement frequency;
    std::uint64_t captured_first_sample=0,captured_end_sample=0;
    std::optional<std::uint64_t> frequency_observation_frontier;
    double expected_helper_hz=0;
    const helper::LinkMapping* actual_mapping=nullptr;
    bool capture_known=false,frequency_known=false;
};
static_assert(sizeof(std::optional<HelperOwnedFrequencyFrame>)<=512);
inline std::optional<HelperOwnedFrequencyFrame> helper_owned_frequency_frame(
        const HelperFrequencyPublication& source,const modem::Config& config,double expected)noexcept {
    const auto& m=source.mapping;const auto& d=source.raw_domain;const auto& t=source.transport_recipe;
    if(!config.oscillator_search || source.source_policy!=*config.oscillator_search ||
       !d.capture_generation || !d.provider_identity || !d.sample_rate || d.input_view ||
       d.rf_gauge!=source.provider.identity || d.provider_identity!=source.provider.identity ||
       m.capture_generation!=d.capture_generation || m.timebase_identity!=d.provider_identity ||
       m.phase_segment!=d.rf_gauge || m.sample_rate!=d.sample_rate ||
       m.provenance!=helper::LinkMappingProvenance::fixed_configuration || m.mono_image_sign!=1 ||
       m.actual_rf_shift_hz!=source.source_policy.rf_shift_hz || m.helper_audio_hz!=expected ||
       m.data_audio_hz!=config.carrier_hz ||
       m.sideband_sign!=(source.source_policy.sideband==modem::OscillatorSideband::upper?1:-1) ||
       m.settled_first_sample>source.first_sample || source.first_sample>source.last_sample ||
       source.last_sample>source.publication_sample || source.publication_sample>=source.raw_captured_end ||
       m.settled_end_sample!=source.raw_captured_end ||
       t.kind==audio::CaptureTransportKind::unknown || t.raw_rate!=d.sample_rate || t.logical_rate!=config.sample_rate)
        return {};
    HelperOwnedFrequencyFrame result;result.domain=d;result.transport_recipe=t;
    result.frequency=HelperMeasurement(source.reference());result.capture_known=result.frequency_known=true;
    result.captured_first_sample=m.settled_first_sample;result.captured_end_sample=source.raw_captured_end;
    result.frequency_observation_frontier=source.raw_captured_end;result.expected_helper_hz=expected;
    result.actual_mapping=&source.mapping;return result;
}
inline LiveHelperClockFrequencyInput helper_owned_frequency_input(const modem::Config& config,
        const HelperOwnedFrequencyFrame& frame,helper::JointClockSourceRelation source_relation,
        std::optional<double> original_data_clock_half_width_ppm,
        std::optional<std::uint64_t> scored_raw_last_sample)noexcept {
    if(!frame.actual_mapping)return {};
    return live_helper_raw_frequency_input(config,frame,1,source_relation,
        original_data_clock_half_width_ppm,scored_raw_last_sample,frame.actual_mapping);
}

// Requires the additive frequency_observation_frontier API owned by the clock
// component. On failure, the caller declines this paired snapshot. No fallback
// may silently substitute another raw frequency into only half of the proof.
inline bool bind_helper_frequency_publication(LiveHelperClockSnapshot& snapshot,
        const HelperFrequencyPublication& source,const modem::Config& config,double expected,
        double actual_provider_phase_sigma)noexcept {
    if(!snapshot.capture_known || !config.oscillator_search ||
       !source.provider.identity || !source.raw_domain.capture_generation ||
       !std::isfinite(actual_provider_phase_sigma) || actual_provider_phase_sigma<0 ||
       source.provider.phase_noise_radians_per_sqrt_second!=actual_provider_phase_sigma ||
       source.raw_domain!=snapshot.domain || source.source_policy!=*config.oscillator_search ||
       source.provider.identity!=snapshot.domain.provider_identity ||
       source.mapping.capture_generation!=snapshot.domain.capture_generation ||
       source.mapping.sample_rate!=snapshot.domain.sample_rate ||
       source.mapping.phase_segment!=snapshot.domain.rf_gauge ||
       source.mapping.timebase_identity!=source.provider.identity ||
       source.mapping.provenance!=helper::LinkMappingProvenance::fixed_configuration ||
       source.mapping.actual_rf_shift_hz!=source.source_policy.rf_shift_hz ||
       source.mapping.sideband_sign!=(source.source_policy.sideband==modem::OscillatorSideband::upper?1:-1) ||
       source.mapping.mono_image_sign!=1 || source.raw_domain.input_view!=0 ||
       source.transport_recipe.kind==audio::CaptureTransportKind::unknown ||
       source.transport_recipe!=snapshot.transport_recipe ||
       source.mapping.helper_audio_hz!=expected || snapshot.expected_helper_hz!=expected ||
       source.mapping.settled_end_sample!=source.raw_captured_end ||
       snapshot.captured_first_sample>=snapshot.captured_end_sample ||
       source.first_sample<snapshot.captured_first_sample || source.first_sample>source.last_sample ||
       source.first_sample<source.mapping.settled_first_sample ||
       source.last_sample>source.publication_sample || source.publication_sample>=source.raw_captured_end ||
       source.raw_captured_end>snapshot.captured_end_sample)return false;
    // Frequency only. Markers/phase, transport, actual capture bounds and
    // scored/processed frontiers are intentionally not assigned here.
    snapshot.frequency=HelperMeasurement(source.reference());snapshot.frequency_known=true;
    snapshot.frequency_observation_frontier=source.raw_captured_end;
    return true;
}

// A monotone compact/logical receiver reads only its ingested input prefix.
// Map the COMPLETE original unfiltered bank chunk, including every upstream
// resampler's right halo. The cohort PayloadFilter is causal, so its output
// cannot require a later source than this original chunk. This is not the
// native FFT/refinement proof below and must never replace it.
enum class HelperFrequencyIngressReason {
    accepted,invalid_binding,unsupported_transport,coordinate_overflow,empty_prefix,beyond_source_date
};
struct HelperFrequencyIngressProof {
    HelperFrequencyIngressReason reason=HelperFrequencyIngressReason::invalid_binding;
    std::uint64_t raw_last_sample=0;
    explicit operator bool()const noexcept{return reason==HelperFrequencyIngressReason::accepted;}
};
inline const char* helper_frequency_ingress_reason(HelperFrequencyIngressReason reason)noexcept {
    using Reason=HelperFrequencyIngressReason;
    switch(reason) {
    case Reason::accepted:return "actual whole-input raw adjoint is within the original frequency SourceDate";
    case Reason::invalid_binding:return "owned frequency provider/rate/transport binding is invalid";
    case Reason::unsupported_transport:return "actual immutable raw/logical transport is unknown or unsupported";
    case Reason::coordinate_overflow:return "whole-input transport adjoint exceeds sample coordinates";
    case Reason::empty_prefix:return "no original logical input prefix has been ingested";
    case Reason::beyond_source_date:return "whole-input raw resampler support reaches beyond the original frequency SourceDate";
    }
    return "owned frequency whole-input support is unavailable";
}
inline HelperFrequencyIngressProof helper_frequency_ingress_support(
        const HelperFrequencyPublication& source,std::uint32_t logical_rate,
        std::uint64_t bank_source_origin,std::uint64_t complete_bank_ingress_end)noexcept {
    using Reason=HelperFrequencyIngressReason;
    const auto& recipe=source.transport_recipe;
    if(!source.raw_domain.capture_generation || !source.raw_domain.provider_identity ||
       source.raw_domain.input_view || source.raw_domain.rf_gauge!=source.provider.identity ||
       source.raw_domain.provider_identity!=source.provider.identity ||
       source.mapping.capture_generation!=source.raw_domain.capture_generation ||
       source.mapping.timebase_identity!=source.provider.identity ||
       source.mapping.phase_segment!=source.raw_domain.rf_gauge ||
       source.mapping.sample_rate!=source.raw_domain.sample_rate ||
       source.mapping.provenance!=helper::LinkMappingProvenance::fixed_configuration ||
       source.mapping.mono_image_sign!=1 ||
       source.mapping.settled_end_sample!=source.raw_captured_end ||
       recipe.raw_rate!=source.raw_domain.sample_rate || recipe.logical_rate!=logical_rate)
        return {Reason::invalid_binding};
    if(complete_bank_ingress_end>UINT64_MAX-bank_source_origin)return {Reason::coordinate_overflow};
    const auto end=bank_source_origin+complete_bank_ingress_end;
    if(end<=recipe.logical_zero)return {Reason::empty_prefix};
    std::optional<LiveHelperClockCaptureMap> mapping;
    using Kind=audio::CaptureTransportKind;
    if(recipe.kind==Kind::fixed_nominal_audio)
        mapping=LiveHelperClockCaptureMap::nominal_audio(
            LiveHelperClockTransportBasis::fixed_nominal_unshifted,source.raw_domain,
            logical_rate,recipe.raw_zero,recipe.logical_zero);
    else if(recipe.kind==Kind::sampled_physical_halfband)
        mapping=LiveHelperClockCaptureMap::simulation_halfband(
            LiveHelperClockTransportBasis::fixed_nominal_unshifted,source.raw_domain,
            logical_rate,recipe.raw_zero,recipe.logical_zero);
    if(!mapping)return {Reason::unsupported_transport};
    const auto last=mapping->right_adjoint_last(end);
    if(!last)return {Reason::coordinate_overflow};
    if(*last>=source.raw_captured_end)return {Reason::beyond_source_date,*last};
    return {Reason::accepted,*last};
}
inline std::optional<std::uint64_t> helper_frequency_ingress_raw_last(
        const HelperFrequencyPublication& source,std::uint32_t logical_rate,
        std::uint64_t bank_source_origin,std::uint64_t complete_bank_ingress_end)noexcept {
    const auto proof=helper_frequency_ingress_support(source,logical_rate,bank_source_origin,complete_bank_ingress_end);
    return proof?std::optional<std::uint64_t>{proof.raw_last_sample}:std::nullopt;
}

// This is the core's FULL scored FFT/FIR/refinement support, not the current
// audio block slice. An owned old SourceDate cannot condition later raw data.
template<class RawFrequencyFrame>
inline std::optional<std::uint64_t> helper_frequency_scored_raw_last(
        const RawFrequencyFrame& snapshot,const modem::Config& config,
        const modem::PatternAcquisitionScoredSupport& scored,std::uint64_t bank_source_origin,
        std::uint64_t fixed_filter_radius,std::uint64_t input_captured_end_sample)noexcept {
    if(!snapshot.frequency_observation_frontier || !snapshot.capture_known ||
       scored.domain.input_phase_view_id || scored.domain.frequency_origin_hz!=0 ||
       scored.last_sample>=input_captured_end_sample || scored.consumed_end_sample>input_captured_end_sample ||
       snapshot.transport_recipe.raw_rate!=snapshot.domain.sample_rate ||
       snapshot.transport_recipe.logical_rate!=config.sample_rate)return {};
    const auto& recipe=snapshot.transport_recipe;
    std::optional<LiveHelperClockCaptureMap> mapping;
    using Kind=audio::CaptureTransportKind;
    if(recipe.kind==Kind::fixed_nominal_audio)
        mapping=LiveHelperClockCaptureMap::nominal_audio(
            LiveHelperClockTransportBasis::fixed_nominal_unshifted,snapshot.domain,
            recipe.logical_rate,recipe.raw_zero,recipe.logical_zero);
    else if(recipe.kind==Kind::sampled_physical_halfband)
        mapping=LiveHelperClockCaptureMap::simulation_halfband(
            LiveHelperClockTransportBasis::fixed_nominal_unshifted,snapshot.domain,
            recipe.logical_rate,recipe.raw_zero,recipe.logical_zero);
    if(!mapping)return {};
    const auto raw=mapping->expand(scored,bank_source_origin,fixed_filter_radius,
        snapshot.captured_first_sample,snapshot.captured_end_sample);
    if(!raw || raw.last_sample>=*snapshot.frequency_observation_frontier)return {};
    return raw.last_sample;
}

} // namespace datapump::live::detail
