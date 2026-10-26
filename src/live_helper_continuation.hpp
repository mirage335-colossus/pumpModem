#pragma once
#include "live_helper_clock.hpp"
#include "live_helper_prior.hpp"
#include "helper_phase_chord.hpp"
#include <numbers>

namespace datapump::helper {
// Explicit experimental same-link assumption, refreshed from one independently
// measured raw publication each joined call. The configured Wiener process is
// retained. Unknown thermal changes / unrelated carriers are NOT certified.
// This is retrospective only: the entire mapped score support must predate the
// publication's original captured frontier, even if more PCM is captured now.
enum class ContinuationFrequencyModel {unknown,conditional_stationary_mean_configured_wiener};
enum class ContinuationFrequencyReason {
    accepted,invalid_domain,unsupported_basis,uncovered_support,unqualified_frequency,
    unknown_process_model,provider_process_mismatch,unsupported_catalogue,no_rf_reduction
};
struct ContinuationFrequencyRequest {
    modem::PatternContinuationScoredSupport scored;
    std::uint64_t bank_source_origin=0,fixed_filter_radius=0,input_captured_end_sample=0;
    int mono_image_sign=1;
    JointClockSourceRelation source_relation=JointClockSourceRelation::physical_audio_unsteered;
    bool ordinary_utc_steering_enabled=true;
    JointClockInputBasis input_basis=JointClockInputBasis::unknown;
    ContinuationFrequencyModel model=ContinuationFrequencyModel::unknown;
    // ACTUAL HelperWorkspace observer configuration, never simulation truth.
    // The caller binds this witness to the same provider/domain as snapshot.
    double provider_phase_diffusion_radians_per_sqrt_second=0;
};
struct ContinuationFrequencyCertificateResult {
    ContinuationFrequencyReason reason=ContinuationFrequencyReason::invalid_domain;
    live::detail::LiveHelperClockMapReason mapping=live::detail::LiveHelperClockMapReason::unsupported_transport;
    LinkReason frequency=LinkReason::invalid_measurement;
    std::uint64_t raw_first=0,raw_last=0,frequency_observed_end=0;
    std::size_t selected_pairs=0,original_pairs=0,clock_rates=0;
    double provider_sigma=0,aperture_process_expansion_hz=0,frequency_radius_hz=0;
};
// Temporary separately paid setup scratch; no owner, pointer or selector is
// retained by the receiver. prior() is valid only until this builder changes.
class ContinuationFrequencyCertificate {
public:
    template<class RawFrequencyFrame>
    bool build(const modem::Config& config,const ContinuationFrequencyRequest& request,
            const RawFrequencyFrame& snapshot) noexcept {
        using namespace live::detail;
        ready_=false;result_={};support_=request.scored;
        const auto& domain=support_.input.domain;
        if(!punctuation_numeric::environment() || !config.oscillator_search || !domain.sample_rate ||
           domain.sample_rate!=config.sample_rate || !domain.symbol_samples || domain.original_pairs.empty() ||
           domain.original_pairs.size()>mask_.size() || !snapshot.capture_known || !snapshot.frequency_known ||
           snapshot.domain.input_view || !snapshot.domain.provider_identity || !snapshot.domain.rf_gauge ||
           !snapshot.domain.capture_generation || !snapshot.domain.sample_rate ||
           !domain.legacy_frequency_mask.empty() || domain.legacy_mask_id || domain.frequency_origin_hz!=0 ||
           std::signbit(domain.frequency_origin_hz) || domain.input_phase_view_id ||
           !std::isfinite(domain.data_clock_half_width_ppm) || domain.data_clock_half_width_ppm<0 ||
           domain.data_clock_half_width_ppm>10000 || !std::isfinite(domain.clock_cell_half_width_ppm) ||
           domain.clock_cell_half_width_ppm<0 || domain.clock_cell_half_width_ppm>10000)return false;
        result_.reason=ContinuationFrequencyReason::unsupported_basis;
        if(request.input_basis!=JointClockInputBasis::unshifted_fixed_filter_and_nominal_resampler ||
           config.oscillator_search->reference!=modem::OscillatorReference::independent_audio ||
           !config.fake_hopping || !config.fake_hopping->defer_symbols || config.symbol_burst.enabled() ||
           (config.scheduled_hopping && config.scheduled_hopping->enabled()))return false;
        const auto& recipe=snapshot.transport_recipe;
        if(recipe.raw_rate!=snapshot.domain.sample_rate || recipe.logical_rate!=domain.sample_rate)return false;
        using Map=LiveHelperClockCaptureMap;using Basis=LiveHelperClockTransportBasis;
        std::optional<Map> map;
        if(recipe.kind==audio::CaptureTransportKind::fixed_nominal_audio)
            map=Map::nominal_audio(Basis::fixed_nominal_unshifted,snapshot.domain,recipe.logical_rate,recipe.raw_zero,recipe.logical_zero);
        else if(recipe.kind==audio::CaptureTransportKind::sampled_physical_halfband)
            map=Map::simulation_halfband(Basis::fixed_nominal_unshifted,snapshot.domain,recipe.logical_rate,recipe.raw_zero,recipe.logical_zero);
        if(!map)return false;
        result_.reason=ContinuationFrequencyReason::uncovered_support;
        const auto raw=map->expand(support_.input,request.bank_source_origin,request.fixed_filter_radius,
            snapshot.captured_first_sample,snapshot.captured_end_sample);
        result_.mapping=raw.reason;if(!raw)return false;
        result_.raw_first=raw.first_sample;result_.raw_last=raw.last_sample;
        const auto frontier=snapshot.frequency_observation_frontier.value_or(snapshot.captured_end_sample);
        result_.frequency_observed_end=frontier;
        if(!frontier || frontier>snapshot.captured_end_sample || raw.last_sample>=frontier ||
           support_.input.last_sample>=request.input_captured_end_sample ||
           support_.input.consumed_end_sample>request.input_captured_end_sample)return false;
        result_.reason=ContinuationFrequencyReason::unknown_process_model;
        if(request.model!=ContinuationFrequencyModel::conditional_stationary_mean_configured_wiener)return false;
        result_.reason=ContinuationFrequencyReason::provider_process_mismatch;
        double sigma=0;
        try {sigma=modem::oscillator_effects(config).phase_noise_degrees_per_sqrt_second*std::numbers::pi/180;}
        catch(...){return false;}
        result_.provider_sigma=sigma;
        const auto& reference=snapshot.frequency;
        if(!std::isfinite(sigma) || sigma<0 || request.provider_phase_diffusion_radians_per_sqrt_second!=sigma ||
           !std::isfinite(reference.frequency_phase_diffusion_gain) || reference.frequency_phase_diffusion_gain<0 ||
           !std::isfinite(reference.frequency_process_uncertainty_hz) || reference.frequency_process_uncertainty_hz<0 ||
           reference.frequency_process_uncertainty_hz>reference.frequency_uncertainty_hz ||
           (sigma>0 && !(reference.frequency_process_uncertainty_hz>0)) ||
           (reference.frequency_fit!=FrequencyFit::local_polynomial && reference.frequency_fit!=FrequencyFit::conditioned_polynomial))return false;
        const LinkMapping* actual_mapping=nullptr;
        if constexpr(requires {snapshot.actual_mapping;}) {
            actual_mapping=snapshot.actual_mapping;if(!actual_mapping)return false;
        }
        const auto measured=live_helper_raw_frequency_input(config,snapshot,request.mono_image_sign,
            request.source_relation,domain.data_clock_half_width_ppm,raw.last_sample,actual_mapping);
        result_.frequency=measured.measured.reason;result_.reason=ContinuationFrequencyReason::unqualified_frequency;
        if(!measured.valid)return false;
        const auto fit_last=reference.frequency_sample?reference.frequency_sample:reference.sample;
        const auto first=std::min(raw.first_sample,reference.frequency_first_sample),last=std::max(raw.last_sample,fit_last);
        if(first>last || last>=frontier)return false;
        // Keep the raw-fit process allowance already inside its CI. Add a
        // conservative full-aperture Brownian endpoint-mean allowance, not an
        // accuracy-ppm/time drift estimate. For any full symbol endpoints in
        // this hull, |W(t2)-W(t1)| <= 2*z*sigma*sqrt(hull duration) on the same
        // explicit per-capture dated-pair event used by PhaseChord. Divide by
        // the SHORTEST original full-symbol duration, preserving all LF cells.
        // The stationary carrier mean and unmodelled thermal evolution remain
        // explicit conditional assumptions, not proof of instantaneous CFO.
        const long double seconds=static_cast<long double>(last-first+1)/snapshot.domain.sample_rate;
        // Explicit catalogue centres can be conservative extras outside the
        // declared physical box. They remain numerically scored: include their
        // complete cell rates rather than silently using a longer nominal Tmin.
        long double maximum_clock=domain.data_clock_half_width_ppm;
        for(const auto& pair:domain.original_pairs) {
            if(!std::isfinite(pair.clock_error_ppm) || std::abs(pair.clock_error_ppm)>10000)return false;
            maximum_clock=std::max(maximum_clock,static_cast<long double>(pair.clock_error_ppm)+domain.clock_cell_half_width_ppm);
        }
        const long double minimum_symbol=static_cast<long double>(domain.symbol_samples)/domain.sample_rate/
            (1+maximum_clock/1e6L);
        // Maximum over every integer/half-date endpoint pair in this
        // hull; using (first,last) alone underweights pairs near the end.
        const auto z=phase_chord_detail::pair_standard_deviations(last,last);
        const auto expanded=2*static_cast<long double>(z)*sigma*std::sqrt(seconds)/
            (2*std::numbers::pi_v<long double>*minimum_symbol);
        const auto expansion=expanded==0?0:std::nextafter(static_cast<double>(expanded),INFINITY);
        if(!std::isfinite(expansion) || expansion<0)return false;
        result_.aperture_process_expansion_hz=expansion;
        auto radius=helper_prior_detail::sum(measured.uncertainty_hz,expansion).upper;
        if(request.source_relation==JointClockSourceRelation::physical_audio_unsteered) {
            const auto utc=helper_utc_frequency_allowance(config,request.ordinary_utc_steering_enabled,snapshot.expected_helper_hz);
            if(!utc.valid)return false;radius=helper_prior_detail::sum(radius,utc.radius_hz).upper;
        } else if(request.source_relation!=JointClockSourceRelation::common_corrected_robust_source)return false;
        result_.frequency_radius_hz=radius;result_.reason=ContinuationFrequencyReason::unsupported_catalogue;
        const auto proof=helper_prior_mask(domain.original_pairs,measured.offset_hz,radius,
            config.carrier_hz-snapshot.expected_helper_hz,domain.frequency_cell_radius_hz,
            domain.clock_cell_half_width_ppm,domain.data_clock_half_width_ppm,false);
        if(!proof.admitted || proof.origin_hz!=0 || proof.represented_rates!=proof.rates)return false;
        result_.selected_pairs=proof.selected;result_.original_pairs=domain.original_pairs.size();result_.clock_rates=proof.rates;
        result_.reason=ContinuationFrequencyReason::no_rf_reduction;
        if(!proof.selected || proof.selected>=domain.original_pairs.size())return false;
        std::copy_n(proof.cells.begin(),domain.original_pairs.size(),mask_.begin());
        input_end_=request.input_captured_end_sample;raw_last_=raw.last_sample;reference_end_=frontier;
        ready_=true;result_.reason=ContinuationFrequencyReason::accepted;return true;
    }
    std::optional<modem::PatternContinuationFrequencyPrior> prior()const noexcept {
        if(!ready_)return {};
        return modem::PatternContinuationFrequencyPrior(support_,std::span(mask_).first(support_.input.domain.original_pairs.size()),
            input_end_,raw_last_,reference_end_);
    }
    const ContinuationFrequencyCertificateResult& result()const noexcept{return result_;}
private:
    std::array<std::uint8_t,modem::maximum_pattern_frequency_rate_hypotheses> mask_{};
    modem::PatternContinuationScoredSupport support_;
    ContinuationFrequencyCertificateResult result_;
    std::uint64_t input_end_=0,raw_last_=0,reference_end_=0;bool ready_=false;
};
} // namespace datapump::helper
