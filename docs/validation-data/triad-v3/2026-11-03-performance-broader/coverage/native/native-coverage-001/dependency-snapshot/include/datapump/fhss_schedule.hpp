#pragma once
#include "datapump/crypto.hpp"
#include "datapump/clock_sync.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <limits>

namespace datapump::modem {
// Immutable tuning/guard plan; deferred calendars are shared by TX and RX.
// Fake and a future radio adapter must consume
// the same keyed schedule; presentation must never invent a second hop order.
// This implements schedule selection, NOT radio control, settling or compliance.
struct FhssPlan {
    static constexpr unsigned capacity=200;
    std::array<double,capacity> shifts{};
    unsigned channels=0;
    double dwell_seconds=0;
    // Production Fake modes illustrate the power transition as well as tuning.
    // False retains the original PCM-only diagnostic reference. Production
    // bounds include BOTH endpoint clocks and the propagation region. Offset
    // delays the receiving schedule; it never delays the transmitted sequence.
    bool power_ramps=false;
    // Shared TX/RX UTC calendar: whole symbols are deferred through hop guards.
    // Both peers must use the same immutable waveform allowances.
    bool defer_symbols=false;
    // Production must never substitute ungated audio for an unavailable ramp.
    bool mandatory_ramps=false;
    // UTC-periodic pulse omissions are represented in every RX template.
    bool matched_gaps=false;
    // Smooth amplitude envelope V1, independent of cipher sample phase. The
    // receiver retains its original full-energy templates (conservative fading
    // projection), not a separate masked bank at every possible source sample.
    bool smooth_ramps=false;
    bool remote_guard_covered=true;
    double ramp_seconds=0;
    bool utc_schedule() const {return defer_symbols || matched_gaps || smooth_ramps;}
    double power_clock_region_seconds() const {
        return remote_guard_covered?relative_clock_region_seconds():clock_error_seconds;
    }
    std::string power_fallback_reason;
    // Clock accuracy is a +/- bound; Audio error is nonnegative lag [0,A].
    double clock_error_seconds=.01,audio_error_seconds=clock_sync::default_audio_error_seconds;
    // Low-level diagnostics may specify an exact peer clock. Production copies
    // the selected per-station accuracy to BOTH bounds, even at Default.
    double peer_clock_error_seconds=0,region_seconds=0,offset_seconds=0;
    double relative_clock_region_seconds() const {
        return clock_error_seconds+peer_clock_error_seconds+region_seconds/2;
    }
    void validate() const {
        if(channels<2 || channels>capacity || !std::isfinite(dwell_seconds) || dwell_seconds<=0)
            throw Error("invalid FHSS tuning plan");
        if(utc_schedule() && dwell_seconds!=.4)
            throw Error("UTC FHSS currently requires a 0.4-second dwell");
        for(const auto value:{clock_error_seconds,peer_clock_error_seconds,region_seconds,offset_seconds})
            if(!std::isfinite(value)||value<0||value>32768)
                throw Error("invalid FHSS clock/propagation allowance");
        if(!std::isfinite(audio_error_seconds)||audio_error_seconds<0||audio_error_seconds>60)
            throw Error("invalid FHSS audio lag allowance");
        for(unsigned i=0;i<channels;++i) {
            if(!std::isfinite(shifts[i]) || shifts[i]<0)throw Error("invalid FHSS Shift");
            for(unsigned j=0;j<i;++j)if(shifts[i]==shifts[j])throw Error("duplicate FHSS Shift");
        }
    }
};
// Production uses a smooth post-shaper envelope with an explicit bandwidth
// allowance. The diagnostic path omits coefficients and lets their finite pulse
// support drain. Neither path resets carrier or cryptographic stream time.
struct FhssPowerShape {
    long double period=0,quiet=0,support=0,lag_center=0;
    bool smooth=false;
    bool enabled=false;
    FhssPowerShape()=default;
    FhssPowerShape(const FhssPlan& plan,std::uint32_t rate,std::uint64_t half_support,std::uint64_t symbol) {
        if(!plan.power_ramps) {
            if(plan.mandatory_ramps)throw Error("FHSS transmission blocked: "+plan.power_fallback_reason);
            return;
        }
        plan.validate();
        if(!rate||!half_support)throw Error("FHSS power ramps require finite pulse shaping");
        smooth=plan.smooth_ramps;
        period=static_cast<double>(rate*plan.dwell_seconds);
        quiet=std::ceil(static_cast<long double>(rate)*(plan.power_clock_region_seconds()+plan.audio_error_seconds/2));
        lag_center=static_cast<long double>(rate)*plan.audio_error_seconds/2;
        support=smooth?std::ceil(static_cast<long double>(rate)*plan.ramp_seconds):half_support;
        if(smooth && (!(plan.ramp_seconds>0)||!std::isfinite(plan.ramp_seconds)))
            throw Error("invalid FHSS smooth ramp duration");
        if(!std::isfinite(period)||2*affected_radius()>=period)
            throw Error("FHSS power guard and pulse support do not fit the dwell");
        if(!smooth && !plan.defer_symbols && !plan.matched_gaps && 2*affected_radius()>=symbol)
            throw Error("FHSS power guard could erase a complete symbol; reduce clock/audio/region uncertainty");
        const auto gap=2*affected_radius();
        const auto excluded=std::floor(symbol/period)*gap+std::min(gap,std::fmod(symbol,period));
        // Conservative geometry gate, not a sensitivity certificate. Preserve
        // >=75% ordinary statistical power support in EVERY symbol phase.
        // Wider gaps have lost clean bits with the unchanged private receiver.
        if(!smooth && !plan.defer_symbols && !plan.matched_gaps && excluded>symbol*.25L)
            throw Error("FHSS gaps/transitions exceed one quarter of a symbol");
        enabled=true;
    }
    long double coefficient_radius() const {return quiet+support;}
    long double affected_radius() const {return quiet+(smooth?1:2)*support;}
    // C2 smoothstep: amplitude and its first two derivatives are continuous at
    // both ends. Includes exactly zero through the protected hop interval.
    double gain(std::uint64_t epoch,long double sample,std::uint32_t rate) const {
        if(!enabled||!smooth)return 1.;
        const auto two=2.L*rate;
        auto local=std::fmod((epoch%2)*static_cast<long double>(rate)+sample+lag_center,two);
        if(local<0)local+=two;
        const auto distance=local-std::floor(local*5/two)*two/5;
        const auto edge=std::min(distance,two/5-distance)-quiet;
        if(edge<=0)return 0.;
        if(edge>=support)return 1.;
        const auto t=static_cast<double>(edge/support);
        return std::clamp(t*t*t*(10+t*(-15+6*t)),0.,1.);
    }
    // Expected retained power for a stationary input envelope. Finite private
    // symbols vary; this is geometry, not a measured link budget guarantee.
    double mean_power() const {
        if(!enabled)return 1.;
        if(!smooth)return static_cast<double>(1-2*coefficient_radius()/period);
        // Integral_0^1 smootherstep(t)^2 dt = 181/462.
        return static_cast<double>((period-2*quiet-2*support+2*support*(181.L/462))/period);
    }
    // Minimum integral g^2 over ANY interval of this duration, divided by its
    // duration. A flat-power geometry bound, NOT a bound for every random
    // private waveform. Deferred symbols reserve their complete scored interval
    // in g=1; long-symbol actual weighted losses are measured separately.
    double minimum_flat_power_fraction(long double duration) const {
        if(!enabled)return 1.;
        if(!smooth||!(duration>0))return 0.;
        const auto whole=std::floor(duration/period),rem=duration-whole*period;
        const auto x=std::clamp((rem/2-quiet)/support,0.L,1.L);
        const auto integral=std::pow(x,7)*(100.L/7+x*(-37.5L+x*(115.L/3+x*(-18.L+x*36.L/11))));
        const auto retained=whole*period*mean_power()+2*support*integral+
            std::max(0.L,rem-2*quiet-2*support);
        return std::clamp(static_cast<double>(retained/duration),0.,1.);
    }
    // Conservative, phase-independent span containing the requested amount of
    // full statistical cover, EXCLUDING every hop gap and pulse transition.
    // It cannot restore continuous masking through a real hop's quiet interval.
    std::uint64_t protected_span(std::uint64_t useful) const {
        if(!enabled)return useful;
        const auto gap=2*affected_radius(),good=period-gap;
        const auto span=std::ceil(useful+gap*std::ceil(useful/good));
        if(!std::isfinite(span)||span>=std::ldexp(1.L,64))throw Error("FHSS protected cover duration overflow");
        return static_cast<std::uint64_t>(span);
    }
};
inline bool fhss_utc_quiet(std::uint64_t epoch,long double sample,std::uint32_t rate,
        long double radius,long double lag_center) {
    if(!rate||!std::isfinite(sample)||!std::isfinite(radius)||radius<0)
        throw Error("invalid FHSS UTC power coordinate");
    const auto two=2.L*rate;
    auto local=std::fmod((epoch%2)*static_cast<long double>(rate)+sample+lag_center,two);
    if(local<0)local+=two;
    const auto cell=std::floor(local*5/two),distance=local-cell*two/5;
    return distance<=radius || two/5-distance<=radius;
}
namespace fhss_detail {
// Separate deterministic draw kernel for independent boundary/rejection tests.
inline std::size_t permutation(unsigned channels,std::span<const std::uint8_t> bytes,
        std::array<unsigned,FhssPlan::capacity>& next) {
    if(channels<2||channels>FhssPlan::capacity)throw Error("invalid FHSS channel count");
    for(unsigned i=0;i<channels;++i)next[i]=i;
    std::size_t used=0;
    for(unsigned i=channels-1;i>0;--i) {
        const std::uint64_t bound=i+1;
        const auto limit=(std::uint64_t{1}<<32)/bound*bound;
        std::uint32_t word;
        do {
            if(used+4>bytes.size())throw Error("FHSS permutation draw budget exhausted");
            word=0;for(unsigned j=0;j<4;++j)word=(word<<8)|bytes[used++];
        } while(word>=limit); // Unbiased integer selection, not modulo bias.
        std::swap(next[i],next[word%bound]);
    }
    return used;
}
}
class FhssSchedule {
public:
    std::shared_ptr<const FhssPlan> plan;
    std::array<unsigned,FhssPlan::capacity> order{};
    std::array<std::uint8_t,32> prefix{};
    std::size_t prefix_size=0;
    std::uint64_t epoch=0,cycle=0,byte_offset=0;
    long double source_origin_samples=0; // Relative to epoch; set once before generation.
    std::uint32_t source_rate=0;
    std::size_t consumed_bytes=0;
    // Each cycle owns a disjoint region, including bounded rejection reserve.
    // No counter wrap or fallback to an earlier order is permitted.
    static constexpr std::uint64_t cycle_bytes=1024;
    static constexpr std::uint64_t maximum_cycle=std::numeric_limits<std::uint64_t>::max()/cycle_bytes;
    FhssSchedule(std::shared_ptr<const FhssPlan> value,const Crypto& key,std::uint64_t start):
        plan(std::move(value)),epoch(start),key_(key) {
        if(!plan)throw Error("missing FHSS tuning plan");
        plan->validate();
        select_cycle(0);
    }
    // Generation-relative sample coordinate, including surrounding noise.
    // Absolute UTC scheduling/driver latency belong to the playback adapter.
    std::uint64_t ordinal(std::uint64_t sample,std::uint32_t rate) const {
        if(plan->utc_schedule())return utc_ordinal(sample,rate).value_or(0);
        // Round the configured dwell to its double sample-domain value once.
        // Dividing seconds first can place exact integer boundaries a sample late.
        const double dwell_samples=rate*plan->dwell_seconds;
        if(!rate || !std::isfinite(dwell_samples) || dwell_samples<=0)throw Error("invalid FHSS sample timeline");
        const auto value=std::floor((static_cast<long double>(sample)+(plan->utc_schedule()?source_origin_samples:0))/dwell_samples);
        if(value<0)return 0;
        if(!rate || !std::isfinite(value) || value>=std::ldexp(1.L,64))throw Error("FHSS sample timeline overflow");
        return static_cast<std::uint64_t>(value);
    }
    // Nominal RECEIVING RF schedule at the same elapsed wall time. Propagation
    // offsets may span many complete permutations; always retain the TX epoch,
    // ordinal and key domain. No new stream is seeded at epoch+offset. This is
    // a preview/adapter coordinate, never a receiver admission/absence gate.
    std::optional<std::uint64_t> arrival_ordinal(long double sample,std::uint32_t rate) const {
        if(!rate||!std::isfinite(sample)||sample<0)throw Error("invalid FHSS arrival timeline");
        const auto delayed=sample-static_cast<long double>(rate)*plan->offset_seconds;
        if(delayed<0)return std::nullopt;
        if(plan->utc_schedule())return utc_ordinal(delayed,rate);
        const double dwell_samples=rate*plan->dwell_seconds;
        if(!std::isfinite(dwell_samples)||dwell_samples<=0)throw Error("invalid FHSS arrival dwell");
        const auto hop=std::floor(delayed/dwell_samples);
        if(!std::isfinite(hop)||hop>=std::ldexp(1.L,64))throw Error("FHSS arrival timeline overflow");
        return static_cast<std::uint64_t>(hop);
    }
    unsigned channel(std::uint64_t hop) {
        const auto wanted=hop/plan->channels;
        if(wanted!=cycle)select_cycle(wanted);
        return order[hop%plan->channels];
    }
    bool changes_at(std::uint64_t hop) {
        if(plan->utc_schedule())return true;
        if(!hop)return false;
        if(hop%plan->channels)return true; // distinct entries within a permutation
        if(cached_boundary_!=hop) {
            const auto before=channel(hop-1),after=channel(hop);
            cached_change_=before!=after;cached_boundary_=hop;
        }
        return cached_change_;
    }
    bool near_change(long double sample,long double radius,const FhssPowerShape& shape) {
        if(!shape.enabled)return false;
        if(!std::isfinite(sample)||!std::isfinite(radius)||radius<0)
            throw Error("invalid FHSS power coordinate");
        // Source power must be quiet BEFORE a nominal tuner hop: presentation
        // lag lies in [0,A]. With relative endpoint clock+region bound B, the
        // source gap is [N-A-B,N+B]. RX retunes at N+offset, so fixed offset
        // cancels from this guard. Capture audio is AFTER the RF selector and
        // is not a second transmit lag. Radio-control latency remains unqualified.
        if(plan->utc_schedule()) {
            if(!source_rate)throw Error("FHSS UTC source rate unavailable");
            // Reduce before forming the fractional .4-second boundary. Never
            // divide a large epoch by rounded double(fs*.4).
            return fhss_utc_quiet(epoch,source_origin_samples+sample,source_rate,radius,shape.lag_center);
        }
        sample+=shape.lag_center;
        if(sample<0)return false;
        const auto value=std::floor(sample/shape.period);
        if(value<0||value>=std::ldexp(1.L,64)-1)throw Error("FHSS power timeline overflow");
        const auto hop=static_cast<std::uint64_t>(value);
        return (sample-value*shape.period<=radius&&changes_at(hop)) ||
               ((value+1)*shape.period-sample<=radius&&changes_at(hop+1));
    }
private:
    std::optional<std::uint64_t> utc_ordinal(long double sample,std::uint32_t rate) const {
        if(!rate)throw Error("invalid FHSS UTC sample rate");
        const auto local=(epoch%2)*static_cast<long double>(rate)+source_origin_samples+sample;
        const auto two=2.L*rate,cycles=std::floor(local/two),remainder=local-cycles*two;
        const auto value=static_cast<long double>(epoch/2)*5+cycles*5+std::floor(remainder*5/two);
        if(value<0)return std::nullopt;
        if(!std::isfinite(value)||value>=std::ldexp(1.L,64))throw Error("FHSS UTC stream exhausted");
        return static_cast<std::uint64_t>(value);
    }
    Crypto key_;
    std::uint64_t cached_boundary_=0;
    bool cached_change_=false;
    void select_cycle(std::uint64_t wanted) {
        if(wanted>maximum_cycle)throw Error("FHSS cycle stream exhausted");
        const auto offset=wanted*cycle_bytes;
        auto bytes=key_.stream(StreamPurpose::Fhss,plan->utc_schedule()?0:epoch,offset,cycle_bytes,
            plan->utc_schedule()?StreamDomain::FhssUtcV3:StreamDomain::FhssCycleV2);
        std::array<unsigned,FhssPlan::capacity> next{};
        std::size_t used;
        try { used=fhss_detail::permutation(plan->channels,bytes,next); }
        catch(...) { std::fill(bytes.begin(),bytes.end(),0);throw; }
        // Commit only after a complete successful construction. The single cache
        // is valid solely for this immutable key/epoch/plan and exact cycle.
        // Seeking never generates all intervening cycles or retains their bytes.
        order=next;cycle=wanted;byte_offset=offset;consumed_bytes=used;
        prefix_size=std::min(prefix.size(),used);
        std::copy_n(bytes.begin(),prefix_size,prefix.begin());
        std::fill(bytes.begin(),bytes.end(),0);
    }
};
}
