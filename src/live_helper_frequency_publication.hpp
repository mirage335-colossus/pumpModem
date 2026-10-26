#pragma once

#include "helper_current_link_runtime.hpp"
#include "punctuation_clock.hpp"
#include "datapump/modem.hpp"
#include "datapump/capture_transport.hpp"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace datapump::live::detail {

// Immutable after publication into a queued/staged input owner. This is the
// original RAW direct-frequency observation, not a pointer into the observer's
// short ring, a new estimator, or phase/punctuation/timecode authority.
struct HelperFrequencyPublication {
    helper::PunctuationClockDomain raw_domain;
    helper::LinkMapping mapping;
    helper::CurrentLinkProvider provider;
    modem::OscillatorSearchConfig source_policy;
    // Exact original raw/logical conversion and zero origins at publication.
    // Unknown means no logical-input support certificate, even if Fs match.
    audio::CaptureTransportRecipe transport_recipe;
    std::uint64_t raw_captured_end=0;
    std::uint64_t publication_sample=0,first_sample=0,last_sample=0;
    double hz=0,uncertainty_hz=0,process_uncertainty_hz=0,diffusion_gain=0;
    helper::FrequencyFit fit=helper::FrequencyFit::unknown;

    helper::Reference reference()const noexcept {
        helper::Reference r;r.sample=publication_sample;r.sample_rate=mapping.sample_rate;
        r.frequency_sample=last_sample;r.frequency_first_sample=first_sample;
        r.frequency_hz=hz;r.frequency_uncertainty_hz=uncertainty_hz;
        r.frequency_process_uncertainty_hz=process_uncertainty_hz;
        r.frequency_phase_diffusion_gain=diffusion_gain;r.frequency_fit=fit;r.frequency_valid=true;
        // No phase, punctuation, metadata or independently observed endpoint
        // is synthesized while decoding historical PCM.
        return r;
    }
    static std::optional<std::uint64_t> map_sample(std::uint64_t n,std::uint32_t from,
            std::uint32_t to,bool ceiling)noexcept {
        if(!from||!to)return {};
        const auto whole=n/from,remainder=n%from;
        if(whole>UINT64_MAX/to)return {};
        const auto product=remainder*static_cast<std::uint64_t>(to);
        const auto fraction=product/from+(ceiling&&product%from?1:0);
        const auto base=whole*to;
        if(fraction>UINT64_MAX-base)return {};
        return base+fraction;
    }
    bool matches(const helper::Reference& r,bool raw,std::uint64_t logical_origin=0)const noexcept {
        if(!r.frequency_valid || r.frequency_hz!=hz || r.frequency_uncertainty_hz!=uncertainty_hz ||
           r.frequency_process_uncertainty_hz!=process_uncertainty_hz ||
           r.frequency_phase_diffusion_gain!=diffusion_gain || r.frequency_fit!=fit)return false;
        const auto end=r.frequency_sample?r.frequency_sample:r.sample;
        if(raw)return r.sample_rate==mapping.sample_rate && r.sample==publication_sample &&
            end==last_sample && r.frequency_first_sample==first_sample;
        const auto publication=map_sample(publication_sample,mapping.sample_rate,r.sample_rate,true);
        const auto first=map_sample(first_sample,mapping.sample_rate,r.sample_rate,false);
        const auto last=map_sample(last_sample,mapping.sample_rate,r.sample_rate,false);
        return publication&&first&&last && *publication>=logical_origin && *first>=logical_origin &&
            *last>=logical_origin && *publication-logical_origin==r.sample &&
            *first-logical_origin==r.frequency_first_sample && *last-logical_origin==end;
    }
    bool contains_logical_frontier(std::uint64_t frontier,std::uint64_t origin,
            std::uint32_t logical_rate)const noexcept {
        // Outward capture boundary only. The independently measured frequency
        // date stays unchanged and is revalidated by LinkState at RAW ingress.
        const auto end=map_sample(raw_captured_end,mapping.sample_rate,logical_rate,true);
        return end && *end>=origin && frontier<=*end-origin;
    }
};

inline std::optional<HelperFrequencyPublication> helper_frequency_publication(
        const modem::Config& config,double expected,const helper::Reference& raw,
        std::uint64_t generation,helper::CurrentLinkProvider provider,
        std::uint64_t raw_first,std::uint64_t raw_captured_end,
        audio::CaptureTransportRecipe transport={})noexcept {
    if(!config.oscillator_search || !provider.identity || !raw.sample_rate || !raw.frequency_valid ||
       !std::isfinite(provider.phase_noise_radians_per_sqrt_second) ||
       provider.phase_noise_radians_per_sqrt_second<0 || raw.sample>=raw_captured_end)return {};
    HelperFrequencyPublication p;p.provider=provider;p.source_policy=*config.oscillator_search;
    p.transport_recipe=transport;
    // HelperWorkspace observes original raw input_view zero. Planned/FakeFHSS
    // dates are never a physical capture, provider or phase-gauge identity.
    p.raw_domain={generation,provider.identity,0,provider.identity,raw.sample_rate};
    p.raw_captured_end=raw_captured_end;p.publication_sample=raw.sample;
    p.first_sample=raw.frequency_first_sample;p.last_sample=raw.frequency_sample?raw.frequency_sample:raw.sample;
    p.hz=raw.frequency_hz;p.uncertainty_hz=raw.frequency_uncertainty_hz;
    p.process_uncertainty_hz=raw.frequency_process_uncertainty_hz;p.diffusion_gain=raw.frequency_phase_diffusion_gain;
    p.fit=raw.frequency_fit;
    auto& m=p.mapping;m.capture_generation=generation;m.timebase_identity=provider.identity;
    m.reference_identity_known=false;m.sample_rate=raw.sample_rate;
    m.settled_first_sample=raw_first;m.settled_end_sample=raw_captured_end;
    m.actual_rf_shift_hz=p.source_policy.rf_shift_hz;m.helper_audio_hz=expected;m.data_audio_hz=config.carrier_hz;
    m.sideband_sign=p.source_policy.sideband==modem::OscillatorSideband::upper?1:-1;
    m.mono_image_sign=1;m.phase_segment=provider.identity;
    m.provenance=helper::LinkMappingProvenance::fixed_configuration;m.phase_continuity_known=false;
    return p;
}

// Replaces the existing optional Reference allocation; AudioBlock still owns
// exactly one unique_ptr of the same size when helper is disabled. Each owned
// allocation's complete sizeof is charged, including the inline capsule.
struct HelperOwnedReference {
    helper::Reference value;
    std::optional<HelperFrequencyPublication> frequency;
    std::size_t bytes()const noexcept {
        return sizeof(*this)+(value.metadata?value.metadata->callsign.capacity():0);
    }
};
// Decoder assistance never consumes metadata. Copy each fixed-size observation
// explicitly so optional provenance can yield without allocating a callsign,
// including the Stage allocation-failure fallback. Phase dates stay untouched.
inline helper::Reference helper_assistance_reference(const helper::Reference& source)noexcept {
    helper::Reference result;
    result.sample=source.sample;result.sample_rate=source.sample_rate;
    result.frequency_hz=source.frequency_hz;result.phase_radians=source.phase_radians;
    result.frequency_uncertainty_hz=source.frequency_uncertainty_hz;
    result.phase_uncertainty_radians=source.phase_uncertainty_radians;
    result.dwell_start_seconds=source.dwell_start_seconds;result.dwell_uncertainty_seconds=source.dwell_uncertainty_seconds;
    result.frequency_valid=source.frequency_valid;result.phase_valid=source.phase_valid;result.timing_valid=source.timing_valid;
    result.phase_observation_known=source.phase_observation_known;
    result.phase_observation_sample=source.phase_observation_sample;
    result.frequency_sample=source.frequency_sample;result.frequency_first_sample=source.frequency_first_sample;
    result.frequency_fit=source.frequency_fit;result.frequency_phase_diffusion_gain=source.frequency_phase_diffusion_gain;
    result.frequency_process_uncertainty_hz=source.frequency_process_uncertainty_hz;
    result.punctuation=source.punctuation;return result;
}
inline std::optional<helper::Reference> helper_reference_value(const HelperOwnedReference* r)noexcept {
    return r?std::optional<helper::Reference>{helper_assistance_reference(r->value)}:std::nullopt;
}
inline std::optional<helper::Reference> helper_reference_value(
        const std::optional<HelperOwnedReference>& r)noexcept {
    return helper_reference_value(r?&*r:nullptr);
}
inline const HelperFrequencyPublication* helper_frequency_source(
        const std::optional<HelperOwnedReference>& r)noexcept {
    return r&&r->frequency?&*r->frequency:nullptr;
}
inline const HelperFrequencyPublication* helper_frequency_source(const HelperOwnedReference* r)noexcept {
    return r&&r->frequency?&*r->frequency:nullptr;
}

} // namespace datapump::live::detail
