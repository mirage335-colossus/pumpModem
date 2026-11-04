#pragma once
#include "datapump/types.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <string>

namespace datapump::helper {
struct SteadyToneEstimate {
    bool available=false;
    std::size_t accepted_observations=384;
    double uncertainty_hz=0,observation_seconds=0,block_seconds=0,noise_bandwidth_hz=0;
    double minimum_level_cn0_db_hz=0,phase_innovation_radians=0;
    std::string reason;
};
// Conditional ENGINEERING calculation from ToneTracker::Impl::fit, not an
// empirical calibration or a prediction of lock probability. Its ideal maximum
// assumes 384 UNINTERRUPTED steady-tone differences within the 8 s recent
// aperture at the actual 20 ms block cadence, ZERO excess residual, constant
// endpoint thermal variance, and the configured Wiener phase process. Adjacent
// differences share their measured phase endpoint, exactly as in the runtime
// fit. Metadata/phase reversals, capture glitches, noisy or rejected observations
// break this ideal condition and can widen the actual uncertainty.
inline SteadyToneEstimate steady_tone_estimate(std::uint32_t rate,double cn0_db_hz,double sigma) {
    SteadyToneEstimate out;
    if(rate<1000 || rate>192000 || !std::isfinite(cn0_db_hz) || !std::isfinite(sigma) || sigma<0) {
        out.reason="Raw capture rate or phase-process model is unavailable";return out;
    }
    constexpr auto tau=2*std::numbers::pi;
    const auto block=std::clamp<std::size_t>(static_cast<std::size_t>(std::lround(rate*.02)),16,4096);
    const auto dt=static_cast<double>(block)/rate;
    double ws=0,w2=0;
    for(std::size_t i=0;i<block;++i) {
        const auto w=.5-.5*std::cos(tau*i/(block-1));ws+=w;w2+=w*w;
    }
    const auto bandwidth=rate*w2/(ws*ws),cn0=std::pow(10.,cn0_db_hz/10);
    out.noise_bandwidth_hz=bandwidth;out.block_seconds=dt;
    out.minimum_level_cn0_db_hz=10*std::log10(4*bandwidth);
    if(!std::isfinite(cn0) || !(cn0>4*bandwidth)) {
        out.reason="Expected tone does not exceed the real-time tracker level gate";return out;
    }
    const auto phase_variance=bandwidth/(2*cn0);
    out.phase_innovation_radians=6*std::sqrt(2*phase_variance+sigma*sigma*dt);
    if(out.phase_innovation_radians>=2.4) {
        out.reason="Expected phase innovations exceed the real-time tracker's unwrapped following gate";return out;
    }
    const auto n=out.accepted_observations;
    // Same actual frequency-observation dates and current sample endpoint as
    // fit(): last block_first is one block before now, up to one raw sample.
    const auto mean_t=-(static_cast<double>(n)+1)*dt/2+1./rate;
    double tt=0;
    for(std::size_t i=0;i<n;++i) {
        const auto t=(static_cast<double>(i)-(static_cast<double>(n)-1)/2)*dt;tt+=t*t;
    }
    double weight_square=0,endpoint_square=0,previous_weight=0;
    for(std::size_t i=0;i<n;++i) {
        const auto t=(static_cast<double>(i)-(static_cast<double>(n)-1)/2)*dt;
        const auto weight=1./n-mean_t*t/tt;weight_square+=weight*weight;
        // First/last endpoints occur once; each shared interior endpoint has
        // coefficient w[i-1]-w[i]. Sum squares without covariance cancellation.
        const auto coefficient=i?previous_weight-weight:weight;
        endpoint_square+=coefficient*coefficient;previous_weight=weight;
    }
    endpoint_square+=previous_weight*previous_weight;
    // Exact constant-variance endpoint covariance for this ideal contiguous
    // observation chain. The process term retains the runtime PSD bound.
    const auto thermal=phase_variance/(tau*tau*dt*dt)*endpoint_square;
    const auto process=sigma*sigma/(tau*tau*dt)*weight_square;
    out.observation_seconds=(n+1)*dt;
    out.uncertainty_hz=3*std::sqrt(thermal+process)+
        3*sigma/(tau*std::sqrt(std::max(.02,out.observation_seconds)))+.005;
    out.available=std::isfinite(out.uncertainty_hz);
    out.reason=out.available?
        "Conditional ideal uninterrupted steady-tone engineering estimate: 384 accepted 20 ms observations within 8 s, zero excess residual; actual APSK, punctuation and noisy/rejected observations can widen uncertainty; not a measured lock or a qualification-probability forecast":
        "Conditional tone uncertainty exceeds the numerical range";
    return out;
}
}
