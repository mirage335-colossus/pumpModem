#pragma once

#include "datapump/lpi_estimate.hpp"
#include "estimate_cancellation.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>

namespace datapump::lpi::detail {

// Numerical limits, not restrictions on physical capture bandwidth. Failure
// leaves the aggregate observer available and identifies omitted bank coverage.
inline constexpr long double maximum_gamma_shape = 1000000;
inline constexpr std::uint64_t maximum_bank_dwells = std::uint64_t{1} << 40;
struct MathBudget {
    unsigned remaining = 500000;
    std::stop_token stop;
    bool take() { if(remaining%256==0)estimate_detail::check(stop);if(!remaining)return false;--remaining;return true; }
};
struct GammaLogs { long double cdf, survival; };

inline long double log_complement(long double log_probability) {
    return log_probability < -std::numbers::ln2_v<long double> ?
        std::log1p(-std::exp(log_probability)) : std::log(-std::expm1(log_probability));
}

// Regularized incomplete gamma: lower series / upper modified-Lentz continued
// fraction, evaluated in logs. See NIST DLMF 8.7, 8.9 and 8.25. Bounded iteration
// and a shared budget prevent an advisory calculation from monopolizing the GUI.
inline std::optional<GammaLogs> gamma_logs(long double shape,long double x,MathBudget& budget) {
    if(!std::isfinite(shape)||shape<1||shape>maximum_gamma_shape||!std::isfinite(x)||x<0)
        return std::nullopt;
    if(x==0)return GammaLogs{-std::numeric_limits<long double>::infinity(),0};
    if(shape==1)return GammaLogs{log_complement(-x),-x};
    const auto prefix=shape*std::log(x)-x-std::lgamma(shape);
    constexpr auto epsilon=std::max(1e-17L,16*std::numeric_limits<long double>::epsilon());
    constexpr auto tiny=std::numeric_limits<long double>::min()/epsilon;
    constexpr unsigned iterations=16384;
    long double log_p=0,log_q=0;
    if(x<shape+1) {
        auto term=1/shape,sum=term,denominator=shape;
        bool converged=false;
        for(unsigned i=0;i<iterations;++i) {
            if(!budget.take())return std::nullopt;
            term*=x/++denominator;sum+=term;
            if(std::abs(term)<=std::abs(sum)*epsilon) {converged=true;break;}
        }
        if(!converged||!(sum>0))return std::nullopt;
        log_p=prefix+std::log(sum);
        if(log_p>64*epsilon)return std::nullopt;
        log_p=std::min(0.L,log_p);log_q=log_complement(log_p);
    } else {
        auto b=x+1-shape,c=1/tiny,d=1/b,h=d;
        bool converged=false;
        for(unsigned i=1;i<=iterations;++i) {
            if(!budget.take())return std::nullopt;
            const auto numerator=-static_cast<long double>(i)*(static_cast<long double>(i)-shape);
            b+=2;d=numerator*d+b;c=b+numerator/c;
            if(std::abs(d)<tiny)d=std::copysign(tiny,d);
            if(std::abs(c)<tiny)c=std::copysign(tiny,c);
            d=1/d;const auto delta=d*c;h*=delta;
            if(std::abs(delta-1)<=epsilon) {converged=true;break;}
        }
        if(!converged||!(h>0)||!std::isfinite(h))return std::nullopt;
        log_q=prefix+std::log(h);
        if(log_q>64*epsilon)return std::nullopt;
        log_q=std::min(0.L,log_q);log_p=log_complement(log_q);
    }
    return GammaLogs{log_p,log_q};
}

inline std::optional<long double> gamma_upper_quantile(long double shape,long double log_tail,
                                                      MathBudget& budget) {
    if(!std::isfinite(shape)||shape<1||shape>maximum_gamma_shape||
       !std::isfinite(log_tail)||log_tail>=0)return std::nullopt;
    if(shape==1)return -log_tail;
    long double lower=0;
    const auto exponent=-log_tail;
    // Chernoff upper bracket for a Gamma(shape,1) variable.
    auto upper=shape+std::sqrt(2*shape*exponent)+exponent;
    auto check=gamma_logs(shape,upper,budget);
    if(!check||check->survival>log_tail)return std::nullopt;
    for(unsigned i=0;i<64;++i) {
        const auto middle=(lower+upper)/2;
        check=gamma_logs(shape,middle,budget);if(!check)return std::nullopt;
        if(check->survival>log_tail)lower=middle;else upper=middle;
        if(upper-lower<=1e-13L*(1+upper))return upper;
    }
    return upper; // Upper bracket: does not spend more than the null tail budget.
}

struct CellFit {
    long double log_miss=0,log_false_alarm_survival=0;
};
inline std::optional<CellFit> channel_cells(std::uint64_t samples,long double rho,unsigned channels,
                                          std::uint64_t dwells,MathBudget& budget) {
    if(!samples||samples>maximum_gamma_shape||!channels||!dwells||dwells>maximum_bank_dwells||
       !std::isfinite(rho)||rho<=0)return std::nullopt;
    const auto cells=static_cast<long double>(channels)*dwells;
    const auto log_cell_tail=std::log(-std::expm1(
        std::log1p(-static_cast<long double>(false_alarm_probability))/cells));
    const auto threshold=gamma_upper_quantile(samples,log_cell_tail,budget);
    if(!threshold)return std::nullopt;
    const auto null=gamma_logs(samples,*threshold,budget);
    const auto active=gamma_logs(samples,*threshold/(1+rho),budget);
    if(!null||!active)return std::nullopt;
    // One active channel per dwell, all channels recorded simultaneously.
    // This product is independent of the unknown order of those active channels.
    return CellFit{static_cast<long double>(dwells)*active->cdf+
        static_cast<long double>(channels-1)*dwells*null->cdf,cells*null->cdf};
}

struct BankResult {
    ChannelBankStatus status=ChannelBankStatus::numeric_limit;
    double seconds=0;
    std::uint64_t dwells=0,samples_per_cell=0;
};
inline BankResult channel_bank(long double bandwidth,long double rho,const Hopping& hopping,
                               long double aggregate_seconds,std::stop_token stop = {}) {
    estimate_detail::check(stop);
    if(!std::isfinite(bandwidth)||bandwidth<=0||!hopping.channels||
       !std::isfinite(hopping.dwell_seconds)||hopping.dwell_seconds<=0)return {};
    const auto dwell_samples=std::floor(bandwidth*hopping.dwell_seconds);
    if(dwell_samples<1)return {ChannelBankStatus::insufficient_time_bandwidth};
    if(!std::isfinite(dwell_samples)||!std::isfinite(aggregate_seconds)||aggregate_seconds<=0)
        return {};
    MathBudget budget;budget.stop=stop;
    const auto supported=static_cast<std::uint64_t>(std::min(dwell_samples,maximum_gamma_shape));
    constexpr auto target=-2.3025850929940456840179914546843642L; // log(.1)
    auto fit=channel_cells(supported,rho,hopping.channels,1,budget);
    if(!fit)return {};
    if(fit->log_miss<=target) {
        // Within the first known dwell, use complete independent complex samples.
        // Never apply a nonzero-variance normal approximation at zero duration.
        std::uint64_t lower=0,upper=supported;
        while(upper-lower>1) {
            const auto middle=lower+(upper-lower)/2;
            fit=channel_cells(middle,rho,hopping.channels,1,budget);if(!fit)return {};
            if(fit->log_miss<=target)upper=middle;else lower=middle;
        }
        return {ChannelBankStatus::available,static_cast<double>(upper/bandwidth),1,upper};
    }
    if(dwell_samples>maximum_gamma_shape)return {};
    const auto possible=std::floor(aggregate_seconds/hopping.dwell_seconds);
    if(possible<2)return {ChannelBankStatus::not_faster};
    const auto maximum=static_cast<std::uint64_t>(std::min(possible,
        static_cast<long double>(maximum_bank_dwells)));
    fit=channel_cells(supported,rho,hopping.channels,maximum,budget);if(!fit)return {};
    if(fit->log_miss>target)return {possible>maximum_bank_dwells?
        ChannelBankStatus::numeric_limit:ChannelBankStatus::not_faster};
    // Search complete-dwell endpoints only: creating a new partial cell changes
    // the family threshold discontinuously. Independent scale-Gamma cells have
    // increasing maximum-test power at these endpoints.
    std::uint64_t lower=1,upper=maximum;
    while(upper-lower>1) {
        const auto middle=lower+(upper-lower)/2;
        fit=channel_cells(supported,rho,hopping.channels,middle,budget);if(!fit)return {};
        if(fit->log_miss<=target)upper=middle;else lower=middle;
    }
    const auto seconds=static_cast<long double>(upper)*hopping.dwell_seconds;
    if(seconds>std::numeric_limits<double>::max())return {};
    return {ChannelBankStatus::available,static_cast<double>(seconds),upper,supported};
}
} // namespace datapump::lpi::detail
