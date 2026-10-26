#pragma once

#include "helper_link_state.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace datapump::helper {

// PRIVATE caller proposal, not a tuner, phase controller or new estimator.
// A persistent owner binds the last independently dated direct-frequency fit
// to its actual capture/provider/mapping. UNKNOWN thermal evolution keeps each
// new fit independent: LinkState starts from its original static box, not the
// first fit. Wiener fit uncertainty is already in Reference and is not added a
// second time. Phase/punctuation/metadata are deliberately not retained here.
struct CurrentLinkProvider {
    std::uint64_t identity=0;
    double phase_noise_radians_per_sqrt_second=0;
};
enum class CurrentLinkReason {
    accepted, unconfigured, optional_work_unfunded, provider_changed,
    capture_changed, mapping_change_unproved, nonincreasing_fit,
    reference_unavailable, direct_frequency_declined, correction_unrepresentable
};
struct CurrentLinkResult {
    LinkResult link;
    CurrentLinkReason reason=CurrentLinkReason::unconfigured;
    bool available=false;
    bool new_independent_fit=false;
    // RAW physical input only. This is not a transformed-view catalogue,
    // an NCO update or a precision lease over later/historical payload samples.
    double correction_hz=0,correction_uncertainty_hz=0;
    std::uint64_t first_sample=0,last_sample=0,captured_end_sample=0;
};

class CurrentLinkRuntime {
public:
    // Actual retained charge is sizeof(*this), never this structural ceiling.
    // Queries run only in Live's already serialized receiver-control worker.
    // Its existing 64KiB temporary allowance is NOT a new per-object heap or
    // a claimed measured frame. New emitted-callgraph qualification is pending.
    static constexpr std::size_t retained_storage_bound=4096;
    static constexpr std::size_t receiver_control_scratch_allowance=65536;
    static constexpr std::size_t storage_required()noexcept {return sizeof(CurrentLinkRuntime);}

    bool configure(const LinkModel& model,CurrentLinkProvider provider,std::size_t storage_grant,
            std::size_t receiver_control_grant) noexcept {
        if(configured_ || storage_grant<sizeof(*this) || receiver_control_grant<receiver_control_scratch_allowance || !provider.identity ||
           !std::isfinite(provider.phase_noise_radians_per_sqrt_second) ||
           provider.phase_noise_radians_per_sqrt_second<0 || !state_.reset(model))return false;
        model_=model;provider_=provider;configured_=worker_funded_=true;return true;
    }
    // Explicit caller-owned raw provider/gauge reset. No allocation, new PCM,
    // message owner, phase continuity or stale reference survives this boundary.
    bool reset_provider(CurrentLinkProvider provider,std::size_t receiver_control_grant)noexcept {
        if(!configured_ || receiver_control_grant<receiver_control_scratch_allowance || !provider.identity ||
           provider.phase_noise_radians_per_sqrt_second!=provider_.phase_noise_radians_per_sqrt_second)
            return false;
        if(!state_.reset(model_))return false;
        provider_=provider;mapping_known_=have_frequency_=current_qualified_=false;
        frequency_={};mapping_={};worker_funded_=true;return true;
    }
    // Optional work may yield without altering any established input/NCO/PCM
    // owner. This class has none of those resources and cannot imply END.
    void release_optional_work() noexcept {worker_funded_=false;current_qualified_=false;}
    void close_reference() noexcept {current_qualified_=false;}
    bool has_predictive_state()const noexcept {return configured_&&worker_funded_&&state_.has_predictive_state();}

    CurrentLinkResult observe(const Reference& reference,const LinkMapping& mapping,
            CurrentLinkProvider provider,std::uint64_t captured_end_sample,
            std::optional<double> fit_slew_enclosed_hz_per_second={}) noexcept {
        if(!configured_)return declined(CurrentLinkReason::unconfigured);
        if(!worker_funded_)return declined(CurrentLinkReason::optional_work_unfunded);
        if(mapping_known_ && !same_capture(mapping_,mapping))
            return declined(CurrentLinkReason::capture_changed);
        const bool transition=mapping_known_ && mapping_coefficients_changed(mapping_,mapping);
        const bool genuine=transition && genuine_mapping_transition(mapping_,mapping);
        if(!same_provider(provider) && (!genuine || !provider.identity ||
           provider.phase_noise_radians_per_sqrt_second!=provider_.phase_noise_radians_per_sqrt_second))
            return declined(CurrentLinkReason::provider_changed);
        if(transition && !genuine)
            return declined(CurrentLinkReason::mapping_change_unproved);
        const auto last=reference.frequency_sample?reference.frequency_sample:reference.sample;
        if(reference.sample>=captured_end_sample || last>reference.sample) {
            auto result=declined(CurrentLinkReason::direct_frequency_declined);
            result.link.reason=LinkReason::future_measurement;return result;
        }
        // A carried publication cannot renew the frequency evidence date or
        // count as another independent fit. current() revalidates a retained
        // record without feeding it back into the persistent state.
        if(have_frequency_ && last==frequency_.last_sample && !transition &&
           same_frequency(reference))return current(mapping,provider,captured_end_sample);
        if(have_frequency_ && last<=frequency_.last_sample)
            return declined(CurrentLinkReason::nonincreasing_fit);
        return observe_funded(reference,mapping,provider,captured_end_sample,fit_slew_enclosed_hz_per_second);
    }
    CurrentLinkResult current(const LinkMapping& mapping,CurrentLinkProvider provider,
            std::uint64_t captured_end_sample) noexcept {
        if(!configured_)return declined(CurrentLinkReason::unconfigured);
        if(!worker_funded_)return declined(CurrentLinkReason::optional_work_unfunded);
        if(!same_provider(provider))return declined(CurrentLinkReason::provider_changed);
        if(!have_frequency_ || !current_qualified_)
            return declined(CurrentLinkReason::reference_unavailable);
        if(!same_capture(mapping_,mapping))return declined(CurrentLinkReason::capture_changed);
        if(mapping_coefficients_changed(mapping_,mapping))
            return declined(CurrentLinkReason::mapping_change_unproved);
        return current_funded(mapping,captured_end_sample);
    }
    // Explicitly separate future authority. Default presets leave evolution
    // UNKNOWN, so this returns declined. A real configured evolution model can
    // use the existing LinkState contract only after observe() received the
    // actual fit's matching within-aperture slew certificate.
    LinkResult predict(const LinkMapping& mapping,std::uint64_t at_sample,double maximum_horizon_seconds)const noexcept {
        if(!configured_ || !worker_funded_ || !current_qualified_)return {};
        return state_.predict(mapping,at_sample,maximum_horizon_seconds);
    }
    // Revalidate an ACTUAL raw ring record selected for this consumer's input
    // frontier. Different rolling profiles may project that same raw evidence
    // to another DATA audio frequency. This is not a physical RF retune, and an
    // older queued record neither replaces nor refreshes the latest record.
    CurrentLinkResult evaluate_current(const Reference& reference,const LinkMapping& mapping,
            CurrentLinkProvider provider,std::uint64_t captured_end_sample,
            std::size_t receiver_control_grant)noexcept {
        if(!configured_)return declined(CurrentLinkReason::unconfigured);
        if(!worker_funded_ || receiver_control_grant<receiver_control_scratch_allowance)
            return declined(CurrentLinkReason::optional_work_unfunded);
        if(!same_provider(provider))return declined(CurrentLinkReason::provider_changed);
        if(mapping_known_ && !same_capture(mapping_,mapping))return declined(CurrentLinkReason::capture_changed);
        // This entry accepts no RF-change authority. Genuine retunes must use
        // observe() with their actual settled mapping before later queries.
        auto same=mapping;same.data_audio_hz=mapping_.data_audio_hz;
        if(mapping_known_ && mapping_coefficients_changed(mapping_,same))
            return declined(CurrentLinkReason::mapping_change_unproved);
        return evaluate_current_funded(reference,mapping,captured_end_sample);
    }

private:
    struct FrequencyRecord {
        double hz=0,uncertainty_hz=0,process_uncertainty_hz=0,diffusion_gain=0;
        std::uint64_t publication_sample=0,first_sample=0,last_sample=0;
        std::uint32_t rate=0;
        FrequencyFit fit=FrequencyFit::unknown;
        Reference reference()const noexcept {
            Reference r;r.sample=publication_sample;r.sample_rate=rate;
            r.frequency_hz=hz;r.frequency_uncertainty_hz=uncertainty_hz;
            r.frequency_process_uncertainty_hz=process_uncertainty_hz;
            r.frequency_phase_diffusion_gain=diffusion_gain;
            r.frequency_first_sample=first_sample;r.frequency_sample=last_sample;
            r.frequency_fit=fit;r.frequency_valid=true;return r;
        }
    };
    bool same_provider(CurrentLinkProvider other)const noexcept {
        // The owner establishes this binding when it constructs/resets the
        // actual raw estimator. A local publication cannot choose its sigma.
        return other.identity==provider_.identity &&
            other.phase_noise_radians_per_sqrt_second==provider_.phase_noise_radians_per_sqrt_second;
    }
    bool same_frequency(const Reference& r)const noexcept {
        return r.frequency_valid && r.sample_rate==frequency_.rate &&
            r.frequency_first_sample==frequency_.first_sample && r.frequency_fit==frequency_.fit &&
            r.frequency_hz==frequency_.hz && r.frequency_uncertainty_hz==frequency_.uncertainty_hz &&
            r.frequency_process_uncertainty_hz==frequency_.process_uncertainty_hz &&
            r.frequency_phase_diffusion_gain==frequency_.diffusion_gain;
    }
    static bool same_capture(const LinkMapping& a,const LinkMapping& b)noexcept {
        return a.capture_generation==b.capture_generation && a.timebase_identity==b.timebase_identity &&
            a.sample_rate==b.sample_rate;
    }
    static bool mapping_coefficients_changed(const LinkMapping& a,const LinkMapping& b)noexcept {
        return a.actual_rf_shift_hz!=b.actual_rf_shift_hz || a.helper_audio_hz!=b.helper_audio_hz ||
            a.data_audio_hz!=b.data_audio_hz || a.sideband_sign!=b.sideband_sign ||
            a.mono_image_sign!=b.mono_image_sign || a.reference_identity_known!=b.reference_identity_known ||
            (a.reference_identity_known && a.reference_identity!=b.reference_identity) ||
            a.phase_segment!=b.phase_segment;
    }
    static bool genuine_mapping_transition(const LinkMapping& previous,const LinkMapping& next)noexcept {
        // Fixed configuration may extend an observed capture interval, but it
        // cannot manufacture RF retunes. A real producer must bound its settled
        // segment and give a new gauge; phase continuity remains independent.
        const bool actual=next.provenance==LinkMappingProvenance::confirmed_adapter ||
            next.provenance==LinkMappingProvenance::modelled_physical_capture;
        return actual && next.settled_first_sample>=previous.settled_end_sample &&
            next.settled_first_sample<next.settled_end_sample &&
            next.phase_segment!=previous.phase_segment;
    }
    CurrentLinkResult declined(CurrentLinkReason reason)noexcept {
        current_qualified_=false;CurrentLinkResult r;r.reason=reason;return r;
    }
    static CurrentLinkResult describe(const LinkResult& link,std::uint64_t captured_end,bool fresh)noexcept {
        CurrentLinkResult result;result.link=link;result.captured_end_sample=captured_end;
        result.reason=CurrentLinkReason::direct_frequency_declined;
        if(link.authority==LinkAuthority::declined)return result;
        const auto low=link.data_residual_hz.lower,high=link.data_residual_hz.upper;
        if(!std::isfinite(low)||!std::isfinite(high)||low>high) {
            result.reason=CurrentLinkReason::correction_unrepresentable;return result;
        }
        const auto middle=low*.5+high*.5;
        const auto radius=std::nextafter(std::max(middle-low,high-middle),
            std::numeric_limits<double>::infinity());
        if(!std::isfinite(middle)||!std::isfinite(radius)) {
            result.reason=CurrentLinkReason::correction_unrepresentable;return result;
        }
        result.correction_hz=-middle;result.correction_uncertainty_hz=radius;
        result.first_sample=link.evidence_first_sample;result.last_sample=link.evidence_last_sample;
        result.reason=CurrentLinkReason::accepted;result.available=true;result.new_independent_fit=fresh;
        return result;
    }
#if defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    CurrentLinkResult observe_funded(const Reference& r,const LinkMapping& m,CurrentLinkProvider provider,
            std::uint64_t end,std::optional<double> fit_slew)noexcept {
        const auto link=state_.observe({r,m,fit_slew},end);
        auto result=describe(link,end,true);
        if(!result.available) {current_qualified_=false;return result;}
        frequency_={r.frequency_hz,r.frequency_uncertainty_hz,r.frequency_process_uncertainty_hz,
            r.frequency_phase_diffusion_gain,r.sample,r.frequency_first_sample,
            r.frequency_sample?r.frequency_sample:r.sample,r.sample_rate,r.frequency_fit};
        mapping_=m;provider_=provider;mapping_known_=have_frequency_=current_qualified_=true;return result;
    }
#if defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    CurrentLinkResult current_funded(const LinkMapping& m,std::uint64_t end)noexcept {
        // Revalidate raw support, explicit current age and actual frontier.
        // Unknown evolution cannot reuse a cached record as latent precision.
        LinkState current;
        auto current_model=model_;
        current_model.rf_maximum_fractional_slew_per_second.reset();
        current_model.audio_maximum_fractional_slew_per_second.reset();
        if(!current.reset(current_model))return declined(CurrentLinkReason::unconfigured);
        const auto reference=frequency_.reference();
        const auto direct=current.observe({reference,m,{}},end);
        auto result=describe(direct,end,false);
        if(!result.available) {current_qualified_=false;return result;}
        // This proposal deliberately exports CURRENT measurement authority
        // only. Explicit evolution, if later supplied, still needs the fit's
        // within-aperture certificate and a separate prediction consumer.
        result.link.phase_bridge_allowed=false;
        return result;
    }
#if defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    CurrentLinkResult evaluate_current_funded(const Reference& reference,const LinkMapping& m,std::uint64_t end)noexcept {
        // Same bounded stack LinkState calculation as the former fresh caller;
        // no per-query heap and no new thermal history/precision inference.
        LinkState current;auto model=model_;
        model.rf_maximum_fractional_slew_per_second.reset();
        model.audio_maximum_fractional_slew_per_second.reset();
        if(!current.reset(model))return declined(CurrentLinkReason::unconfigured);
        const auto link=current.observe({reference,m,{}},end);
        const auto last=reference.frequency_sample?reference.frequency_sample:reference.sample;
        const bool fresh=!have_frequency_ || last>frequency_.last_sample;
        auto result=describe(link,end,fresh);
        if(!result.available) {current_qualified_=false;return result;}
        result.link.phase_bridge_allowed=false;
        if(fresh) {
            frequency_={reference.frequency_hz,reference.frequency_uncertainty_hz,
                reference.frequency_process_uncertainty_hz,reference.frequency_phase_diffusion_gain,
                reference.sample,reference.frequency_first_sample,last,reference.sample_rate,reference.frequency_fit};
            mapping_=m;mapping_known_=have_frequency_=current_qualified_=true;
        }
        else if(last==frequency_.last_sample && same_frequency(reference))current_qualified_=true;
        return result;
    }
    LinkState state_;
    LinkModel model_;
    LinkMapping mapping_;
    FrequencyRecord frequency_;
    CurrentLinkProvider provider_;
    bool configured_=false,worker_funded_=false,mapping_known_=false;
    bool have_frequency_=false,current_qualified_=false;
};
static_assert(sizeof(CurrentLinkRuntime)<=CurrentLinkRuntime::retained_storage_bound);

} // namespace datapump::helper
