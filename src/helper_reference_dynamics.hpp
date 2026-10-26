#pragma once

#include "datapump/helper_receiver.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>

namespace datapump::helper {

namespace reference_dynamics_detail {
constexpr long double tau=2*std::numbers::pi_v<long double>;
inline double upward(long double value) noexcept {
    const auto infinity=std::numeric_limits<double>::infinity();
    if(!std::isfinite(value) || value>std::numeric_limits<double>::max())return infinity;
    if(value<0)return infinity;
    return std::nextafter(static_cast<double>(value),infinity);
}
inline bool finite_nonnegative(double value) noexcept {return std::isfinite(value)&&value>=0;}
} // namespace reference_dynamics_detail

// This is a future phase allowance, not an oscillator accuracy-to-Hz/s
// conversion. sigma is the already topology-combined oscillator phase diffusion
// in radians/sqrt(second); the caller must not combine the two ends again.
// Missing fresh observed phase/punctuation authority, invalid arithmetic or a negative horizon
// declines optional steering with infinity. Neither metadata nor UTC is used.
inline double helper_phase_forecast_uncertainty(const Reference& reference,double horizon_seconds,
        double sigma_radians_per_sqrt_second) noexcept {
    using namespace reference_dynamics_detail;
    if(!reference.frequency_valid || !reference.phase_valid || !reference.timing_valid ||
       !reference.phase_observation_known || reference.phase_observation_sample!=reference.sample ||
       !std::isfinite(reference.frequency_hz) || !std::isfinite(reference.phase_radians) ||
       !finite_nonnegative(reference.phase_uncertainty_radians) ||
       !finite_nonnegative(reference.frequency_uncertainty_hz) ||
       !std::isfinite(reference.dwell_start_seconds) ||
       !finite_nonnegative(reference.dwell_uncertainty_seconds) || reference.dwell_uncertainty_seconds>=.2 ||
       !finite_nonnegative(horizon_seconds) || !finite_nonnegative(sigma_radians_per_sqrt_second))
        return std::numeric_limits<double>::infinity();
    const auto h=static_cast<long double>(horizon_seconds);
    const auto forecast=static_cast<long double>(reference.phase_uncertainty_radians)+
        tau*reference.frequency_uncertainty_hz*h+8*sigma_radians_per_sqrt_second*std::sqrt(h);
    return upward(forecast+64*std::numeric_limits<double>::epsilon()*std::max(1.L,forecast));
}

enum class ReferenceDynamicsVerdict {unqualified,accept,reject};
enum class ReferenceDynamicsReason {
    accepted,
    awaiting_anchors,
    sample_clock_unset,
    sample_clock_mismatch,
    invalid_model,
    invalid_measurement,
    future_measurement,
    nonincreasing_measurement,
    phase_or_punctuation_unqualified,
    phase_observation_unknown,
    carried_phase_observation,
    ambiguous_phase_unwrap,
    unsupported_phase_interval,
    numerical_limit,
    oscillator_innovation_exceeded
};
inline const char* reference_dynamics_reason(ReferenceDynamicsReason reason) noexcept {
    switch(reason) {
    case ReferenceDynamicsReason::accepted:return "helper phase increments fit the configured oscillator diffusion";
    case ReferenceDynamicsReason::awaiting_anchors:return "helper oscillator continuity needs three independently dated phase anchors";
    case ReferenceDynamicsReason::sample_clock_unset:return "helper continuity has no explicitly selected capture sample clock";
    case ReferenceDynamicsReason::sample_clock_mismatch:return "helper continuity sample clock changed; a new capture reset is required";
    case ReferenceDynamicsReason::invalid_model:return "helper oscillator diffusion is invalid";
    case ReferenceDynamicsReason::invalid_measurement:return "helper continuity measurement is invalid";
    case ReferenceDynamicsReason::future_measurement:return "helper continuity measurement is ahead of captured samples";
    case ReferenceDynamicsReason::nonincreasing_measurement:return "helper continuity needs a strictly newer phase measurement";
    case ReferenceDynamicsReason::phase_or_punctuation_unqualified:return "helper phase or punctuation ambiguity remains unresolved";
    case ReferenceDynamicsReason::phase_observation_unknown:return "helper actual phase-observation provenance is unknown";
    case ReferenceDynamicsReason::carried_phase_observation:return "helper phase was propagated from an older observation; no independent anchor";
    case ReferenceDynamicsReason::ambiguous_phase_unwrap:return "helper prior phase allowance spans a cycle ambiguity";
    case ReferenceDynamicsReason::unsupported_phase_interval:return "helper phase has no cycle supported by its prior frequency interval";
    case ReferenceDynamicsReason::numerical_limit:return "helper continuity arithmetic cannot certify a phase cycle";
    case ReferenceDynamicsReason::oscillator_innovation_exceeded:return "helper phase trend exceeds configured oscillator diffusion; optional steering declined";
    }
    return "helper continuity is unqualified";
}
struct ReferenceDynamicsResult {
    ReferenceDynamicsVerdict verdict=ReferenceDynamicsVerdict::unqualified;
    ReferenceDynamicsReason reason=ReferenceDynamicsReason::awaiting_anchors;
    std::size_t anchors=0;
    double frequency_change_hz=0,allowed_change_hz=0;
};

// A small optional steering gate. It owns no receiver, NCO, payload or metadata
// state. In particular, reject/unqualified never resets an accepted phase
// convention or retires a Robust receiver. The caller decides bounded fallback.
//
// Reset explicitly for a new capture generation/sample clock, not for a lost
// helper or a rejected trend. Distinct valid dates roll through the fixed
// history even when a phase gap is ambiguous, so later independent increments
// can qualify again without declaring that gap coherent.
class ReferenceDynamics {
public:
    static constexpr double phase_forecast_ceiling_radians=.25;
    void reset(std::uint32_t sample_rate) noexcept {rate_=sample_rate;count_=0;anchors_={};}
    ReferenceDynamicsResult observe(const Reference& reference,std::uint64_t frontier,
            std::uint32_t sample_rate,double sigma_radians_per_sqrt_second) noexcept {
        using namespace reference_dynamics_detail;
        const auto result=[&](ReferenceDynamicsReason reason) {
            return ReferenceDynamicsResult{ReferenceDynamicsVerdict::unqualified,reason,count_};
        };
        if(!rate_)return result(ReferenceDynamicsReason::sample_clock_unset);
        if(!sample_rate || sample_rate!=rate_ || reference.sample_rate!=rate_)
            return result(ReferenceDynamicsReason::sample_clock_mismatch);
        if(!finite_nonnegative(sigma_radians_per_sqrt_second))return result(ReferenceDynamicsReason::invalid_model);
        if(reference.sample>frontier || reference.frequency_sample>reference.sample)
            return result(ReferenceDynamicsReason::future_measurement);
        if(!reference.frequency_valid || !std::isfinite(reference.frequency_hz) ||
           !std::isfinite(reference.phase_radians) ||
           !finite_nonnegative(reference.frequency_uncertainty_hz) ||
           !finite_nonnegative(reference.phase_uncertainty_radians) ||
           !std::isfinite(reference.dwell_start_seconds) ||
           !finite_nonnegative(reference.dwell_uncertainty_seconds))
            return result(ReferenceDynamicsReason::invalid_measurement);
        // The receiver has already removed the independently known punctuation
        // sign. A modulo-400ms dwell observation does not establish UTC/epoch.
        if(!reference.phase_valid || !reference.timing_valid || reference.dwell_uncertainty_seconds>=.2)
            return result(ReferenceDynamicsReason::phase_or_punctuation_unqualified);
        if(!reference.phase_observation_known)return result(ReferenceDynamicsReason::phase_observation_unknown);
        if(reference.phase_observation_sample>reference.sample)
            return result(ReferenceDynamicsReason::future_measurement);
        if(reference.phase_observation_sample!=reference.sample)
            return result(ReferenceDynamicsReason::carried_phase_observation);
        if(count_ && reference.sample<=anchors_[count_-1].sample)
            return result(ReferenceDynamicsReason::nonincreasing_measurement);
        const Anchor anchor{reference.sample,reference.frequency_hz,reference.frequency_uncertainty_hz,
            reference.phase_radians,reference.phase_uncertainty_radians};
        if(count_<anchors_.size())anchors_[count_++]=anchor;
        else {anchors_[0]=anchors_[1];anchors_[1]=anchors_[2];anchors_[2]=anchor;}
        if(count_<2)return result(ReferenceDynamicsReason::awaiting_anchors);
        const auto last=increment(anchors_[count_-2],anchors_[count_-1],sigma_radians_per_sqrt_second);
        if(last.reason!=ReferenceDynamicsReason::accepted)return result(last.reason);
        if(count_<3)return result(ReferenceDynamicsReason::awaiting_anchors);
        const auto first=increment(anchors_[0],anchors_[1],sigma_radians_per_sqrt_second);
        if(first.reason!=ReferenceDynamicsReason::accepted)return result(first.reason);
        const auto h1=first.seconds,h2=last.seconds;
        const auto measurement=(anchors_[2].phase_uncertainty/h2+
            anchors_[1].phase_uncertainty*(1/h1+1/h2)+anchors_[0].phase_uncertainty/h1)/tau;
        // Consecutive phase increments occupy disjoint time intervals. Their
        // Wiener average-frequency difference has variance sigma^2*(1/h1+1/h2)
        // /tau^2. Absolute measurement allowances remain conservative even for
        // correlated/overlapping local phase-fit errors at the shared anchor.
        const auto diffusion=8*static_cast<long double>(sigma_radians_per_sqrt_second)/tau*
            std::sqrt(1/h1+1/h2);
        const auto numeric=first.numeric_phase/(tau*h1)+last.numeric_phase/(tau*h2)+
            64*std::numeric_limits<double>::epsilon()*
                std::max({1.L,std::abs(first.frequency),std::abs(last.frequency)});
        const auto bound=measurement+diffusion+numeric;
        const auto difference=std::abs(last.frequency-first.frequency);
        if(!std::isfinite(bound) || !std::isfinite(difference))return result(ReferenceDynamicsReason::numerical_limit);
        const auto allowed=upward(bound);
        const auto change=upward(difference);
        if(!std::isfinite(allowed) || !std::isfinite(change))return result(ReferenceDynamicsReason::numerical_limit);
        // Compare with the outward bound in the wider working type as well:
        // the reported double diagnostics must not turn a boundary into a pass.
        const bool accepted=difference<=static_cast<long double>(allowed);
        return {accepted?ReferenceDynamicsVerdict::accept:ReferenceDynamicsVerdict::reject,
            accepted?ReferenceDynamicsReason::accepted:ReferenceDynamicsReason::oscillator_innovation_exceeded,
            count_,change,allowed};
    }
    std::size_t anchor_count() const noexcept {return count_;}

private:
    struct Anchor {
        std::uint64_t sample=0;
        double frequency=0,frequency_uncertainty=0,phase=0,phase_uncertainty=0;
    };
    struct Increment {
        ReferenceDynamicsReason reason=ReferenceDynamicsReason::numerical_limit;
        long double seconds=0,frequency=0,numeric_phase=0;
    };
    Increment increment(const Anchor& previous,const Anchor& current,double sigma) const noexcept {
        using namespace reference_dynamics_detail;
        const auto h=static_cast<long double>(current.sample-previous.sample)/rate_;
        const auto predicted=tau*previous.frequency*h;
        const auto phase_difference=static_cast<long double>(current.phase)-previous.phase;
        if(!(h>0) || !std::isfinite(predicted) || !std::isfinite(phase_difference))return {};
        const auto numeric=64*std::numeric_limits<double>::epsilon()*
            std::max({1.L,std::abs(predicted),std::abs(static_cast<long double>(previous.phase)),
                std::abs(static_cast<long double>(current.phase))});
        const auto allowance=static_cast<long double>(previous.phase_uncertainty)+current.phase_uncertainty+
            tau*previous.frequency_uncertainty*h+8*static_cast<long double>(sigma)*std::sqrt(h)+numeric;
        if(!std::isfinite(allowance))return {};
        // An interval as wide as half a cycle does not authorize guessing a
        // branch, even if the nearest phase happens to look plausible.
        if(allowance>=std::numbers::pi_v<long double>)return {ReferenceDynamicsReason::ambiguous_phase_unwrap};
        const auto residual=std::remainder(phase_difference-predicted,tau);
        if(!std::isfinite(residual))return {};
        if(std::abs(residual)>allowance)return {ReferenceDynamicsReason::unsupported_phase_interval};
        const auto frequency=(predicted+residual)/(tau*h);
        if(!std::isfinite(frequency))return {};
        return {ReferenceDynamicsReason::accepted,h,frequency,numeric};
    }
    std::array<Anchor,3> anchors_{};
    std::uint32_t rate_=0;
    std::size_t count_=0;
};

static_assert(sizeof(ReferenceDynamics)<=192,"helper continuity must remain fixed and small");

} // namespace datapump::helper
