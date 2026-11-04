#pragma once
#include "datapump/pattern_receiver.hpp"
#include "datapump/pattern_search.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cfenv>
#include <cfloat>
#include <limits>
#include <optional>
#include <utility>

namespace datapump::modem {
// The software producer composes helper and DATA at the same corrected
// Resampler source coordinate. A generic/external physical helper can instead
// precede DATA UTC steering; keep that distinct, conservative default.
enum class HelperReferenceClockRelation {physical_helper,common_corrected_source};

inline double helper_reference_rf_half_width(const Config& config,double uncertainty_hz,
        double helper_hz,double utc_rate_limit,
        HelperReferenceClockRelation relation=HelperReferenceClockRelation::physical_helper) {
    if(!std::isfinite(uncertainty_hz) || uncertainty_hz<0 || !std::isfinite(helper_hz) ||
       !std::isfinite(utc_rate_limit) || utc_rate_limit<0 || utc_rate_limit>.001)
        throw Error("helper reference requires finite RF/clock bounds");
    if(relation!=HelperReferenceClockRelation::physical_helper &&
       relation!=HelperReferenceClockRelation::common_corrected_source)
        throw Error("unknown helper source-clock relation");
    if(relation==HelperReferenceClockRelation::common_corrected_source || utc_rate_limit==0)return uncertainty_hz;
    if(!config.oscillator_search)
        throw Error("unknown helper source-clock relation");
    const auto& policy=*config.oscillator_search;
    const auto original_lf=policy.reference==OscillatorReference::shared_radio && policy.rf_shift_hz!=0?
        policy.rf.accuracy_ppm:policy.lf.accuracy_ppm;
    const auto infinity=std::numeric_limits<long double>::infinity();
    const auto up=[&](long double x){return std::nextafter(x,infinity);};
    const auto physical_lf=up(up(static_cast<long double>(policy.margin)*original_lf)*1e-6L);
    const auto width=up(static_cast<long double>(uncertainty_hz)+
        up(up(std::abs(static_cast<long double>(helper_hz))*utc_rate_limit)*up(1+physical_lf)));
    if(!std::isfinite(width) || width>std::numeric_limits<double>::max())
        throw Error("realtime helper UTC RF uncertainty exceeds numeric range");
    auto result=static_cast<double>(width);
    if(static_cast<long double>(result)<width)result=std::nextafter(result,std::numeric_limits<double>::infinity());
    return result;
}

// This scalar is the same UTC-expanded symmetric DATA domain used by
// oscillator_pattern_search: retain unknown c and all original a alternatives.
// It does not query/generate a catalogue on the raw/capture callback.
inline std::optional<PatternHelperClockInterval> helper_reference_data_clock_interval(
        const Config& config,PatternHelperClockInterval measured,double utc_rate_limit,
        HelperReferenceClockRelation relation=HelperReferenceClockRelation::physical_helper) noexcept {
    if(!config.oscillator_search || !measured.valid() || !std::isfinite(utc_rate_limit) ||
       utc_rate_limit<0 || utc_rate_limit>.001)return {};
    const auto& policy=*config.oscillator_search;
    const bool shared=policy.reference==OscillatorReference::shared_radio && policy.rf_shift_hz!=0;
    const auto accuracy=shared?policy.rf.accuracy_ppm:policy.lf.accuracy_ppm;
    const auto bound=accuracy*policy.margin;
    if(!std::isfinite(bound) || bound<0 || !std::isfinite(policy.margin) || policy.margin<1)return {};
    auto data_bound=bound;
    if(utc_rate_limit!=0) {
        // Match the original domain's deliberate outward LF-model rounding.
        const auto a=static_cast<long double>(policy.margin)*accuracy*1e-6L;
        const auto expanded=a+utc_rate_limit+a*utc_rate_limit;
        const auto ppm=expanded*1e6L/policy.margin;
        if(!std::isfinite(ppm) || ppm>10000)return {};
        const auto expanded_accuracy=std::nextafter(static_cast<double>(ppm),INFINITY);
        data_bound=static_cast<double>(static_cast<long double>(policy.margin)*expanded_accuracy);
    }
    if(relation==HelperReferenceClockRelation::common_corrected_source) {
        measured.lower_ppm=std::max(measured.lower_ppm,-data_bound);
        measured.upper_ppm=std::min(measured.upper_ppm,data_bound);
        return measured.valid()?std::optional{measured}:std::nullopt;
    }
    if(relation!=HelperReferenceClockRelation::physical_helper)return {};
    measured.lower_ppm=std::max(measured.lower_ppm,-bound);
    measured.upper_ppm=std::min(measured.upper_ppm,bound);
    if(!measured.valid())return {};
    if(utc_rate_limit==0)return measured;
    // Physical helper a alone leaves DATA steering c individually unknown.
    // Enclose b=a+c+a*c without cancellation through 1+a minus one.
    const auto endpoint=[&](double ppm,long double c,bool upper) {
        const auto direction=upper?INFINITY:-INFINITY;
        const auto toward=static_cast<long double>(direction);
        const auto a=std::nextafter(static_cast<long double>(ppm)/1e6L,toward);
        const auto sum=std::nextafter(a+c,toward),product=std::nextafter(a*c,toward);
        const auto value=std::nextafter(std::nextafter(sum+product,toward)*1e6L,toward);
        return std::nextafter(static_cast<double>(value),static_cast<double>(direction));
    };
    measured={std::max(-data_bound,endpoint(measured.lower_ppm,-utc_rate_limit,false)),
              std::min(data_bound,endpoint(measured.upper_ppm,utc_rate_limit,true))};
    return measured.valid()?std::optional{measured}:std::nullopt;
}
// A declared shared radio oscillator controls BOTH RF translation and the
// physical audio clock. On a no-steering source M=(s*LO+H)*a and DATA b=a.
// observed_helper_hz is the SIGNED physical image admitted by the caller's
// immutable input view: real tone detection alone does not identify that image.
// This is conditional frequency-derived clock evidence, not independent marker
// cadence, an epoch, phase, a drift rate or a future stationarity certificate.
// A separately steered/external shared source needs the full unknown c model;
// decline it here instead of silently substituting physical a for DATA b.
inline std::optional<PatternHelperClockInterval> helper_reference_shared_clock_interval(
        const Config& config,double observed_helper_hz,double helper_hz,double uncertainty_hz,
        double utc_rate_limit,HelperReferenceClockRelation relation=HelperReferenceClockRelation::physical_helper) noexcept {
#if defined(__FAST_MATH__) || (defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__>0) || FLT_EVAL_METHOD!=0
    return {};
#else
    if(!std::numeric_limits<double>::is_iec559 || std::fegetround()!=FE_TONEAREST ||
       !config.oscillator_search || !std::isfinite(observed_helper_hz) || !std::isfinite(helper_hz) ||
       helper_hz<=0 || !std::isfinite(uncertainty_hz) || uncertainty_hz<0 || utc_rate_limit!=0 ||
       (relation!=HelperReferenceClockRelation::physical_helper &&
        relation!=HelperReferenceClockRelation::common_corrected_source))return {};
    const auto& policy=*config.oscillator_search;
    if(policy.reference!=OscillatorReference::shared_radio || !std::isfinite(policy.rf_shift_hz) ||
       policy.rf_shift_hz<=0 || !std::isfinite(policy.margin) || policy.margin<1 ||
       !std::isfinite(policy.rf.accuracy_ppm) || policy.rf.accuracy_ppm<0 || policy.rf.accuracy_ppm>10000 ||
       (policy.sideband!=OscillatorSideband::upper && policy.sideband!=OscillatorSideband::lower))return {};
    const auto down=[](long double x){return std::nextafter(x,-std::numeric_limits<long double>::infinity());};
    const auto up=[](long double x){return std::nextafter(x,std::numeric_limits<long double>::infinity());};
    const auto shift=policy.sideband==OscillatorSideband::upper?
        static_cast<long double>(policy.rf_shift_hz):-static_cast<long double>(policy.rf_shift_hz);
    const auto denominator=shift+static_cast<long double>(helper_hz);
    const auto dl=down(denominator),du=up(denominator);
    if(!std::isfinite(dl) || !std::isfinite(du) || (dl<=0 && du>=0))return {};
    const auto nl=down(down(static_cast<long double>(observed_helper_hz)-helper_hz)-uncertainty_hz);
    const auto nu=up(up(static_cast<long double>(observed_helper_hz)-helper_hz)+uncertainty_hz);
    const std::array quotients{nl/dl,nl/du,nu/dl,nu/du};
    const auto low=down(down(*std::min_element(quotients.begin(),quotients.end()))*1e6L);
    const auto high=up(up(*std::max_element(quotients.begin(),quotients.end()))*1e6L);
    const auto bound=static_cast<double>(static_cast<long double>(policy.margin)*policy.rf.accuracy_ppm);
    if(!std::isfinite(low) || !std::isfinite(high) || !std::isfinite(bound))return {};
    auto lower=static_cast<double>(low),upper=static_cast<double>(high);
    if(static_cast<long double>(lower)>low)lower=std::nextafter(lower,-INFINITY);
    if(static_cast<long double>(upper)<high)upper=std::nextafter(upper,INFINITY);
    PatternHelperClockInterval result{std::max(-bound,lower),std::min(bound,upper)};
    return result.valid()?std::optional{result}:std::nullopt;
#endif
}
// Admission-time helper-side choice. A weak measured RF interval must not
// replace a tighter independent RF allowance. With a shared radio oscillator,
// the clock-correlated RF extent is the RF prior, not a zero independent term.
// LF and peer UTC uncertainty retain their full physical coverage. The default
// policy also preserves the original numerical representatives.
// The physical (pre-UTC) RF floor is conservative for frequency-only widening:
// declining a marginal helper gain is allowed; enlarging Robust work is not.
inline bool helper_reference_prefers_original_prior(const Config& config,double uncertainty_hz,
        double helper_hz,double utc_rate_limit,
        HelperReferenceClockRelation relation=HelperReferenceClockRelation::physical_helper,
        std::optional<double> measured_residual_hz={}) {
    if(!config.oscillator_search)return false;
    validate(config);
    const auto width=helper_reference_rf_half_width(config,uncertainty_hz,helper_hz,utc_rate_limit,relation);
    const auto& policy=*config.oscillator_search;
    const bool shared=policy.reference==OscillatorReference::shared_radio && policy.rf_shift_hz!=0;
    const auto lf=static_cast<long double>(policy.margin)*(shared?policy.rf.accuracy_ppm:policy.lf.accuracy_ppm)*1e-6L;
    const auto rf=shared?0.L:static_cast<long double>(policy.margin)*policy.rf_shift_hz*policy.rf.accuracy_ppm*1e-6L;
    const auto coupling=shared?static_cast<long double>(oscillator_effects(config).physical_rf_hz):config.carrier_hz;
    const auto allowance=shared?
        std::abs(coupling)*lf*config.frequency_search_multiplier:
        rf+(std::abs(coupling)*lf+rf)*(config.frequency_search_multiplier-1);
    if(static_cast<long double>(width)<allowance)return false;
    if(!measured_residual_hz)return true;
    // Width alone cannot preserve a prior whose LOCATION is contradicted by
    // the measured pilot. At admission only, enclose the full original joint
    // DATA frequency/clock domain in a rectangle and project M=F+(H-D)*B.
    // The enclosure deliberately ignores correlations: only definite
    // disjointness selects the existing translated basis. Touching or
    // numerically uncertain intervals keep the original basis.
    if(!std::isfinite(*measured_residual_hz) || !std::numeric_limits<double>::is_iec559 ||
       std::fegetround()!=FE_TONEAREST)return true;
    const auto infinity=std::numeric_limits<long double>::infinity();
    const auto up=[&](long double x){return std::nextafter(x,infinity);};
    const auto down=[&](long double x){return std::nextafter(x,-infinity);};
    const auto bound_up=[](long double x){return static_cast<long double>(
        std::nextafter(static_cast<double>(x),INFINITY));};
    // Same scalar physical/UTC expansion as oscillator_pattern_search, with
    // outward rounding. Do not allocate its potentially large catalogue just
    // to decide the input basis on the capture/admission path.
    auto accuracy=shared?policy.rf.accuracy_ppm:policy.lf.accuracy_ppm;
    if(utc_rate_limit!=0) {
        const auto expanded=lf+utc_rate_limit+lf*utc_rate_limit;
        const auto ppm=expanded*1e6L/policy.margin;
        if(!std::isfinite(ppm) || ppm>10000)return true;
        accuracy=std::nextafter(static_cast<double>(ppm),INFINITY);
    }
    const auto b=up(bound_up(static_cast<long double>(policy.margin)*accuracy)*1e-6L);
    const auto base=bound_up(up(std::abs(coupling)*b)+bound_up(rf));
    const auto f=bound_up(base+bound_up(base*(config.frequency_search_multiplier-1)));
    const auto separation=up(std::abs(static_cast<long double>(helper_hz)-config.carrier_hz));
    const auto half=up(f+up(separation*b));
    const auto low=down(static_cast<long double>(*measured_residual_hz)-width);
    const auto high=up(static_cast<long double>(*measured_residual_hz)+width);
    return !std::isfinite(half) || !(low>half || high<-half);
}
// Search coordinates AFTER the synchronous helper translation. This is not a
// waveform/configuration change or evidence that the helper is locked. Every
// physical LF lane and the supplied peer UTC-rate allowance remain admitted.
struct HelperReferenceSearch {
    OscillatorPatternSearch catalogue;
    double rf_half_width_hz=0,clock_frequency_hz=0,residual_center_hz=0;
    bool original_prior_retained=false;
};

inline HelperReferenceSearch helper_reference_search(const Config& config,
        double uncertainty_hz,double helper_hz,double residual_center_hz,
        double utc_rate_limit,
        HelperReferenceClockRelation relation=HelperReferenceClockRelation::physical_helper,
        std::optional<bool> preserve_original_prior={},
        PatternUtcClockGrid utc_grid=PatternUtcClockGrid::preserve_original_lanes) {
    if(!config.oscillator_search || !std::isfinite(uncertainty_hz) || uncertainty_hz<0 ||
       !std::isfinite(helper_hz) || !std::isfinite(residual_center_hz) ||
       !std::isfinite(utc_rate_limit) || utc_rate_limit<0)
        throw Error("realtime helper search requires finite RF/clock bounds");
    validate(config);
    auto corrected=config;
    auto& policy=*corrected.oscillator_search;
    const auto original_lf=policy.reference==OscillatorReference::shared_radio && policy.rf_shift_hz!=0?
        policy.rf.accuracy_ppm:policy.lf.accuracy_ppm;
    const auto rf_width=helper_reference_rf_half_width(config,uncertainty_hz,helper_hz,utc_rate_limit,relation);
    const bool preserve=preserve_original_prior?*preserve_original_prior:
        helper_reference_prefers_original_prior(config,uncertainty_hz,helper_hz,utc_rate_limit,relation,residual_center_hz);
    if(preserve) {
        HelperReferenceSearch result;
        result.catalogue=oscillator_pattern_search(config,utc_rate_limit,{},utc_grid);
        result.rf_half_width_hz=rf_width;result.clock_frequency_hz=config.carrier_hz-helper_hz;
        // q=0 input: the full original raw RF/LF prior remains. The measured helper
        // offset M only selects cells through F ~= M+(D-H)*b; it never shifts
        // those cells or replaces their original RF/LF/UTC coverage.
        result.residual_center_hz=residual_center_hz;result.original_prior_retained=true;
        return result;
    }
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
    result.catalogue=oscillator_pattern_search(corrected,utc_rate_limit,resolution,utc_grid);
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
