#pragma once
#include "datapump/pattern_receiver.hpp"
#include "datapump/symbol_schedule.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <span>

namespace datapump::live::detail {
// One successfully exposed, whole confirmed drain. This occupies the existing
// Burst-only presentation coordinate storage; it owns no bits or allocations.
// Source phase is the EXACT Data-XOR coordinate, not a fitted clock cone.
struct ReceptionSourcePublication {
    std::uint64_t signal_id=0,epoch=0,count=0,first=0,end=0,symbol_samples=0,timing_tolerance=0;
    double raw_support=0,support=0;
    std::uint32_t phase=0,sample_rate=0;
    bool helper_assisted=false;
};
namespace source_handoff_detail {
inline bool multiply(std::uint64_t a,std::uint64_t b,std::uint64_t& out) noexcept {
    if(b && a>UINT64_MAX/b)return false;
    out=a*b;return true;
}
// Sum floor((a*i+b)/m), 0<=i<n. Callers reduce n to a modular period <=Fs;
// checked arithmetic leaves unsupported coordinates on the ordinary policy.
inline std::optional<std::uint64_t> floor_sum(std::uint64_t n,std::uint64_t m,
        std::uint64_t a,std::uint64_t b) noexcept {
    if(!m)return {};
    std::uint64_t sum=0;
    for(;;) {
        if(a>=m) {
            auto x=n,y=n?n-1:0;if(x%2)y/=2;else x/=2;
            std::uint64_t term=0;
            if(!multiply(x,y,term) || !multiply(term,a/m,term) || term>UINT64_MAX-sum)return {};
            sum+=term;a%=m;
        }
        if(b>=m) {
            std::uint64_t term=0;
            if(!multiply(n,b/m,term) || term>UINT64_MAX-sum)return {};
            sum+=term;b%=m;
        }
        std::uint64_t maximum=0;
        if(!multiply(a,n,maximum) || b>UINT64_MAX-maximum)return {};
        maximum+=b;
        if(maximum<m)return sum;
        n=maximum/m;b=maximum%m;std::swap(m,a);
    }
}
inline std::optional<bool> equal_floors(std::uint64_t n,std::uint64_t m,
        std::uint64_t step,std::uint64_t lower,std::uint64_t upper) noexcept {
    const auto a=floor_sum(n,m,step,lower),b=floor_sum(n,m,step,upper);
    if(!a || !b)return {};
    return *a==*b;
}
}
// True means an ACTUAL Data(epoch,ordinal) disagreement in the common prefix;
// false proves every address equal. No keystream, received bits or payload size
// is consulted. For uniform T, each modular sequence repeats after <=Fs steps;
// floor sums test the complete period/prefix in O(log Fs), including T<Fs.
inline std::optional<bool> reception_source_conflict(const ReceptionSourcePublication& a,
        const ReceptionSourcePublication& b,std::uint64_t count) noexcept {
    if(!count || !a.symbol_samples || a.symbol_samples!=b.symbol_samples ||
       !a.sample_rate || a.sample_rate!=b.sample_rate ||
       a.phase>=a.sample_rate || b.phase>=b.sample_rate)return {};
    // Equality is useful only over representable actual source addresses.
    // Validate BOTH endpoints even for T>=Fs, before the fast equality path.
    std::uint64_t last_epoch=0;
    try {
        const auto last=modem::symbol_stream_address(a.epoch,a.phase,count-1,a.symbol_samples,a.sample_rate);
        (void)modem::symbol_stream_address(b.epoch,b.phase,count-1,b.symbol_samples,b.sample_rate);
        last_epoch=last.epoch;
    } catch(const Error&) {return {};}
    if(a.epoch!=b.epoch)return true;
    const auto fs=static_cast<std::uint64_t>(a.sample_rate),t=a.symbol_samples;
    const auto low=std::min(a.phase,b.phase),high=std::max(a.phase,b.phase);
    const auto epoch_period=fs/std::gcd(t%fs,fs);
    const auto same_epochs=source_handoff_detail::equal_floors(
        std::min(count,epoch_period),fs,t%fs,low,high);
    if(!same_epochs)return {};
    if(!*same_epochs)return true;
    if(t>=fs)return false; // Every within-second Data ordinal is zero.
    if(low/t!=high/t)return true;
    try {
        // Epoch sequences have already been proven equal, so either endpoint
        // supplies the complete consecutive-second range below.
        const auto seconds=last_epoch-a.epoch;
        if(seconds==UINT64_MAX)return {};
        // T<Fs advances the epoch by zero or one per symbol, so every second
        // from zero through 'seconds' occurs. Once epochs agree, the ordinal
        // difference is floor((phase-second*Fs)/T) between the two phases.
        const auto period=t/std::gcd(fs%t,t),step=(t-fs%t)%t;
        const auto equal=source_handoff_detail::equal_floors(
            std::min(seconds+1,period),t,step,low%t,high%t);
        if(!equal)return {};
        return !*equal;
    } catch(const Error&) {return {};}
}
// nullopt preserves ordinary arbitration. Only an EXACT corresponding native
// N-1 checkpoint may constrain a growing source conflict; there is no tail
// subtraction, total-deficit localization, extrapolation or payload-sized log.
inline std::optional<bool> reception_source_handoff_allowed(
        const ReceptionSourcePublication& old,const ReceptionSourcePublication& incoming,
        const std::optional<modem::PatternPrefixSupport>& prefix) noexcept {
    if(!(old.helper_assisted || incoming.helper_assisted) || !old.count || !incoming.count ||
       old.end<=old.first || incoming.end<=incoming.first ||
       !std::isfinite(old.support) || old.support<0)return {};
    const auto distance=old.first>incoming.first?old.first-incoming.first:incoming.first-old.first;
    if(!old.symbol_samples || old.symbol_samples!=incoming.symbol_samples ||
       distance>std::min(std::max(old.timing_tolerance,incoming.timing_tolerance),
                         (old.symbol_samples-1)/2))return {};
    const auto conflict=reception_source_conflict(old,incoming,std::min(old.count,incoming.count));
    if(!conflict)return {};
    if(!*conflict || incoming.count<=old.count)return true;
    if(!prefix || prefix->prefix_symbols!=old.count || prefix->prefix_symbols!=incoming.count-1 ||
       prefix->stream_first_sample!=incoming.first || prefix->end_sample<=incoming.first ||
       prefix->end_sample>incoming.end || !std::isfinite(prefix->support_samples) ||
       prefix->support_samples<0 || prefix->support_samples>static_cast<long double>(prefix->end_sample-incoming.first))return {};
    const auto normalized=(static_cast<long double>(prefix->support_samples)/
        (prefix->end_sample-incoming.first))*old.count*incoming.symbol_samples;
    if(!std::isfinite(normalized))return {};
    // Actual supported-media evidence, not a statistical confidence claim.
    // Widen a numerical tie so a full correct prefix cannot be frozen behind
    // a full false early source merely because its clock windows round apart.
    const auto scale=std::max({1.L,std::abs(normalized),static_cast<long double>(old.support),
        static_cast<long double>(prefix->support_samples)});
    const auto allowance=256*std::numeric_limits<double>::epsilon()*scale;
    return normalized+allowance>=old.support;
}
}
