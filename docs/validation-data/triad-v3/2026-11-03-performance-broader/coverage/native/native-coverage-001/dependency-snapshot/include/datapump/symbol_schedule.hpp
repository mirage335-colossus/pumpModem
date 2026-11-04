#pragma once

#include "datapump/types.hpp"
#include "datapump/fhss_schedule.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/sched_hop.hpp"
#include "datapump/scheduled_symbol_calendar.hpp"
#include "datapump/symbol_burst.hpp"

#include <cstdint>
#include <limits>
#include <numeric>
#include <utility>

namespace datapump::modem {

// A symbol keeps the epoch selected at its first payload sample for its entire
// duration. The ordinal distinguishes symbols beginning in the same second;
// it is recoverable from timing without receiving earlier symbols.
struct SymbolStreamAddress {
    std::uint64_t epoch = 0;
    std::uint64_t ordinal = 0;
    std::uint32_t sample_in_second = 0;
};

// A public waveform has no UTC-dependent source or power calendar. Its first
// complete symbol can use the ordinary rolling search; that accepted physical
// start anchors the constant (J=0) Burst spacing. Finite amplitude searches
// retain their separate arrival windows even for these configurations.
inline bool symbol_burst_public_rolling_search(const Config& config) {
    return config.symbol_burst.enabled() && !config.scramble && !config.dsss &&
        !config.data_key && !config.fake_hopping && !config.scheduled_hopping;
}

// Symbol_Burst binds its complete finite support to the UTC half-minute grid.
// The payload begins P samples later, so a keyed payload phase is not generally
// zero and cannot be enumerated with the ordinary contiguous-symbol gcd grid.
inline std::optional<std::uint64_t> symbol_burst_payload_start_in_epoch(
        const Config& config,std::uint64_t epoch) {
    if(!config.symbol_burst.enabled())return std::nullopt;
    if(!config.sample_rate)throw Error("invalid Symbol_Burst sample rate");
    const auto padding=pattern_pulse_padding_samples(config);
    const auto seconds=padding/config.sample_rate;
    if(epoch<seconds || (epoch-seconds)%30)return std::nullopt;
    return padding%config.sample_rate;
}

template<class Consume>
inline void for_each_symbol_burst_phase(const Config& config,std::uint64_t epoch,Consume&& consume) {
    if(const auto phase=symbol_burst_payload_start_in_epoch(config,epoch))
        consume(*phase);
}

namespace symbol_schedule_detail {
inline std::uint64_t add(std::uint64_t a, std::uint64_t b) {
    if (b > std::numeric_limits<std::uint64_t>::max() - a)
        throw Error("symbol stream epoch or address would overflow");
    return a + b;
}
inline std::uint64_t multiply(std::uint64_t a, std::uint64_t b) {
    if (b && a > std::numeric_limits<std::uint64_t>::max() / b)
        throw Error("symbol stream epoch or address would overflow");
    return a * b;
}
} // namespace symbol_schedule_detail

// The first symbol begins phase_samples after epoch. Sample coordinates, not
// buffer-generation wall time, determine all later epochs. Splitting products
// around sample_rate avoids overflowing symbol_index * symbol_samples when the
// resulting epoch still fits in 64 bits, without nonportable wide integers.
inline SymbolStreamAddress symbol_stream_address(std::uint64_t epoch,
        std::uint64_t phase_samples, std::uint64_t symbol_index,
        std::uint64_t symbol_samples, std::uint32_t sample_rate) {
    if (!sample_rate || !symbol_samples || phase_samples >= sample_rate)
        throw Error("invalid symbol stream timing coordinate");
    using symbol_schedule_detail::add;
    using symbol_schedule_detail::multiply;
    const auto whole = symbol_samples / sample_rate;
    const auto remainder = symbol_samples % sample_rate;
    auto seconds = add(multiply(symbol_index, whole),
                       multiply(symbol_index / sample_rate, remainder));
    // Both remainders are below a uint32 sample rate, so this expression fits.
    const auto fractional = (symbol_index % sample_rate) * remainder + phase_samples;
    seconds = add(seconds, fractional / sample_rate);
    const auto sample = static_cast<std::uint32_t>(fractional % sample_rate);
    return {add(epoch, seconds), sample / symbol_samples, sample};
}

// Versioned whole-symbol calendar. Current public profiles use exactly 2/5 s.
// Reserve every boundary, even if two keyed channels happen to coincide. The
// minimum dwell capacity covers all five fractional-sample boundary residues.
// No previous bits, PCM buffering or iteration over elapsed hops is required.
class FhssSymbolCalendar {
public:
    std::uint64_t rate=0, symbol=0, left=0, right=0, per_hop=0;
    FhssSymbolCalendar(const FhssPlan& plan,std::uint32_t fs,std::uint64_t half_support,std::uint64_t samples):
        rate(fs),symbol(samples) {
        if(plan.dwell_seconds!=.4 || !fs || !samples || !half_support)
            throw Error("deferred FHSS requires shaped symbols and a 0.4 s hop calendar");
        const auto b=plan.power_clock_region_seconds();
        if(!std::isfinite(b)||b<0||b>65536||!std::isfinite(plan.audio_error_seconds)||
           plan.audio_error_seconds<0||plan.audio_error_seconds>60)throw Error("invalid FHSS calendar allowance");
        const auto lag=static_cast<long double>(fs)*plan.audio_error_seconds/2;
        const auto quiet=std::ceil(static_cast<long double>(fs)*(b+plan.audio_error_seconds/2));
        const auto ramp=plan.smooth_ramps?static_cast<std::uint64_t>(std::ceil(fs*plan.ramp_seconds)):0;
        // A post-shaper envelope can suppress the outside-symbol tails. Keep
        // the entire scoring interval at unity; reserving the original long
        // pulse support here would unnecessarily prohibit short bits. The
        // diagnostic coefficient-omission construction still needs that drain.
        const auto drain=plan.smooth_ramps?0:half_support;
        left=static_cast<std::uint64_t>(std::ceil(quiet-lag))+drain+ramp;
        right=static_cast<std::uint64_t>(std::ceil(quiet+lag))+drain+ramp;
        auto shortest=2ULL*fs/5;
        for(unsigned h=0;h<5;++h)shortest=std::min<std::uint64_t>(shortest,boundary(h+1)-boundary(h,true));
        if(shortest<=left+right || (per_hop=(shortest-left-right)/symbol)==0)
            throw Error("no complete shaped symbol fits between FHSS guards; change dwell, symbol duration or timing allowances");
    }
    std::uint64_t boundary(std::uint64_t hop,bool up=false) const {
        using namespace symbol_schedule_detail;
        // floor/ceil(2*fs*hop/5), avoiding a large intermediate product.
        const auto whole=multiply(hop/5,2*rate);
        const auto fraction=(hop%5)*2*rate;
        return add(whole,(fraction+(up?4:0))/5);
    }
    std::uint64_t slot(std::uint64_t rank) const {
        using namespace symbol_schedule_detail;
        return add(add(boundary(rank/per_hop,true),left),multiply(rank%per_hop,symbol));
    }
    std::uint64_t first_rank(std::uint64_t epoch,std::uint64_t phase) const {
        using namespace symbol_schedule_detail;
        if(phase>=rate)throw Error("invalid FHSS source phase");
        const auto tick=add(multiply(epoch,rate),phase);
        // This integer-sample inverse can be one dwell low at a rounded edge;
        // the following comparison deliberately advances to the next slot.
        const auto hop=add(multiply(tick/(2*rate),5),(tick%(2*rate))*5/(2*rate));
        const auto begin=add(boundary(hop,true),left);
        const auto within=tick<=begin?0:1+(tick-begin-1)/symbol;
        return add(multiply(hop,per_hop),std::min(within,per_hop));
    }
    std::uint64_t offset(std::uint64_t epoch,std::uint64_t phase,std::uint64_t index) const {
        using namespace symbol_schedule_detail;
        return slot(add(first_rank(epoch,phase),index))-add(multiply(epoch,rate),phase);
    }
    // First slot whose start is >= the supplied relative sample coordinate.
    // Constant-time inverse; independent of elapsed airtime.
    std::uint64_t lower_bound(std::uint64_t epoch,std::uint64_t phase,std::uint64_t sample) const {
        using namespace symbol_schedule_detail;
        const auto tick=add(add(multiply(epoch,rate),phase),sample);
        const auto rank=first_rank(tick/rate,tick%rate),first=first_rank(epoch,phase);
        return rank>first?rank-first:0;
    }
};

inline bool scheduled_bursts(const Config& config) {
    return config.scheduled_hopping && config.scheduled_hopping->enabled() && !config.scheduled_hopping->continuous();
}
// A paused source coordinate is not periodic under a physical T-sample
// shift: null plateaus break the ordinary phase/index equivalence. Every
// physical payload start in its UTC second must remain eligible, even when
// a scheduled symbol is shorter than one second. Active/keyed pruning is a
// separate operation; continuous and ordinary profiles keep their old extent.
inline std::uint64_t canonical_phase_extent(const Config& config) {
    if(config.symbol_burst.enabled())return config.sample_rate;
    return scheduled_bursts(config)?static_cast<std::uint64_t>(config.sample_rate):
        std::min(symbol_sample_count(config),static_cast<std::uint64_t>(config.sample_rate));
}
inline bool deferred_symbols(const Config& config) {
    // A split-source schedule owns physical placement; the ordinary FHSS
    // whole-symbol calendar applies only without scheduled null intervals.
    return !scheduled_bursts(config) && config.fake_hopping && config.fake_hopping->defer_symbols;
}
// Packed starts are exact keyed singleton source slots. Acquisition and its
// planner enumerate the same actual UTC phases, including empty null epochs;
// neither builds an all-Fs grid merely because canonical_phase_step is one.
inline std::uint64_t packed_scheduled_phase_count(const Config& config,std::uint64_t epoch) {
    if(!packed_scheduled_symbols(config))return 0;
    const ScheduledSymbolCalendar calendar(config);
    return calendar.first_rank(symbol_schedule_detail::add(epoch,1),0)-calendar.first_rank(epoch,0);
}
template<class Consume>
inline void for_each_packed_scheduled_phase(const Config& config,std::uint64_t epoch,Consume&& consume) {
    if(!packed_scheduled_symbols(config))return;
    const ScheduledSymbolCalendar calendar(config);
    calendar.for_each_phase_in_epoch(epoch,std::forward<Consume>(consume));
}
// UTC pulse omissions distinguish sample phases that previously shared the
// same private epoch/ordinal. Arrival-window pruning may exclude phases; the
// cipher-address gcd alone may not. The iterator does not allocate this grid.
inline std::uint64_t canonical_phase_step(const Config& config) {
    // There is one exact payload phase per eligible half-minute epoch. Consumers
    // use symbol_burst_payload_start_in_epoch rather than assuming its phase=0.
    if(config.symbol_burst.enabled())return config.sample_rate;
    if(scheduled_bursts(config)) {
        if(packed_scheduled_symbols(config))return 1;
        if(config.fake_hopping&&config.fake_hopping->matched_gaps)return 1;
        const auto floor_hop=2ULL*config.sample_rate/5;
        const auto ceil_hop=(2ULL*config.sample_rate+4)/5;
        // Every physical UTC second, active-group edge and symbol boundary is
        // a multiple of this cell. Fractional hop residues reduce it to one.
        return std::gcd(std::gcd(symbol_sample_count(config),static_cast<std::uint64_t>(config.sample_rate)),
            std::gcd(floor_hop,ceil_hop));
    }
    if(config.fake_hopping && (config.fake_hopping->defer_symbols||config.fake_hopping->matched_gaps))return 1;
    return std::gcd(symbol_sample_count(config),static_cast<std::uint64_t>(config.sample_rate));
}
// A canonical cipher phase represents a CELL of physical source phases. A
// qualified arrival prior must intersect that cell, not only its representative
// point. This shares the exact templates without discarding fractional starts.
inline std::uint64_t canonical_phase_cell_upper(const Config& config,std::uint64_t phase) {
    if(config.symbol_burst.enabled())return phase;
    if(packed_scheduled_symbols(config))return phase;
    const auto limit=canonical_phase_extent(config)-1;
    return phase+std::min(canonical_phase_step(config)-1,limit-std::min(phase,limit));
}
inline std::uint64_t scheduled_symbol_offset(const Config& config,std::uint64_t epoch,
        std::uint64_t phase,std::uint64_t index) {
    const auto samples=symbol_sample_count(config);
    if(config.symbol_burst.enabled()) {
        const SymbolBurstPlan plan(config);
        const auto first=plan.bind_payload(epoch,phase);
        SymbolBurstCursor cursor(plan,first);
        // Compatibility random access is deliberately linear. Streaming PCM,
        // masking and receiver continuations retain their own bounded cursor.
        return plan.distance(first.payload_begin,cursor.at(index).payload_begin);
    }
    if(scheduled_bursts(config)) {
        if(!config.data_key)throw Error("scheduled hopping requires an encryption key");
        if(packed_scheduled_symbols(config)) {
            const ScheduledSymbolCalendar calendar(config);
            const auto physical=calendar.physical_slot(symbol_schedule_detail::add(calendar.first_rank(epoch,phase),index));
            const auto anchor=symbol_schedule_detail::add(symbol_schedule_detail::multiply(epoch,config.sample_rate),phase);
            if(physical<anchor)throw Error("invalid packed scheduled symbol origin");
            return physical-anchor;
        }
        const SchedHopSourceMap map(*config.scheduled_hopping,*config.data_key,config.sample_rate);
        const auto anchor=symbol_schedule_detail::add(symbol_schedule_detail::multiply(epoch,config.sample_rate),phase);
        const auto origin=map.locate(anchor).source;
        const auto physical=map.source_to_physical(origin.add_samples(symbol_schedule_detail::multiply(index,samples)));
        if(physical.fraction!=0 || physical.sample<anchor)throw Error("invalid scheduled symbol origin");
        return physical.sample-anchor;
    }
    if(!deferred_symbols(config))return symbol_schedule_detail::multiply(index,samples);
    return FhssSymbolCalendar(*config.fake_hopping,config.sample_rate,
        pattern_pulse_padding_samples(config),samples).offset(epoch,phase,index);
}
inline SymbolStreamAddress symbol_stream_address(const Config& config,std::uint64_t epoch,
        std::uint64_t phase,std::uint64_t index) {
    if(config.symbol_burst.enabled()) {
        const SymbolBurstPlan plan(config);
        SymbolBurstCursor cursor(plan,plan.bind_payload(epoch,phase));
        const auto payload=cursor.at(index).payload_begin;
        return {payload.epoch,payload.phase/plan.symbol_samples(),payload.phase};
    }
    if(!scheduled_bursts(config)&&!deferred_symbols(config))return symbol_stream_address(epoch,phase,index,
        symbol_sample_count(config),config.sample_rate);
    const auto offset=scheduled_symbol_offset(config,epoch,phase,index);
    const auto total=symbol_schedule_detail::add(offset,phase);
    const auto sample=static_cast<std::uint32_t>(total%config.sample_rate);
    return {symbol_schedule_detail::add(epoch,total/config.sample_rate),sample/symbol_sample_count(config),sample};
}

// Re-resolve after a rate/target/profile change without substituting local
// receive timestamp uncertainty for the agreed waveform guard parameters.
inline std::shared_ptr<const FhssPlan> resolve_fhss_pulse_power(const FhssPlan& source,const Config& config) {
    auto plan=std::make_shared<FhssPlan>(source);plan->validate();
    plan->power_ramps=true;plan->mandatory_ramps=true;plan->defer_symbols=false;
    plan->matched_gaps=false;plan->smooth_ramps=false;plan->remote_guard_covered=true;plan->ramp_seconds=0;plan->power_fallback_reason.clear();
    try {
        FhssPowerShape shape(*plan,config.sample_rate,pattern_pulse_padding_samples(config),symbol_sample_count(config));
        plan->matched_gaps=true;
    }
    catch(const Error& error) {
        try {
            FhssSymbolCalendar calendar(*plan,config.sample_rate,pattern_pulse_padding_samples(config),symbol_sample_count(config));
            plan->defer_symbols=true;
            FhssPowerShape shape(*plan,config.sample_rate,pattern_pulse_padding_samples(config),symbol_sample_count(config));
        } catch(const Error&) {
            plan->defer_symbols=false;plan->matched_gaps=true;
            try {
                const FhssPowerShape shape(*plan,config.sample_rate,pattern_pulse_padding_samples(config),symbol_sample_count(config));
                const auto chip=pattern_chip_samples(config);
                // Keep a full statistical cover interval and at least one
                // retained coefficient for every symbol phase. This bound is
                // conservative; it is not a sensitivity certificate.
                if(shape.period-2*shape.coefficient_radius()<=chip ||
                   symbol_sample_count(config)<=2*shape.coefficient_radius()+2*chip)
                    throw Error("FHSS guard can erase every pulse of a symbol; reduce uncertainty or change symbol/dwell geometry");
            } catch(const Error& gap_error) {
                plan->matched_gaps=false;plan->power_ramps=false;
                plan->power_fallback_reason=std::string(gap_error.what())+"; transmission requires safe nonzero pulse support between hops";
            }
        }
    }
    return plan;
}
// Production envelope V1. A bounded smooth gate keeps local hop silence even
// when the original finite pulse is seconds long. It broadens very narrow
// spectra; 200 Hz is the user-selected ramp bandwidth allowance, not a legal
// mask or a promise that all sidebands stop at 200 Hz. No crypto coordinate or
// receiver hypothesis changes. Legacy exact pulse gaps remain diagnostic above.
inline std::shared_ptr<const FhssPlan> resolve_fhss_power(const FhssPlan& source,const Config& config) {
    auto plan=std::make_shared<FhssPlan>(source);plan->validate();
    plan->power_ramps=plan->mandatory_ramps=plan->smooth_ramps=true;
    plan->defer_symbols=plan->matched_gaps=false;plan->remote_guard_covered=true;
    plan->power_fallback_reason.clear();
    const auto bandwidth=config.bandwidth_hz*std::max(1U,config.dsss_factor);
    plan->ramp_seconds=2/std::max(200.,bandwidth);
    const auto fs=config.sample_rate;const auto symbol=symbol_sample_count(config);
    const auto support=pattern_pulse_padding_samples(config);
    try {
        // A propagation region can exceed an entire dwell (EME). It cannot
        // justify emitting through the LOCAL hop or prohibit otherwise safe
        // Fake audio. Keep local lag/clock silence; explicitly report that a
        // future single-channel tuner has no guaranteed remote overlap.
        if(2*std::ceil(static_cast<long double>(fs)*(plan->relative_clock_region_seconds()+plan->audio_error_seconds/2))+
           2*std::ceil(static_cast<long double>(fs)*plan->ramp_seconds)>=fs*plan->dwell_seconds)
            plan->remote_guard_covered=false;
        FhssPowerShape shape(*plan,fs,support,symbol);
        // Reserve complete short bits where inexpensive; long bits retain their
        // source clock and span gaps. No whole-bit PCM buffer or repeated bits.
        if(symbol<shape.period && !config.symbol_burst.enabled()) {
            try {FhssSymbolCalendar calendar(*plan,fs,support,symbol);plan->defer_symbols=true;}
            catch(const Error&) {
                // Deferral cannot create a longer slot than a dwell contains.
                // Never admit a mostly-muted short scoring interval. This is a
                // flat-power geometry screen; arbitrary keyed energy is not
                // claimed to have a deterministic lower bound.
                if(shape.minimum_flat_power_fraction(symbol)<.5)
                    throw Error("FHSS cannot retain half a short symbol's usable interval; reduce clock/audio/region uncertainty");
            }
        }
        if(config.symbol_burst.enabled() && symbol<shape.period && shape.minimum_flat_power_fraction(symbol)<.5)
            throw Error("FHSS cannot retain half a short symbol's usable interval; reduce clock/audio/region uncertainty");
        if(!plan->remote_guard_covered)plan->power_fallback_reason=
            "Local hop protected; remote timing uncertainty spans the dwell (future radio overlap not guaranteed)";
    } catch(const Error& e) {
        plan->power_ramps=false;plan->power_fallback_reason=e.what();
    }
    return plan;
}
inline std::uint64_t deferred_initial_symbols(const Config& config) {
    if(!deferred_symbols(config))return 4;
    const FhssSymbolCalendar calendar(*config.fake_hopping,config.sample_rate,
        pattern_pulse_padding_samples(config),symbol_sample_count(config));
    return std::max<std::uint64_t>(4,calendar.lower_bound(config.stream_epoch,0,config.sample_rate));
}

inline std::uint64_t symbol_stream_chip(const SymbolStreamAddress& address,
        std::uint64_t chips_per_symbol, std::uint64_t chip_in_symbol) {
    if (!chips_per_symbol || chip_in_symbol >= chips_per_symbol)
        throw Error("invalid symbol stream chip coordinate");
    return symbol_schedule_detail::add(
        symbol_schedule_detail::multiply(address.ordinal, chips_per_symbol), chip_in_symbol);
}

} // namespace datapump::modem
