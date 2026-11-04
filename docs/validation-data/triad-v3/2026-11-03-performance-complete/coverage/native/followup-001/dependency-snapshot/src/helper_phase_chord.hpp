#pragma once
#include "datapump/helper_receiver.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cfloat>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP>=2)
#include <xmmintrin.h>
#endif

namespace datapump::helper {
namespace phase_chord_detail {
inline constexpr long double tau=2*std::numbers::pi_v<long double>;

inline bool interval_environment() noexcept {
#if defined(__FAST_MATH__) || (defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__>0) || (defined(_M_FP_FAST) && _M_FP_FAST) || FLT_EVAL_METHOD!=0
    return false;
#else
    if(!std::numeric_limits<double>::is_iec559 || std::numeric_limits<double>::radix!=2 ||
       std::numeric_limits<double>::digits!=53 || sizeof(double)!=sizeof(std::uint64_t) ||
       std::fegetround()!=FE_TONEAREST)return false;
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP>=2)
    if(_mm_getcsr()&((1U<<15)|(1U<<6)|(3U<<13)))return false;
#endif
    volatile double tiny=std::numeric_limits<double>::denorm_min(),one=1;
    volatile double normal=std::numeric_limits<double>::min(),half=.5;
    const double input=tiny*one,output=normal*half;
    return std::bit_cast<std::uint64_t>(input)==1 &&
        std::bit_cast<std::uint64_t>(output)==0x0008000000000000ULL;
#endif
}
inline double upward(double value) noexcept {
    return std::nextafter(value,std::numeric_limits<double>::infinity());
}
inline double downward(double value) noexcept {
    return std::nextafter(value,-std::numeric_limits<double>::infinity());
}
inline bool finite_nonnegative(double value) noexcept {return std::isfinite(value)&&value>=0;}

// One simultaneous event PER EXPLICIT CAPTURE GENERATION over all pairs of
// integer/half-sample dates, not arbitrary continuous-time Wiener excursions.
// The weights
// telescope to one, so data-dependent date/anchor selection needs no Gaussian
// independence assumption. Existing endpoint measurement intervals are a
// separate prerequisite, and retain their original statistical limitations.
// This is not an unconditional global confidence certificate for provider
// endpoints, unknown interference or repeated generations.
inline double pair_standard_deviations(std::uint64_t first,std::uint64_t last) noexcept {
    constexpr long double alpha=.0001L;
    const auto i=2*static_cast<long double>(first),j=2*static_cast<long double>(last);
    const auto log_inverse_weight=std::log(i+1)+std::log(i+2)+std::log(j+1)+std::log(j+2);
    return static_cast<double>(std::max(8.L,std::sqrt(2*(std::log(2/alpha)+log_inverse_weight))));
}

} // namespace phase_chord_detail

// These identities are supplied by the owner of the actual captured input.
// A raw input view may have id zero; capture generation and RF/gauge identity
// must be explicit and nonzero. A retune with an unknown phase reset needs a
// different RF/gauge identity, even when its nominal helper frequency repeats.
struct PhaseFrequencyDomain {
    std::uint64_t capture_generation=0,rf_gauge=0,input_view=0;
    std::uint32_t sample_rate=0;
    bool operator==(const PhaseFrequencyDomain&)const=default;
};
struct PhaseScoredSupport {std::uint64_t first_sample=0,last_sample=0;}; // inclusive, outward bounds
struct ConnectedPhaseQualification {
    PhaseFrequencyDomain domain;
    std::uint64_t view_id=0,first_sample=0,last_sample=0,evidence_sample=0;
    double maximum_phase_error_radians=0;
    bool stable_affine_tail=false;
};

// Optional, allocation-free endpoint predictor. It neither owns nor changes a
// receiver, payload, phase curve, sample clock, epoch or metadata. The caller
// retains independent ReferenceDynamics and provider qualification gates.
class PhaseChord {
public:
    struct Endpoint {std::uint64_t sample=0;double residual=0,uncertainty=0;};
    struct Prediction {
        struct FrequencyInterval {
            double center_hz=0,radius_hz=0,lower_hz=0,upper_hz=0;
            std::uint64_t first_sample=0,last_sample=0; // actual endpoint aperture
            std::uint32_t sample_rate=0;
        };
        std::uint64_t first_sample=0,last_sample=0;
        double phase=0,frequency_hz=0,past_frequency_error=0;
        double first_uncertainty=0,last_uncertainty=0,sigma=0;
        std::uint32_t rate=0;
        long double phase_at(std::uint64_t sample) const noexcept {
            if(!rate||sample<last_sample)return std::numeric_limits<long double>::infinity();
            // Reject past dates BEFORE unsigned subtraction. Subtract exact
            // integers before widening so platforms with double-sized long
            // double also retain small intervals near UINT64_MAX.
            const auto delta=static_cast<long double>(sample-last_sample);
            return static_cast<long double>(phase)+phase_chord_detail::tau*frequency_hz*
                delta/rate;
        }
        double uncertainty_at(std::uint64_t sample) const noexcept {
            if(!rate||sample<last_sample||first_sample>=last_sample)
                return std::numeric_limits<double>::infinity();
            const auto L=static_cast<long double>(last_sample-first_sample)/rate;
            const auto h=static_cast<long double>(sample-last_sample)/rate;
            const auto future=phase_chord_detail::pair_standard_deviations(last_sample,sample)*sigma*std::sqrt(h);
            const auto numeric=128*std::numeric_limits<double>::epsilon()*
                std::max({1.L,std::abs(static_cast<long double>(phase)),phase_chord_detail::tau*std::abs(frequency_hz)*(L+h)});
            const auto total=last_uncertainty+(first_uncertainty+last_uncertainty)*h/L+
                past_frequency_error*h+future+numeric;
            return std::nextafter(static_cast<double>(total),std::numeric_limits<double>::infinity());
        }

        // Static mean CFO under the SAME endpoint/capture/Wiener model as
        // this prediction. It is neither an instantaneous frequency bound nor
        // an E/T conversion of a phase-view error. Local fit frequency CI was
        // used to resolve cycles; adding it again would double-count it here.
        // Only PhaseChord::prediction() establishes the finite private rounding
        // allowance after actual qualified, uniquely unwrapped endpoints.
        std::optional<FrequencyInterval> mean_frequency_interval() const noexcept {
            using namespace phase_chord_detail;
            if(!interval_environment() || !rate || first_sample>=last_sample ||
               last_sample-first_sample<2ULL*rate/5 || !std::isfinite(phase) ||
               !std::isfinite(frequency_hz) || !finite_nonnegative(first_uncertainty) ||
               !finite_nonnegative(last_uncertainty) || !finite_nonnegative(past_frequency_error) ||
               !finite_nonnegative(sigma) || !finite_nonnegative(mean_roundoff_hz_))return {};
            // Exact integer subtraction BEFORE conversion, then lower-bound L.
            const auto duration=std::nextafter(static_cast<double>(last_sample-first_sample),0.);
            const auto seconds=std::nextafter(duration/static_cast<double>(rate),0.);
            if(!(seconds>0) || !std::isfinite(seconds))return {};
            const auto endpoints=upward(first_uncertainty+last_uncertainty);
            const auto phase_rate=upward(upward(endpoints/seconds)+past_frequency_error);
            const auto tau_lower=std::nextafter(static_cast<double>(tau),0.);
            const auto radius=upward(upward(phase_rate/tau_lower)+mean_roundoff_hz_);
            const auto lower=downward(frequency_hz-radius);
            const auto upper=std::nextafter(frequency_hz+radius,std::numeric_limits<double>::infinity());
            if(!finite_nonnegative(radius) || !std::isfinite(lower) || !std::isfinite(upper))return {};
            return FrequencyInterval{frequency_hz,radius,lower,upper,first_sample,last_sample,rate};
        }

        // Pure, conservative first scope: the complete actual scoring support
        // (including LF/refinement/FIR/bin guards) lies in a qualified FINAL
        // affine tail. Curve.latest_expiry alone cannot create the supplied
        // connected certificate. The caller must independently retain its
        // qualification history and exact capture/RF/input-view provenance.
        // No state, phase weights, detector, clock lane, epoch or trial changes.
        template<class Curve>
        std::optional<FrequencyInterval> affine_residual_interval(const Curve& curve,
                const PhaseFrequencyDomain& prediction_domain,
                const ConnectedPhaseQualification& qualified,PhaseScoredSupport support,
                double expected_helper_hz) const noexcept {
            using namespace phase_chord_detail;
            const auto mean=mean_frequency_interval();
            if(!mean || !prediction_domain.capture_generation || !prediction_domain.rf_gauge ||
               prediction_domain.sample_rate!=rate || !(prediction_domain==qualified.domain) ||
               !qualified.view_id || qualified.view_id!=curve.view_id() || curve.sample_rate()!=rate ||
               !qualified.stable_affine_tail || support.first_sample>support.last_sample ||
               qualified.first_sample>qualified.last_sample || qualified.first_sample>support.first_sample ||
               support.last_sample>qualified.last_sample || qualified.evidence_sample!=last_sample ||
               qualified.evidence_sample>=qualified.first_sample || last_sample>=support.first_sample ||
               qualified.first_sample<curve.first_sample() ||
               qualified.last_sample>curve.qualified_until() || curve.latest_evidence()!=qualified.evidence_sample ||
               !finite_nonnegative(qualified.maximum_phase_error_radians) ||
               qualified.maximum_phase_error_radians>.25 || !std::isfinite(expected_helper_hz) ||
               curve.next_segment_after(support.first_sample))return {};
            const auto slope=curve.slope_at(support.first_sample);
            if(!std::isfinite(slope) || curve.slope_at(support.last_sample)!=slope)return {};
            // Enclose the known affine correction; its stored slope need not
            // come from this latest mean estimate. Blindly centering at B after
            // an unchanged-tail renewal would use the wrong RF coordinate.
            const auto product=slope*static_cast<double>(rate);
            if(!std::isfinite(product))return {};
            const auto product_low=downward(product),product_high=std::nextafter(product,
                std::numeric_limits<double>::infinity());
            const auto tau_low=std::nextafter(static_cast<double>(tau),0.);
            const auto tau_high=std::nextafter(static_cast<double>(tau),std::numeric_limits<double>::infinity());
            const auto shift_low=downward(std::min({product_low/tau_low,product_low/tau_high,
                product_high/tau_low,product_high/tau_high}));
            const auto shift_high=std::nextafter(std::max({product_low/tau_low,product_low/tau_high,
                product_high/tau_low,product_high/tau_high}),std::numeric_limits<double>::infinity());
            const auto lower=downward(downward(mean->lower_hz-expected_helper_hz)-shift_high);
            const auto upper=std::nextafter(std::nextafter(mean->upper_hz-expected_helper_hz,
                std::numeric_limits<double>::infinity())-shift_low,std::numeric_limits<double>::infinity());
            const auto center=(mean->center_hz-expected_helper_hz)-product/static_cast<double>(tau);
            const auto radius=upward(std::max(center-lower,upper-center));
            if(!std::isfinite(center) || !finite_nonnegative(radius) ||
               !std::isfinite(lower) || !std::isfinite(upper))return {};
            return FrequencyInterval{center,radius,lower,upper,first_sample,last_sample,rate};
        }
    private:
        friend class PhaseChord;
        double mean_roundoff_hz_=std::numeric_limits<double>::infinity();
    };

    bool configure(std::uint32_t rate,double sigma,double baseline) noexcept {
        if(rate_||!rate||!std::isfinite(sigma)||sigma<0||!std::isfinite(baseline))return false;
        rate_=rate;sigma_=sigma;baseline_=baseline;return true;
    }
    bool configured() const noexcept {return rate_!=0;}
    // Reset only for an explicit new capture generation/sample clock. Loss of
    // helper qualification must not reset an established external phase curve.
    // The date-pair confidence budget is per reset, never a global lifetime claim.
    bool reset(std::uint32_t rate,double sigma,double baseline) noexcept {
        *this=PhaseChord{};
        return configure(rate,sigma,baseline);
    }
    const char* observe(const Reference& r,std::uint64_t frontier) noexcept {
        if(!rate_||r.sample_rate!=rate_||r.sample>frontier||r.frequency_sample>r.sample||!r.frequency_valid||!r.phase_valid||
           !r.timing_valid||!r.phase_observation_known||r.phase_observation_sample!=r.sample||
           !std::isfinite(r.phase_radians)||!std::isfinite(r.frequency_hz)||
           !std::isfinite(r.phase_uncertainty_radians)||r.phase_uncertainty_radians<0||r.phase_uncertainty_radians>=.25||
           !std::isfinite(r.frequency_uncertainty_hz)||r.frequency_uncertainty_hz<0||
           !std::isfinite(r.dwell_start_seconds)||!std::isfinite(r.dwell_uncertainty_seconds)||
           r.dwell_uncertainty_seconds<0||r.dwell_uncertainty_seconds>.005)
            return "fresh canonical endpoint unavailable";
        if(have_previous_&&r.sample<=previous_.sample)return "endpoint already observed";
        if(!have_previous_) {
            residual_roundoff_=0;
            previous_=previous(r);current_={r.sample,r.phase_radians,r.phase_uncertainty_radians};
            have_previous_=true;history_[0]=current_;count_=1;position_=1;
            return "first canonical endpoint retained";
        }
        const auto L=static_cast<long double>(r.sample-previous_.sample)/rate_;
        const auto predicted=phase_chord_detail::tau*previous_.frequency_hz*L;
        const auto residual=std::remainder(static_cast<long double>(r.phase_radians)-previous_.phase-predicted,phase_chord_detail::tau);
        const auto numeric=128*std::numeric_limits<double>::epsilon()*std::max(1.L,std::abs(predicted));
        const auto allowance=previous_.phase_uncertainty+r.phase_uncertainty_radians+
            phase_chord_detail::tau*previous_.frequency_uncertainty_hz*L+
            sigma_*phase_chord_detail::pair_standard_deviations(previous_.sample,r.sample)*std::sqrt(L)+numeric;
        if(!std::isfinite(allowance)||allowance>=std::numbers::pi_v<long double>||std::abs(residual)>allowance) {
            // Only the optional phase estimator restarts. Its caller retains
            // every accepted payload owner and the existing affine curve.
            residual_roundoff_=0;
            previous_=previous(r);current_={r.sample,r.phase_radians,r.phase_uncertainty_radians};
            history_={};history_[0]=current_;count_=1;position_=1;
            return "endpoint cycle is unresolved; optional chord restarted";
        }
        const auto next=current_.residual+phase_chord_detail::tau*(previous_.frequency_hz-baseline_)*L+residual;
        if(!std::isfinite(next)||std::abs(next)>1e12L)return "unwrapped residual exceeds finite range";
        residual_roundoff_+=static_cast<double>(128*std::numeric_limits<double>::epsilon()*
            std::max({1.L,std::abs(static_cast<long double>(current_.residual)),std::abs(predicted),std::abs(next)}));
        current_={r.sample,static_cast<double>(next),r.phase_uncertainty_radians+residual_roundoff_};previous_=previous(r);
        std::uint64_t newest=0;
        for(std::size_t i=0;i<count_;++i)newest=std::max(newest,history_[i].sample);
        // Decimation bounds storage, never measurements or payload progress.
        // Keep the current endpoint separately, regardless of this cadence.
        if(r.sample-newest>=std::max<std::uint64_t>(1,rate_/10)) {
            history_[position_]=current_;position_=(position_+1)%history_.size();
            count_=std::min(count_+1,history_.size());
        }
        return "fresh endpoint has a unique carrier cycle";
    }
    std::optional<Prediction> prediction() const noexcept {
        if(!have_previous_)return {};
        const Endpoint* oldest=nullptr;
        for(std::size_t i=0;i<count_;++i) {
            const auto& p=history_[i];
            if(p.sample>=current_.sample||current_.sample-p.sample<2ULL*rate_/5)continue;
            if(!oldest||p.sample<oldest->sample)oldest=&p;
        }
        if(!oldest)return {};
        const auto L=static_cast<double>(current_.sample-oldest->sample)/rate_;
        const auto frequency=baseline_+(current_.residual-oldest->residual)/(static_cast<double>(phase_chord_detail::tau)*L);
        const auto past=sigma_*phase_chord_detail::pair_standard_deviations(oldest->sample,current_.sample)/std::sqrt(L);
        if(!std::isfinite(frequency)||!std::isfinite(past))return {};
        Prediction result;
        result.first_sample=oldest->sample;result.last_sample=current_.sample;
        result.phase=previous_.phase;result.frequency_hz=frequency;result.past_frequency_error=past;
        result.first_uncertainty=oldest->uncertainty;result.last_uncertainty=current_.uncertainty;
        result.sigma=sigma_;result.rate=rate_;
        // Account for constructing the stored mean center even if the baseline
        // and residual slope nearly cancel. This is temporary Prediction data,
        // not an extra retained estimator/decoder/cohort allocation.
        const auto residual_slope=(current_.residual-oldest->residual)/(static_cast<double>(phase_chord_detail::tau)*L);
        result.mean_roundoff_hz_=phase_chord_detail::upward(128*std::numeric_limits<double>::epsilon()*
            std::max({1.,std::abs(baseline_),std::abs(residual_slope),std::abs(frequency),std::abs(past)}));
        return result;
    }
private:
    struct Previous {
        std::uint64_t sample=0;
        double phase=0,frequency_hz=0,frequency_uncertainty_hz=0,phase_uncertainty=0;
    };
    static Previous previous(const Reference& r) noexcept {
        return {r.sample,r.phase_radians,r.frequency_hz,r.frequency_uncertainty_hz,r.phase_uncertainty_radians};
    }
    std::array<Endpoint,8> history_{};
    Endpoint current_{};
    Previous previous_{};
    std::size_t count_=0,position_=0;
    std::uint32_t rate_=0;
    double sigma_=0,baseline_=0,residual_roundoff_=0;
    bool have_previous_=false;
};
static_assert(sizeof(PhaseChord)<=384,"optional chord control must have an explicit small charge");
} // namespace datapump::helper
