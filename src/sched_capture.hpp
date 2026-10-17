#pragma once

#include "datapump/pattern_receiver.hpp"
#include "datapump/utc_timing.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace datapump::live::detail {

// Capture and decoder must use the same immutable settings/key generation.
// A certificate is not permission to retire an epoch or change its prior.
struct SchedCaptureIdentity {
    std::uint64_t generation=0,revision=0;
    bool operator==(const SchedCaptureIdentity&) const = default;
};
struct SchedCaptureBound {
    // The capture thread owns this calendar/cache; never share the receiver's
    // mutable one-frame cache concurrently. Include EVERY key and profile.
    const modem::SchedHopSourceMap* map=nullptr;
    modem::ScheduledCaptureEnvelope envelope;
};

// A finite continuing timestamp envelope is an explicit caller precondition,
// not something inferred from the requested GPS/audio dropdowns. Future
// timestamp centers/slopes outside these bounds invalidate the certificate.
struct SchedCaptureFuture {
    std::uint64_t reference_sample=0,horizon_samples=0;
    modem::SchedHopCoordinate utc_at_reference;
    std::uint32_t sample_rate=0;
    long double utc_rate_lower=1,utc_rate_upper=1; // UTC samples/input sample.
    long double timestamp_radius_samples=0;
    long double minimum_rate=1,maximum_rate=1;
    long double propagation_samples=0;
    long double epoch_rounding_radius_samples=0;
    long double start_uncertainty_samples=0;
    std::uint64_t maximum_phase_samples=0;
};

inline long double sched_capture_roundoff(long double magnitude) {
    return 128*std::numeric_limits<long double>::epsilon()*std::max(1.L,magnitude);
}

// Validate the complete affine callback against the ORIGINAL future-certificate
// UTC envelope. The callback's mapped slope need not equal nominal Fs: its
// start alone cannot prove that an interior receiver admission remains covered.
// Both endpoint differences are affine, so endpoint containment proves every
// interior point without a per-sample loop or an extra sample/time allowance.
inline bool checkedCaptureTimestamp(std::uint64_t begin,std::uint64_t first,
        std::uint64_t count,std::uint32_t rate,long double utc_reference,
        long double observed_epoch,long double seconds_per_frame) {
    if(!rate || first<begin || count>std::numeric_limits<std::uint64_t>::max()-first ||
       !std::isfinite(utc_reference) || !std::isfinite(observed_epoch) ||
       !std::isfinite(seconds_per_frame) || utc_reference<0 || observed_epoch<0 ||
       seconds_per_frame<=0)return false;
    // Match the future-envelope helper's finite numerical scope. A large
    // absolute sample origin is supported, but an extreme relative age can
    // round the cone boundary outward and must retain PCM conservatively.
    if(first+count-begin>1000000000000000ULL)return false;
    // Subtract exact sample counters before conversion, and add the duration
    // to a relative UTC delta. Adding it to a large absolute epoch first can
    // erase a small but real violation of the original timestamp envelope.
    const auto start_age=static_cast<long double>(first-begin)/rate;
    const auto end_age=static_cast<long double>(first+count-begin)/rate;
    const auto delta=observed_epoch-utc_reference;
    const auto duration=static_cast<long double>(count)*seconds_per_frame;
    const auto end_delta=delta+duration;
    if(!std::isfinite(start_age) || !std::isfinite(end_age) || !std::isfinite(delta) ||
       !std::isfinite(duration) || !std::isfinite(end_delta) ||
       !std::isfinite(observed_epoch+duration))return false;
    const auto contains=[](long double age,long double difference) {
        const auto lower=.999L*age-1,upper=1.001L*age+1;
        return std::isfinite(lower) && std::isfinite(upper) && difference>=lower && difference<=upper;
    };
    return contains(start_age,delta) && contains(end_age,end_delta);
}

// A current-cohort certificate expires before the next UTC-second admission.
// Use the same mapped seconds/frame as the actual constructor clock, rather
// than assuming nominal Fs. Equality at the fence retains PCM conservatively.
inline bool checkedCaptureFence(long double observed_epoch,std::uint64_t count,
        long double seconds_per_frame,long double utc_second) {
    if(!std::isfinite(observed_epoch)||!std::isfinite(seconds_per_frame)||
       !std::isfinite(utc_second)||seconds_per_frame<=0 || utc_second<0 ||
       std::floor(utc_second)!=utc_second)return false;
    const auto offset=observed_epoch-utc_second;
    if(offset<0 || offset>=1)return false;
    const auto duration=static_cast<long double>(count)*seconds_per_frame;
    if(!std::isfinite(duration))return false;
    const auto pad=sched_capture_roundoff(std::max(std::abs(offset),duration));
    return duration+pad<1-offset;
}

// Frozen Burst admission permits a proof to span UTC-second fences. Keep the
// original dated input model as a continuity check; a later callback cannot
// silently replace its uncertainty or sample-clock bounds with wider ones.
struct BurstCaptureTimestamp {
    std::uint64_t reference_sample=0;
    std::uint32_t sample_rate=0;
    audio::TimePrediction reference;

    bool accepts(std::uint64_t first,std::uint64_t count,long double observed_epoch,
                 const std::optional<audio::TimePrediction>& timing) const {
        const auto finite=[](long double x){return std::isfinite(x);};
        if(!sample_rate || !timing || first<reference_sample ||
           count>std::numeric_limits<std::uint64_t>::max()-first ||
           reference.quality==audio::TimingQuality::unavailable ||
           timing->quality==audio::TimingQuality::unavailable ||
           !finite(reference.utc_seconds) || reference.utc_seconds<0 ||
           !finite(timing->utc_seconds) || timing->utc_seconds<0 ||
           !finite(observed_epoch) || observed_epoch<0 ||
           !finite(reference.seconds_per_frame) || reference.seconds_per_frame<=0 ||
           !finite(reference.uncertainty_seconds) || reference.uncertainty_seconds<0 ||
           !finite(reference.rate_uncertainty_fraction) || reference.rate_uncertainty_fraction<0 ||
           !finite(reference.system_clock_phase_uncertainty_seconds) || reference.system_clock_phase_uncertainty_seconds<0 ||
           !finite(timing->seconds_per_frame) || timing->seconds_per_frame<=0 ||
           !finite(timing->uncertainty_seconds) || timing->uncertainty_seconds<0 ||
           !finite(timing->rate_uncertainty_fraction) || timing->rate_uncertainty_fraction<0 ||
           !finite(timing->system_clock_phase_uncertainty_seconds) || timing->system_clock_phase_uncertainty_seconds<0)
            return false;
        // Live has already removed station phase from the provider radius.
        // Two allowed station-clock states can differ by at most 2C. Keep C
        // fixed at admission; never renew it per callback or turn it into ppm.
        const auto phase=static_cast<long double>(reference.system_clock_phase_uncertainty_seconds);
        const auto radius=static_cast<long double>(reference.uncertainty_seconds)+2*phase;
        const auto slope=static_cast<long double>(reference.seconds_per_frame);
        const auto spread=static_cast<long double>(reference.rate_uncertainty_fraction)/sample_rate;
        // AudioBlock currently stores its UTC center as double. Account only
        // for that representation error, separate from physical timing error.
        const auto pad=4*std::numeric_limits<double>::epsilon()*std::max(1.L,std::abs(observed_epoch));
        if(spread>=slope || std::abs(timing->utc_seconds-observed_epoch)>pad ||
           timing->system_clock_phase_uncertainty_seconds>phase+pad)return false;
        const auto delta=observed_epoch-reference.utc_seconds;
        const auto contains=[&](std::uint64_t sample,long double difference,long double uncertainty) {
            const auto age=static_cast<long double>(sample-reference_sample);
            return finite(difference) && finite(uncertainty) &&
                   difference-uncertainty>=(slope-spread)*age-radius-pad &&
                   difference+uncertainty<=(slope+spread)*age+radius+pad;
        };
        return contains(first,delta,timing->uncertainty_seconds) && contains(first+count,
            delta+static_cast<long double>(count)*timing->seconds_per_frame,
            timing->uncertainty_seconds+static_cast<long double>(count)*timing->rate_uncertainty_fraction/sample_rate);
    }
};

// Covers the ORIGINAL finite start prior for every newly admitted epoch,
// including qualified-prior fallback. It does not substitute an arbitrarily
// tight requested clock window for the actual receiver lattice.
//
// At admission a, let E*Fs = UTC(a)*Fs - propagation - e, |e|<=R,
// and origin=(E-UTC(a)+propagation+delta)*Fs, |delta|<=S.
// For q(x)=E*Fs+phase+r*(x-a-origin), at reference x0:
// q(x0)=UTC(x0)*Fs-propagation+phase+(tau-r)*(a-x0)
//       +(r-1)*e-r*delta.
// Every term is multi-affine; extrema occur at the enumerated corners.
// The bound is intentionally conservative when a tight qualified map exists.
inline std::optional<modem::ScheduledCaptureEnvelope> sched_capture_future_envelope(
        const SchedCaptureFuture& f) {
    const auto finite=[](long double x){return std::isfinite(x);};
    if(!f.sample_rate || f.horizon_samples>1000000000000000ULL ||
       f.horizon_samples>std::numeric_limits<std::uint64_t>::max()-f.reference_sample ||
       f.maximum_phase_samples>=f.sample_rate ||
       !finite(f.utc_rate_lower)||!finite(f.utc_rate_upper)||
       !finite(f.minimum_rate)||!finite(f.maximum_rate)||
       !finite(f.timestamp_radius_samples)||!finite(f.propagation_samples)||
       !finite(f.epoch_rounding_radius_samples)||!finite(f.start_uncertainty_samples)||
       f.utc_rate_lower<=0 || f.utc_rate_upper<f.utc_rate_lower ||
       f.minimum_rate<=0 || f.maximum_rate<f.minimum_rate ||
       f.timestamp_radius_samples<0 || f.propagation_samples<0 ||
       f.epoch_rounding_radius_samples<0 || f.start_uncertainty_samples<0)
        return std::nullopt;
    try {
        f.utc_at_reference.validate();
        long double lo=std::numeric_limits<long double>::infinity(),hi=-lo;
        for(const auto age:{0.L,static_cast<long double>(f.horizon_samples)})
            for(const auto tau:{f.utc_rate_lower,f.utc_rate_upper})
                for(const auto r:{f.minimum_rate,f.maximum_rate})
                    for(const auto e:{-f.epoch_rounding_radius_samples,f.epoch_rounding_radius_samples})
                        for(const auto delta:{-f.start_uncertainty_samples,f.start_uncertainty_samples}) {
                            const auto offset=(tau-r)*age+(r-1)*e-r*delta;
                            lo=std::min(lo,offset);hi=std::max(hi,offset);
                        }
        hi+=static_cast<long double>(f.maximum_phase_samples);
        const auto pad=f.timestamp_radius_samples+sched_capture_roundoff(
            std::max(std::abs(lo),std::abs(hi))+f.propagation_samples);
        if(!finite(lo)||!finite(hi)||!finite(pad))return std::nullopt;
        modem::ScheduledCaptureEnvelope result;
        result.reference_sample=f.reference_sample;
        result.minimum_rate=f.minimum_rate;result.maximum_rate=f.maximum_rate;
        result.lower=f.utc_at_reference.add_signed(lo-f.propagation_samples-pad);
        result.upper=f.utc_at_reference.add_signed(hi-f.propagation_samples+pad);
        return result;
    } catch(const Error&) { return std::nullopt; }
}

struct SchedCaptureCertificate {
    SchedCaptureIdentity identity;
    std::uint64_t begin=0,end=0; // Physical capture sample coordinates, half open.
    std::span<const SchedCaptureBound> bounds;
    // Defaults fail closed. The integrator must prove the complete resident
    // AND future-admission union, a continuing timestamp envelope, and that
    // every backend supports paused-source skips (including partial FFTs).
    bool residents_complete=false,future_admissions_complete=false;
    bool timestamp_model_valid=false,paused_source_skip_supported=false;

    bool excludes(std::uint64_t first,std::uint64_t count,SchedCaptureIdentity current) const {
        if(current!=identity || !residents_complete || !future_admissions_complete ||
           !timestamp_model_valid || !paused_source_skip_supported || bounds.empty() ||
           first<begin || first>end || count>end-first)return false;
        try {
            const auto last=first+count;
            for(const auto& b:bounds) {
                const auto& e=b.envelope;
                if(!b.map || b.map->continuous() || first<e.reference_sample ||
                   !std::isfinite(e.minimum_rate)||!std::isfinite(e.maximum_rate)||
                   e.minimum_rate<=0 || e.maximum_rate<e.minimum_rate ||
                   e.upper.distance(e.lower)<0)return false;
                const auto dlo=e.minimum_rate*static_cast<long double>(first-e.reference_sample);
                const auto dhi=e.maximum_rate*static_cast<long double>(last-e.reference_sample);
                const auto pad=sched_capture_roundoff(std::max(std::abs(dlo),std::abs(dhi)));
                const auto lower=e.lower.add_signed(dlo-pad),upper=e.upper.add_signed(dhi+pad);
                // Equality of the MONOTONE source map proves every interior
                // sample paused for every enclosed lane. Gain==0 alone does
                // not prove this: zero-power group guards still consume U.
                const auto a=b.map->locate(lower).source,z=b.map->locate(upper).source;
                if(a.distance(z)!=0)return false;
            }
            return true;
        } catch(const Error&) { return false; }
    }
};

} // namespace datapump::live::detail
