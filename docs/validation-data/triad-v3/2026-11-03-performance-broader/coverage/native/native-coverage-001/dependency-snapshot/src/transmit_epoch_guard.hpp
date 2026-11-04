#pragma once

#include "datapump/pattern_pulse.hpp"
#include "datapump/symbol_schedule.hpp"
#include "datapump/sched_hop.hpp"
#include "datapump/scheduled_symbol_calendar.hpp"
#include <algorithm>

namespace datapump::detail {
// Account for every symbol that can contribute to the generated prefix. The
// pulse support reaches padding samples before a symbol's nominal start; its
// leading padding therefore cancels here. Reserving a zero-valued edge sample
// is conservative. Never infer exposure from the nominal payload cursor alone.
inline std::uint64_t exposed_transmit_epoch(const modem::Config& config,
        std::uint64_t epoch, std::uint64_t total_samples, std::uint64_t emitted) {
    if(modem::scheduled_bursts(config))
        throw Error("scheduled key exposure requires logical source sample counts");
    const auto training=modem::training_sample_count(config);
    const auto padding=modem::pattern_pulse_padding_samples(config);
    const auto overhead=training+2*padding+modem::suppression_sample_count(config);
    const auto symbol=modem::symbol_sample_count(config);
    if(total_samples<=overhead || emitted<=training)return epoch;
    std::optional<modem::FhssSymbolCalendar> calendar;
    if(modem::deferred_symbols(config))calendar.emplace(*config.fake_hopping,config.sample_rate,padding,symbol);
    const auto symbols=calendar?calendar->lower_bound(epoch,config.stream_phase_samples,total_samples-overhead):
        (total_samples-overhead)/symbol;
    if(!symbols)return epoch;
    const auto exposed=calendar?calendar->lower_bound(epoch,config.stream_phase_samples,emitted-training):
        1+(emitted-training-1)/symbol;
    if(!exposed)return epoch;
    const auto last=std::min(symbols-1,exposed-1);
    return modem::symbol_stream_address(config,epoch,config.stream_phase_samples,last).epoch;
}

// Split scheduled bursts have two clocks: physical output includes null hops,
// while coefficients and pulse support advance only on the compact source
// clock. The transmitter supplies these ORIGINAL logical extents explicitly;
// subtracting a virtual cover duration from a wall-clock total is incorrect.
// A cancellation's valid endpoint prevents independent cover from reserving
// additional payload epochs, while the pulse lookahead remains conservative.
inline std::uint64_t exposed_transmit_source_epoch(const modem::Config& config,
        std::uint64_t epoch, std::uint64_t original_source_total,
        std::uint64_t source_emitted, std::uint64_t source_valid_payload_end) {
    const auto training=modem::training_sample_count(config);
    const auto padding=modem::pattern_pulse_padding_samples(config);
    const auto overhead=modem::sched_hop_detail::add(modem::sched_hop_detail::add(training,
        modem::sched_hop_detail::multiply(2,padding)),modem::suppression_sample_count(config));
    const auto symbol=modem::symbol_sample_count(config);
    const auto emitted=std::min(source_emitted,source_valid_payload_end);
    if(original_source_total<=overhead || emitted<=training)return epoch;
    if(modem::packed_scheduled_symbols(config)) {
        const modem::ScheduledSymbolCalendar calendar(config);
        const auto symbols=calendar.lower_bound_source(epoch,config.stream_phase_samples,original_source_total-overhead);
        const auto exposed=calendar.lower_bound_source(epoch,config.stream_phase_samples,emitted-training);
        if(!symbols || !exposed)return epoch;
        return modem::symbol_stream_address(config,epoch,config.stream_phase_samples,
            std::min(symbols-1,exposed-1)).epoch;
    }
    const bool split=config.scheduled_hopping && config.scheduled_hopping->enabled() &&
        !config.scheduled_hopping->continuous();
    std::optional<modem::FhssSymbolCalendar> calendar;
    if(!split && modem::deferred_symbols(config))
        calendar.emplace(*config.fake_hopping,config.sample_rate,padding,symbol);
    const auto symbols=calendar?calendar->lower_bound(epoch,config.stream_phase_samples,original_source_total-overhead):
        (original_source_total-overhead)/symbol;
    if(!symbols)return epoch;
    const auto exposed=calendar?calendar->lower_bound(epoch,config.stream_phase_samples,emitted-training):
        1+(emitted-training-1)/symbol;
    if(!exposed)return epoch;
    const auto last=std::min(symbols-1,exposed-1);
    return modem::symbol_stream_address(config,epoch,config.stream_phase_samples,last).epoch;
}

// Ordinary automatic payloads start on a whole second after their prefix. A
// scheduled payload uses the physical inverse of that logical prefix instead.
// Waiting is conservative about preparation lead; no receive timestamp chooses it.
inline double transmit_epoch_lock_seconds(const modem::Config& config,
        std::uint64_t used_epoch, double now) {
    if(modem::scheduled_bursts(config)) {
        if(!config.data_key)throw Error("scheduled key lock requires its transmission key");
        if(!std::isfinite(now)||now<0)throw Error("invalid clock for scheduled key lock");
        const auto next=modem::sched_hop_detail::add(used_epoch,1);
        const auto tick=modem::sched_hop_detail::multiply(next,config.sample_rate);
        modem::SchedHopSourceMap map(*config.scheduled_hopping,*config.data_key,config.sample_rate);
        const auto prefix=modem::sched_hop_detail::add(modem::training_sample_count(config),
            modem::pattern_pulse_padding_samples(config));
        auto source=map.locate(tick).source.sample;
        if(modem::packed_scheduled_symbols(config)) {
            const modem::ScheduledSymbolCalendar calendar(config);
            source=calendar.source_slot(calendar.first_rank_source(source));
        }
        if(source<prefix)return 0;
        const auto output=map.source_to_physical({source-prefix});
        const auto origin=static_cast<long double>(output.sample/config.sample_rate)+
            static_cast<long double>(output.sample%config.sample_rate)/config.sample_rate;
        return static_cast<double>(std::max(0.L,origin-static_cast<long double>(now)));
    }
    const auto prefix=(static_cast<long double>(modem::training_sample_count(config))+
        modem::pattern_pulse_padding_samples(config))/config.sample_rate;
    const auto remaining=static_cast<long double>(used_epoch)+1-prefix-now;
    return static_cast<double>(std::max(0.L,remaining));
}
}
