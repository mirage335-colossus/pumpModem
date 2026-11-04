#pragma once
#include "live_helper.hpp"
#include "datapump/capture_transport.hpp"
#include "joint_clock_cells.hpp"
#include <array>
#include <cstdint>
#include <limits>
#include <optional>

namespace datapump::live::detail {

// Raw capture owner. This is OPTIONAL storage, separately funded from the
// existing receiver/observer/cohort promises. Ingest every actual publication
// inside observe_helper's chunk loop, before logical rebasing clears marker
// provenance. It owns no PCM, payload, receiver or mutable phase convention.
struct LiveHelperClockSnapshot {
    helper::PunctuationClock clock;
    helper::PunctuationClockDomain domain;
    audio::CaptureTransportRecipe transport_recipe;
    HelperMeasurement frequency;
    std::uint64_t captured_first_sample=0,captured_end_sample=0;
    // An owned raw-frequency publication may be validated retrospectively at
    // its ORIGINAL exclusive observed-input end. This does not replace actual
    // capture, marker, phase or scored-support frontiers. Caller must bind the
    // exact provider/gauge/sample clock and actual retained frequency support.
    std::optional<std::uint64_t> frequency_observation_frontier;
    double expected_helper_hz=0;
    bool capture_known=false,frequency_known=false;
};
class LiveHelperClockHistory {
public:
    static constexpr std::size_t persistent_workspace_bound=2*1024;
    // Provider identity is caller-owned and MUST change on Receiver
    // reconstruction. A capture discontinuity or unknown actual RF retune
    // needs a fresh capture/gauge identity, even if nominal settings match.
    // No mismatch or delayed publication can reset this owner implicitly.
    void reset(helper::PunctuationClockDomain domain,double expected_helper_hz) noexcept {
        state_={};state_.domain=domain;state_.expected_helper_hz=expected_helper_hz;
        state_.clock.reset(domain);last_publication_=0;publication_known_=false;discontinuous_=false;
    }
    // Actual contiguous raw PCM, [first,end). This is not a decoded/processed
    // frontier and cannot be supplied from a logical-rate sample count.
    bool capture(helper::PunctuationClockDomain domain,std::uint64_t first,std::uint64_t end) noexcept {
        if(domain!=state_.domain || !valid_domain(domain) || discontinuous_)return false;
        if(first>end || (state_.capture_known && first!=state_.captured_end_sample)) {
            state_.capture_known=false;state_.frequency_known=false;discontinuous_=true;return false;
        }
        if(!state_.capture_known)state_.captured_first_sample=first;
        state_.captured_end_sample=end;state_.capture_known=true;return true;
    }
    helper::PunctuationClockReason observe(const helper::Reference& reference,
            helper::PunctuationClockDomain domain) noexcept {
        using Reason=helper::PunctuationClockReason;
        if(!helper::punctuation_numeric::environment())return Reason::unsupported_numeric_environment;
        if(domain!=state_.domain)return Reason::domain_mismatch;
        if(!valid_domain(domain) || reference.sample_rate!=domain.sample_rate)return Reason::invalid_domain;
        if(!state_.capture_known || reference.sample<state_.captured_first_sample ||
           reference.sample>=state_.captured_end_sample)return Reason::future_marker;
        if(publication_known_ && reference.sample<last_publication_)return Reason::nonincreasing_marker;
        // Frequency is independently optional: no punctuation/phase rejection
        // below can invalidate an otherwise usable current raw tone record.
        const auto frequency_last=reference.frequency_sample?reference.frequency_sample:reference.sample;
        const auto previous_last=state_.frequency.frequency_sample?
            state_.frequency.frequency_sample:state_.frequency.sample;
        if(reference.frequency_valid && reference.frequency_fit!=helper::FrequencyFit::unknown &&
           reference.frequency_first_sample<=frequency_last && frequency_last<=reference.sample &&
           reference.frequency_first_sample>=state_.captured_first_sample &&
           frequency_last<state_.captured_end_sample &&
           (!state_.frequency_known || frequency_last>=previous_last)) {
            state_.frequency=HelperMeasurement(reference);state_.frequency_known=true;
        }
        last_publication_=reference.sample;publication_known_=true;
        if(reference.punctuation.known && reference.punctuation.first_sample<state_.captured_first_sample)
            return Reason::invalid_marker;
        return state_.clock.observe(reference,domain,state_.captured_end_sample);
    }
    // Copy this bounded numeric snapshot while holding helper_mutex, then
    // release the lock before any clock fit/certificate/numeric decoder work.
    // Destination storage belongs to the separately funded bank context.
    bool snapshot_to(LiveHelperClockSnapshot& destination)const noexcept {
        if(!state_.capture_known){destination={};return false;}
        destination=state_;return true;
    }
    std::size_t working_bytes()const noexcept{return sizeof(*this);}
private:
    static bool valid_domain(helper::PunctuationClockDomain d) noexcept {
        return d.capture_generation && d.rf_gauge && d.provider_identity && d.sample_rate;
    }
    LiveHelperClockSnapshot state_;
    std::uint64_t last_publication_=0;
    bool publication_known_=false,discontinuous_=false;
};
static_assert(sizeof(LiveHelperClockHistory)<=LiveHelperClockHistory::persistent_workspace_bound);

// A stack-only view of ONE immutable raw publication and its complete actual
// capture domain. Both the ordinary all-LF prerequisite and the joint builder
// use this same policy/observation. No phase mean or marker qualification can
// replace the raw frequency fit, and no current fit becomes a future lease.
struct LiveHelperClockFrequencyInput {
    helper::Reference reference;
    helper::LinkModel model;
    helper::LinkMapping mapping;
    helper::LinkResult measured;
    double offset_hz=0,uncertainty_hz=0;
    bool valid=false;
};
template<class RawFrequencyFrame>
inline LiveHelperClockFrequencyInput live_helper_raw_frequency_input(
        const modem::Config& config,const RawFrequencyFrame& snapshot,int mono_image_sign,
        helper::JointClockSourceRelation source_relation=helper::JointClockSourceRelation::physical_audio_unsteered,
        std::optional<double> original_data_clock_half_width_ppm={},
        std::optional<std::uint64_t> scored_raw_last_sample={},
        const helper::LinkMapping* actual_raw_mapping=nullptr) noexcept {
    LiveHelperClockFrequencyInput input;input.measured.reason=helper::LinkReason::invalid_mapping;
    if(!snapshot.capture_known || !snapshot.frequency_known ||
       !snapshot.domain.capture_generation || !snapshot.domain.rf_gauge || !snapshot.domain.provider_identity ||
       !snapshot.domain.sample_rate || snapshot.frequency.sample_rate!=snapshot.domain.sample_rate ||
       snapshot.captured_first_sample>=snapshot.captured_end_sample ||
       !std::isfinite(snapshot.expected_helper_hz))return input;
    const auto frequency_frontier=snapshot.frequency_observation_frontier.value_or(snapshot.captured_end_sample);
    if(!frequency_frontier || frequency_frontier>snapshot.captured_end_sample ||
       snapshot.frequency.sample>=frequency_frontier)return input;
    // An owned old current-only fit is not a thermal forecast. The native
    // caller must query the ACTUAL whole scored aperture and expand its full
    // FFT/refinement/FIR/resampler support, not substitute a PCM block end.
    if(snapshot.frequency_observation_frontier &&
       (!scored_raw_last_sample || *scored_raw_last_sample>=frequency_frontier))return input;
    input.measured.reason=helper::LinkReason::invalid_model;
    if(!config.oscillator_search || (mono_image_sign!=1 && mono_image_sign!=-1))return input;
    const auto& policy=*config.oscillator_search;
    input.model.topology=policy.reference==modem::OscillatorReference::shared_radio?
        helper::LinkTopology::shared_radio:helper::LinkTopology::independent_audio;
    const auto accuracy=[&](double ppm) {
        if(ppm==0)return 0.;
        const auto infinity=std::numeric_limits<double>::infinity();
        return std::nextafter(std::nextafter(ppm*policy.margin,infinity)*std::nextafter(1e-6,infinity),infinity);
    };
    input.model.rf_accuracy_fraction=accuracy(policy.rf.accuracy_ppm);
    input.model.audio_accuracy_fraction=accuracy(policy.lf.accuracy_ppm);
    if(source_relation==helper::JointClockSourceRelation::common_corrected_robust_source) {
        // ORIGINAL ordinary DATA domain already includes every permitted UTC
        // steering factor. A physical-a-only prior could reject real b beyond
        // that box; the full domain is immutable receiver geometry, not a
        // desired clock estimate or a decoded timestamp.
        if(policy.reference!=modem::OscillatorReference::independent_audio ||
           !original_data_clock_half_width_ppm || !std::isfinite(*original_data_clock_half_width_ppm) ||
           *original_data_clock_half_width_ppm<0 || *original_data_clock_half_width_ppm>10000)return input;
        input.model.audio_accuracy_fraction=*original_data_clock_half_width_ppm==0?0:
            std::nextafter(*original_data_clock_half_width_ppm/1e6,std::numeric_limits<double>::infinity());
    } else if(source_relation!=helper::JointClockSourceRelation::physical_audio_unsteered)return input;
    input.model.maximum_current_measurement_age_seconds=1;
    if(!std::isfinite(policy.margin) || policy.margin<0 ||
       !std::isfinite(policy.lf.accuracy_ppm) || policy.lf.accuracy_ppm<0 ||
       !std::isfinite(input.model.audio_accuracy_fraction) || input.model.audio_accuracy_fraction<0 ||
       input.model.audio_accuracy_fraction>.01)return input;
    auto& mapping=input.mapping;
    mapping.capture_generation=snapshot.domain.capture_generation;
    mapping.timebase_identity=snapshot.domain.input_view;mapping.phase_segment=snapshot.domain.rf_gauge;
    mapping.reference_identity_known=true;mapping.reference_identity=0;
    mapping.sample_rate=snapshot.domain.sample_rate;
    mapping.settled_first_sample=snapshot.captured_first_sample;mapping.settled_end_sample=snapshot.captured_end_sample;
    mapping.actual_rf_shift_hz=policy.rf_shift_hz;mapping.helper_audio_hz=snapshot.expected_helper_hz;
    mapping.data_audio_hz=config.carrier_hz;
    mapping.sideband_sign=policy.sideband==modem::OscillatorSideband::upper?1:-1;
    mapping.mono_image_sign=mono_image_sign;mapping.provenance=helper::LinkMappingProvenance::fixed_configuration;
    if(actual_raw_mapping) {
        const auto& actual=*actual_raw_mapping;
        if(actual.capture_generation!=snapshot.domain.capture_generation ||
           actual.timebase_identity!=snapshot.domain.provider_identity ||
           actual.phase_segment!=snapshot.domain.rf_gauge || actual.sample_rate!=snapshot.domain.sample_rate ||
           actual.settled_first_sample!=snapshot.captured_first_sample ||
           actual.settled_end_sample!=snapshot.captured_end_sample ||
           actual.actual_rf_shift_hz!=mapping.actual_rf_shift_hz || actual.helper_audio_hz!=mapping.helper_audio_hz ||
           actual.data_audio_hz!=mapping.data_audio_hz || actual.sideband_sign!=mapping.sideband_sign ||
           actual.mono_image_sign!=mono_image_sign ||
           actual.provenance!=helper::LinkMappingProvenance::fixed_configuration)return input;
        mapping=actual;
    }
    input.reference=snapshot.frequency.reference();
    helper::LinkState current;if(!current.reset(input.model))return input;
    input.measured=current.observe({input.reference,input.mapping,{}},frequency_frontier);
    const auto interval=input.measured.helper_residual_hz;
    if(input.measured.authority==helper::LinkAuthority::declined || !input.measured.ordinary_domain_valid ||
       !std::isfinite(interval.lower) || !std::isfinite(interval.upper) || interval.lower>interval.upper)return input;
    // Keep the same outward physical strip used by JointClockCellCertificate,
    // including LinkState's arithmetic enclosure; do not subtract H with a
    // second, slightly narrower rounding convention in the legacy mask.
    input.offset_hz=interval.lower/2+interval.upper/2;
    input.uncertainty_hz=std::max(
        modem::detail::clock_prior_detail::sum(input.offset_hz,-interval.lower).upper,
        modem::detail::clock_prior_detail::sum(interval.upper,-input.offset_hz).upper);
    input.valid=std::isfinite(input.offset_hz) && std::isfinite(input.uncertainty_hz) && input.uncertainty_hz>=0;
    return input;
}
inline LiveHelperClockFrequencyInput live_helper_clock_frequency_input(
        const modem::Config& config,const LiveHelperClockSnapshot& snapshot,int mono_image_sign,
        helper::JointClockSourceRelation source_relation=helper::JointClockSourceRelation::physical_audio_unsteered,
        std::optional<double> original_data_clock_half_width_ppm={},
        std::optional<std::uint64_t> scored_raw_last_sample={}) noexcept {
    return live_helper_raw_frequency_input(config,snapshot,mono_image_sign,source_relation,
        original_data_clock_half_width_ppm,scored_raw_last_sample);
}


enum class LiveHelperClockTransportBasis {unknown,fixed_nominal_unshifted};
enum class LiveHelperClockMapReason {
    accepted,unsupported_transport,domain_mismatch,invalid_support,coordinate_overflow,
    unavailable_prefix,unavailable_suffix
};
struct LiveHelperClockRawSupport {
    LiveHelperClockMapReason reason=LiveHelperClockMapReason::unsupported_transport;
    std::uint64_t first_sample=0,last_sample=0;
    explicit operator bool()const noexcept{return reason==LiveHelperClockMapReason::accepted;}
};

// Immutable source-support mapping, not a clock estimate. Factories mirror
// the ACTUAL nominal transport implementation; merely equal sample rates do
// not certify an adjustable/time-warped/NCO/analytic-translated capture.
// Origins identify sample zero of the transport, not a transmission timestamp.
class LiveHelperClockCaptureMap {
public:
    static std::optional<LiveHelperClockCaptureMap> nominal_audio(
            LiveHelperClockTransportBasis basis,helper::PunctuationClockDomain raw_domain,
            std::uint32_t logical_rate,std::uint64_t raw_origin,std::uint64_t logical_origin) noexcept {
        if(basis!=LiveHelperClockTransportBasis::fixed_nominal_unshifted ||
           !valid_rates(raw_domain.sample_rate,logical_rate) || !valid_domain(raw_domain))return {};
        LiveHelperClockCaptureMap result;result.domain_=raw_domain;result.logical_rate_=logical_rate;
        result.raw_origin_=raw_origin;result.logical_origin_=logical_origin;
        if(!result.append_audio(raw_domain.sample_rate,logical_rate))return {};
        result.known_=true;return result;
    }
    static std::optional<LiveHelperClockCaptureMap> simulation_halfband(
            LiveHelperClockTransportBasis basis,helper::PunctuationClockDomain raw_domain,
            std::uint32_t logical_rate,std::uint64_t raw_origin=0,std::uint64_t logical_origin=0) noexcept {
        if(basis!=LiveHelperClockTransportBasis::fixed_nominal_unshifted ||
           !valid_rates(raw_domain.sample_rate,logical_rate) || !valid_domain(raw_domain) ||
           raw_domain.sample_rate%logical_rate)return {};
        const auto factor=raw_domain.sample_rate/logical_rate;
        if(factor!=4 && factor!=8 && factor!=16 && factor!=32)return {};
        LiveHelperClockCaptureMap result;result.domain_=raw_domain;result.logical_rate_=logical_rate;
        result.raw_origin_=raw_origin;result.logical_origin_=logical_origin;
        result.simulation_factor_=factor;result.known_=true;return result;
    }
    helper::PunctuationClockDomain domain()const noexcept{return domain_;}
    std::uint32_t logical_rate()const noexcept{return logical_rate_;}
    // Upper adjoint of the ORIGINAL unfiltered ingested logical prefix.
    // logical_end is exclusive and in the transport's logical clock. Negative
    // startup FIR history is actual zero extension, so this right-only query
    // deliberately imposes no left-support/marker qualification. It certifies
    // no phase, clock or future sample and adds no downstream causal FIR twice.
    std::optional<std::uint64_t> right_adjoint_last(std::uint64_t logical_end)const noexcept {
        if(!known_ || logical_end<=logical_origin_)return {};
        auto high=logical_end-1-logical_origin_;
        if(simulation_factor_) {
            if(!multiply(high,simulation_factor_,high) ||
               !add(high,std::uint64_t{128}*(simulation_factor_-1),high))return {};
        } else for(std::size_t i=stage_count_;i>0;--i) {
            const auto& stage=stages_[i-1];
            if(!floor_ratio(high,stage.input_rate,stage.output_rate,high) ||
               !add(high,stage.radius,high))return {};
        }
        if(!add(high,raw_origin_,high))return {};
        return high;
    }
    // scored is receiver-local. input_sample_origin is already the physical
    // CENTER coordinate in the bank's logical clock (PayloadFilter output0
    // has center=filter.delay()). Do not add that filter delay twice.
    // bank_source_origin translates bank-local logical time to transport time.
    // Expand the actual fixed filter's entire symmetric radius before mapping
    // through every resampler stage. No support is clipped to captured PCM.
    LiveHelperClockRawSupport expand(const modem::PatternAcquisitionScoredSupport& scored,
            std::uint64_t bank_source_origin,std::uint64_t fixed_filter_radius,
            std::uint64_t raw_captured_first,std::uint64_t raw_captured_end)const noexcept {
        using Reason=LiveHelperClockMapReason;
        LiveHelperClockRawSupport result;
        if(!known_ || scored.domain.sample_rate!=logical_rate_)return result;
        result.reason=Reason::invalid_support;
        if(scored.first_sample>scored.last_sample || raw_captured_first>raw_captured_end)return result;
        std::uint64_t low=0,high=0,origin=0;
        result.reason=Reason::coordinate_overflow;
        if(!add(scored.domain.input_sample_origin,bank_source_origin,origin) ||
           !add(origin,scored.first_sample,low) || !add(origin,scored.last_sample,high) ||
           !add(high,fixed_filter_radius,high))return result;
        result.reason=Reason::unavailable_prefix;
        if(low<fixed_filter_radius)return result;
        low-=fixed_filter_radius;
        if(low<logical_origin_ || high<logical_origin_)return result;
        low-=logical_origin_;high-=logical_origin_;
        if(simulation_factor_) {
            const auto radius=std::uint64_t{128}*(simulation_factor_-1);
            // Decimator m uses [Fm-256(F-1),Fm]. prime() discards
            // D=128(F-1)/F outputs, so exposed logical n is centered at Fn,
            // with EXACT physical support [Fn-128(F-1),Fn+128(F-1)].
            result.reason=Reason::coordinate_overflow;
            if(!multiply(low,simulation_factor_,low) || !multiply(high,simulation_factor_,high) ||
               !add(high,radius,high))return result;
            result.reason=Reason::unavailable_prefix;
            if(low<radius)return result;
            low-=radius;
        } else for(std::size_t i=stage_count_;i>0;--i) {
            const auto& stage=stages_[i-1];
            std::uint64_t first=0,last=0;
            result.reason=Reason::coordinate_overflow;
            if(!floor_ratio(low,stage.input_rate,stage.output_rate,first) ||
               !floor_ratio(high,stage.input_rate,stage.output_rate,last) ||
               !add(last,stage.radius,last))return result;
            result.reason=Reason::unavailable_prefix;
            if(first<stage.radius)return result;
            low=first-stage.radius;high=last;
        }
        result.reason=Reason::coordinate_overflow;
        if(!add(low,raw_origin_,low) || !add(high,raw_origin_,high))return result;
        result.reason=Reason::unavailable_prefix;
        if(low<raw_captured_first)return result;
        result.reason=Reason::unavailable_suffix;
        if(high>=raw_captured_end)return result;
        return {Reason::accepted,low,high};
    }
private:
    struct Stage {std::uint32_t input_rate=0,output_rate=0,radius=0;};
    static bool valid_domain(helper::PunctuationClockDomain d) noexcept {
        return d.capture_generation && d.rf_gauge && d.provider_identity && d.sample_rate;
    }
    static bool valid_rates(std::uint32_t input,std::uint32_t output) noexcept {
        return input>=64 && input<=120000000 && output>=64 && output<=120000000;
    }
    static bool add(std::uint64_t a,std::uint64_t b,std::uint64_t& out) noexcept {
        if(a>UINT64_MAX-b)return false;out=a+b;return true;
    }
    static bool multiply(std::uint64_t a,std::uint64_t b,std::uint64_t& out) noexcept {
        if(b && a>UINT64_MAX/b)return false;out=a*b;return true;
    }
    static bool floor_ratio(std::uint64_t n,std::uint32_t numerator,std::uint32_t denominator,
            std::uint64_t& out) noexcept {
        if(!denominator)return false;
        std::uint64_t whole=0;
        if(!multiply(n/denominator,numerator,whole))return false;
        // Rates <=120MHz make this remainder product strictly <2^64.
        const auto tail=(n%denominator)*static_cast<std::uint64_t>(numerator)/denominator;
        return add(whole,tail,out);
    }
    bool append_audio(std::uint32_t input,std::uint32_t output) noexcept {
        if(input==output)return true; // ACTUAL nonadjustable passthrough.
        if(static_cast<std::uint64_t>(input)>static_cast<std::uint64_t>(output)*4) {
            auto middle=output;
            while(static_cast<std::uint64_t>(middle)*4<input)middle*=4;
            return append_audio(input,middle) && append_audio(middle,output);
        }
        if(stage_count_==stages_.size())return false;
        // Actual radius=ceil(48/min(1,double(out)/in)). An extra integer
        // sample encloses binary64 division/ceil rounding for downsampling.
        // Keep exact 48 for upsampling; every stage's ratio is <=4:1.
        const auto radius=input>output?static_cast<std::uint32_t>(
            (std::uint64_t{48}*input+output-1)/output+1):48U;
        stages_[stage_count_++]={input,output,radius};return true;
    }
    helper::PunctuationClockDomain domain_;
    std::array<Stage,12> stages_{};
    std::uint64_t raw_origin_=0,logical_origin_=0;
    std::uint32_t logical_rate_=0,simulation_factor_=0;
    std::size_t stage_count_=0;
    bool known_=false;
};

struct LiveHelperClockRequest {
    modem::PatternAcquisitionScoredSupport scored;
    std::uint64_t bank_source_origin=0,fixed_filter_radius=0;
    // Row-local end of ACTUALLY captured filtered PCM, including an available
    // current block, not the core consumed frontier and not a desired end.
    std::uint64_t input_captured_end_sample=0,maximum_marker_age_samples=0;
    helper::PunctuationClockModel clock_model=helper::PunctuationClockModel::unknown;
    std::optional<double> declared_rate_slew_ppm_per_second;
    bool ordinary_utc_steering_enabled=true;
    // Both mono images remain ordinary alternatives until this mapping is
    // actually settled. Zero declines this single-branch optional certificate.
    int mono_image_sign=0;
    helper::JointClockSourceRelation source_relation=helper::JointClockSourceRelation::physical_audio_unsteered;
};
enum class LiveHelperClockReason {
    accepted,unsupported_numeric_environment,invalid_snapshot,unsupported_input_view,
    unavailable_scored_support,unqualified_audio_clock,invalid_frequency_policy,joint_cells_declined
};
struct LiveHelperClockResult {
    LiveHelperClockReason reason=LiveHelperClockReason::invalid_snapshot;
    LiveHelperClockMapReason mapping=LiveHelperClockMapReason::unsupported_transport;
    helper::PunctuationClockReason clock=helper::PunctuationClockReason::awaiting_observations;
    helper::JointClockCellResult cells;
    std::uint64_t raw_first_sample=0,raw_last_sample=0,raw_captured_end_sample=0;
};

// ONE decoder-bank owner reused sequentially for ready native rows. Borrowed
// prior() is valid only until the next prepare()/snapshot write/destruction;
// the joined numeric call must finish before any of those events. Never hold
// helper_mutex while preparing or scoring. No pointer escapes into a track,
// pending row or END decision. Clock failure leaves ordinary frequency work.
class LiveHelperClockContext {
public:
    static constexpr std::size_t persistent_workspace_bound=16*1024;
    static constexpr std::size_t separately_funded_setup_stack=64*1024;
    LiveHelperClockContext()=default;
    LiveHelperClockContext(const LiveHelperClockContext&)=delete;
    LiveHelperClockContext& operator=(const LiveHelperClockContext&)=delete;
    // Fill from History::snapshot_to while helper_mutex is briefly locked.
    // ONE fresh snapshot is consumed by each prepare(), including declines.
    // A later batch cannot reuse an older captured frontier/frequency lease.
    LiveHelperClockSnapshot& snapshot_target() noexcept {
        ready_=false;snapshot_={};snapshot_available_=true;return snapshot_;
    }
    const LiveHelperClockResult& result()const noexcept{return result_;}
    const helper::PunctuationClockResult& clock_result()const noexcept{return clock_;}
    std::optional<modem::PatternAcquisitionClockPrior> prior()const noexcept {
        return ready_?certificate_.prior():std::nullopt;
    }
    bool prepare(const modem::Config& config,const LiveHelperClockRequest& request) noexcept {
        const auto fresh_snapshot=snapshot_available_;snapshot_available_=false;
        ready_=false;result_={};clock_={};
        if(!fresh_snapshot)return false;
        result_.reason=LiveHelperClockReason::unsupported_input_view;
        const auto& recipe=snapshot_.transport_recipe;
        if(recipe.raw_rate!=snapshot_.domain.sample_rate || recipe.logical_rate!=config.sample_rate)return false;
        std::optional<LiveHelperClockCaptureMap> mapping;
        using Kind=audio::CaptureTransportKind;
        if(recipe.kind==Kind::fixed_nominal_audio) {
            // Hardware has no invented post-marker thermal horizon. Its
            // conditional affine model must be retrospectively bracketed.
            if(request.declared_rate_slew_ppm_per_second)return false;
            mapping=LiveHelperClockCaptureMap::nominal_audio(
                LiveHelperClockTransportBasis::fixed_nominal_unshifted,snapshot_.domain,
                recipe.logical_rate,recipe.raw_zero,recipe.logical_zero);
        } else if(recipe.kind==Kind::sampled_physical_halfband) {
            mapping=LiveHelperClockCaptureMap::simulation_halfband(
                LiveHelperClockTransportBasis::fixed_nominal_unshifted,snapshot_.domain,
                recipe.logical_rate,recipe.raw_zero,recipe.logical_zero);
        }
        if(!mapping)return false;
        return prepare_mapped(config,*mapping,request);
    }
    std::size_t working_bytes()const noexcept{return sizeof(*this);}
private:
    bool prepare_mapped(const modem::Config& config,const LiveHelperClockCaptureMap& mapping,
            const LiveHelperClockRequest& request) noexcept {
        result_.reason=LiveHelperClockReason::unsupported_numeric_environment;
        if(!helper::punctuation_numeric::environment())return false;
        result_.reason=LiveHelperClockReason::invalid_snapshot;
        if(!snapshot_.capture_known || !snapshot_.frequency_known || mapping.domain()!=snapshot_.domain ||
           mapping.logical_rate()!=config.sample_rate || !std::isfinite(snapshot_.expected_helper_hz) ||
           snapshot_.captured_first_sample>=snapshot_.captured_end_sample)return false;
        result_.reason=LiveHelperClockReason::unsupported_input_view;
        if(request.scored.domain.input_phase_view_id || request.scored.domain.frequency_origin_hz!=0)return false;
        result_.reason=LiveHelperClockReason::unavailable_scored_support;
        if(request.scored.last_sample>=request.input_captured_end_sample ||
           request.scored.consumed_end_sample>request.input_captured_end_sample)return false;
        const auto raw=mapping.expand(request.scored,request.bank_source_origin,request.fixed_filter_radius,
            snapshot_.captured_first_sample,snapshot_.captured_end_sample);
        result_.mapping=raw.reason;
        if(!raw)return false;
        result_.raw_first_sample=raw.first_sample;result_.raw_last_sample=raw.last_sample;
        result_.raw_captured_end_sample=snapshot_.captured_end_sample;
        result_.reason=LiveHelperClockReason::invalid_frequency_policy;
        const auto frequency=live_helper_clock_frequency_input(config,snapshot_,request.mono_image_sign,
            request.source_relation,request.scored.domain.data_clock_half_width_ppm,raw.last_sample);
        if(!frequency.valid)return false;
        const auto& model=frequency.model;
        // Default: complete configured physical-a prior. Explicit common
        // source relation: complete ORIGINAL DATA b prior, including every UTC
        // factor before measurement. Neither is an inferred thermal model.
        const auto audio_ppm=model.audio_accuracy_fraction==0?0:
            std::nextafter(model.audio_accuracy_fraction*1e6,std::numeric_limits<double>::infinity());
        const helper::PunctuationClockRequest clock_request{raw.first_sample,raw.last_sample,
            snapshot_.captured_end_sample,request.maximum_marker_age_samples,request.clock_model,
            request.declared_rate_slew_ppm_per_second};
        clock_=snapshot_.clock.qualify({-audio_ppm,audio_ppm},clock_request);
        result_.clock=clock_.reason;result_.reason=LiveHelperClockReason::unqualified_audio_clock;
        if(!clock_.qualified_for_acquisition)return false;
        // Presets supply accuracy, never a thermal slew certificate. The
        // prerequisite and joint builder consume the SAME raw snapshot/model.
        const helper::JointClockCellRequest joint{request.scored,snapshot_.domain,
            raw.first_sample,raw.last_sample,snapshot_.captured_end_sample,
            request.input_captured_end_sample,request.ordinary_utc_steering_enabled,
            helper::JointClockInputBasis::unshifted_fixed_filter_and_nominal_resampler,request.source_relation,
            snapshot_.frequency_observation_frontier};
        result_.reason=LiveHelperClockReason::joint_cells_declined;
        ready_=certificate_.build(config,joint,clock_,
            {frequency.reference,frequency.mapping,{}},frequency.model,snapshot_.domain);
        result_.cells=certificate_.result();
        if(ready_)result_.reason=LiveHelperClockReason::accepted;
        return ready_;
    }
    LiveHelperClockSnapshot snapshot_;
    helper::PunctuationClockResult clock_;
    helper::JointClockCellCertificate certificate_;
    LiveHelperClockResult result_;
    bool ready_=false,snapshot_available_=false;
};
static_assert(sizeof(LiveHelperClockContext)<=LiveHelperClockContext::persistent_workspace_bound);

} // namespace datapump::live::detail
