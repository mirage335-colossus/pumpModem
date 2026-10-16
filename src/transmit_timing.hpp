#pragma once
#include "datapump/execution.hpp"
#include "datapump/audio.hpp"
#include "datapump/transfer.hpp"
#include "datapump/streaming_modem.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/sched_hop.hpp"
#include "datapump/scheduled_symbol_calendar.hpp"
#include "datapump/symbol_schedule.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <thread>
#include <type_traits>
#include <utility>

namespace datapump::detail {
struct ScheduledTransmission {
    std::unique_ptr<modem::StreamingTransmitter> transmitter;
    std::uint64_t epoch = 0;
    std::uint32_t phase_samples = 0;
    double playback_epoch = 0;
};

struct TransmitTimingProtection {
    double stop_seconds=0,independent_cover_seconds=0;
};
inline TransmitTimingProtection transmit_timing_protection(const modem::Config& config) {
    const auto stop=modem::transmit_stop_sample_count(config);
    TransmitTimingProtection result;
    result.stop_seconds=static_cast<double>(modem::scheduled_physical_span_samples(config,stop))/config.sample_rate;
    if(modem::private_cover_enabled(config)) {
        const auto cover=modem::suppression_sample_count(config);
        const auto valid=static_cast<double>(modem::scheduled_physical_span_samples(config,stop-cover))/config.sample_rate;
        result.independent_cover_seconds=modem::scheduled_bursts(config)?
            std::max(0.,result.stop_seconds-valid):static_cast<double>(cover)/config.sample_rate;
    }
    return result;
}
template<class Source>
void configure_transmit_timing_protection(audio::Options& output,const modem::Config& config,Source source) {
    const auto protection=transmit_timing_protection(config);
    output.timing_stop_duration_seconds=protection.stop_seconds;
    output.timing_independent_cover_seconds=protection.independent_cover_seconds;
    output.timing_protected_payload_end={};
    if(modem::scheduled_bursts(config)) {
        output.timing_protected_payload_end=[source=std::move(source)](std::uint64_t frontier)->std::optional<std::uint64_t> {
            const auto* transmitter=source();
            if(!transmitter)return std::nullopt;
            return transmitter->predicted_physical_valid_payload_end(frontier);
        };
    }
}

// Call after opening the output device, before requesting its first samples.
// The prefix precedes the whole-second payload origin. Preparation may take
// longer than anticipated, so retry with a later origin before emitting audio.
template<class Factory, class EpochClock>
ScheduledTransmission schedule_transmission(const modem::Config& config, Factory make,
        EpochClock clock, std::stop_token stop = {}, std::uint64_t minimum_epoch = 0,
        double delivery_margin_seconds = 0) {
    if (!std::isfinite(delivery_margin_seconds) || delivery_margin_seconds < 0)
        throw Error("invalid audio delivery margin");
    const auto make_at = [&](std::uint64_t epoch, std::uint32_t phase) {
        if constexpr (std::is_invocable_v<Factory&,std::uint64_t,std::uint32_t>)
            return make(epoch,phase);
        else {
            if (phase) throw Error("scheduled transmission factory does not accept a fractional-second origin");
            return make(epoch);
        }
    };
    if (modem::scheduled_bursts(config)) {
        if (!config.data_key) throw Error("scheduled transmission requires an encryption key");
        const auto rate=config.sample_rate;
        if (!rate) throw Error("invalid scheduled transmit sample rate");
        const auto prefix=modem::sched_hop_detail::add(modem::training_sample_count(config),
            modem::pattern_pulse_padding_samples(config));
        modem::SchedHopSourceMap map(*config.scheduled_hopping,*config.data_key,rate);
        std::optional<modem::ScheduledSymbolCalendar> calendar;
        if(modem::packed_scheduled_symbols(config))calendar.emplace(config);
        const auto minimum=modem::sched_hop_detail::multiply(minimum_epoch,rate);
        const auto minimum_source=map.locate(minimum).source.sample;
        double lead=delivery_margin_seconds+.05;
        for (unsigned attempt=0;attempt<8;++attempt) {
            if (stop.stop_requested()) throw Error("transmission cancelled");
            const auto now=clock();
            const auto seconds=std::floor(now);
            if (!std::isfinite(now) || now<0 || !std::isfinite(seconds) ||
                static_cast<long double>(seconds)>=std::ldexp(1.L,64))
                throw Error("transmit clock exceeds the scheduled sample range");
            // Keep the UTC integer separate from the subsecond lead. Multiplying
            // the complete wall-clock value would lose sample cells on hosts
            // whose long double is FP64.
            const auto base=modem::sched_hop_detail::multiply(static_cast<std::uint64_t>(seconds),rate);
            const auto residue=std::ceil((static_cast<long double>(now-seconds)+lead)*rate);
            if (!std::isfinite(residue) || residue<0 || residue>=std::ldexp(1.L,64))
                throw Error("transmit preparation lead exceeds the scheduled sample range");
            const auto earliest=modem::sched_hop_detail::add(base,static_cast<std::uint64_t>(residue));
            auto output_source=map.locate(earliest).source;
            // A saved-key reservation constrains the actual physical payload
            // epoch, not the compressed source clock or the first quiet hop.
            if (minimum_source>prefix)
                output_source.sample=std::max(output_source.sample,minimum_source-prefix);
            auto payload_source=output_source.add_samples(prefix);
            if(calendar) {
                // Snap BEFORE the factory encrypts/masks the first bit. The
                // exact physical slot is then its immutable private epoch.
                const auto rank=calendar->first_rank_source(payload_source.sample);
                payload_source={calendar->source_slot(rank)};
                output_source={payload_source.sample-prefix};
            }
            const auto output=map.source_to_physical(output_source);
            const auto payload=map.source_to_physical(payload_source);
            ScheduledTransmission result;
            result.epoch=payload.sample/rate;
            result.phase_samples=static_cast<std::uint32_t>(payload.sample%rate);
            if (result.epoch<minimum_epoch)
                throw Error("scheduled payload did not satisfy the reserved epoch");
            result.playback_epoch=static_cast<double>(output.sample/rate)+
                static_cast<double>(output.sample%rate)/rate;
            result.transmitter=make_at(result.epoch,result.phase_samples);
            const auto ready=clock();
            if (!std::isfinite(ready) || ready<now)
                throw Error("transmit clock moved backwards during preparation");
            if (ready+delivery_margin_seconds<result.playback_epoch) return result;
            lead=std::max(2*lead,ready-now+delivery_margin_seconds+.05);
        }
        throw Error("could not prepare transmission before its scheduled active hop");
    }
    const auto prefix = (static_cast<double>(modem::training_sample_count(config)) +
        static_cast<double>(modem::pattern_pulse_padding_samples(config))) / config.sample_rate;
    double lead = delivery_margin_seconds + .05;
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        if (stop.stop_requested()) throw Error("transmission cancelled");
        const auto now = clock();
        const auto target = std::ceil(now + prefix + lead);
        if (!std::isfinite(now) || now < 0 || !std::isfinite(target) ||
            static_cast<long double>(target) >= static_cast<long double>(std::numeric_limits<std::uint64_t>::max()))
            throw Error("transmit clock exceeds the whole-second epoch range");
        ScheduledTransmission result;
        result.epoch = std::max(minimum_epoch, static_cast<std::uint64_t>(target));
        result.playback_epoch = static_cast<double>(result.epoch) - prefix;
        result.transmitter = make_at(result.epoch,0);
        const auto ready = clock();
        if (!std::isfinite(ready) || ready < now) throw Error("transmit clock moved backwards during preparation");
        if (ready + delivery_margin_seconds < result.playback_epoch) return result;
        lead = std::max(2 * lead, ready - now + delivery_margin_seconds + .05);
    }
    throw Error("could not prepare transmission before its scheduled whole second");
}

// Bind both parts of the private origin before encoding or masking any bits.
template<class Factory,class EpochClock>
ScheduledTransmission schedule_transfer_transmission(transfer::Options& options,Factory make,
        EpochClock clock,std::stop_token stop={},std::uint64_t minimum_epoch=0,
        double delivery_margin_seconds=0) {
    return schedule_transmission(options.modem,[&](std::uint64_t epoch,std::uint32_t phase) {
        options.timestamp=epoch;options.modem.stream_phase_samples=phase;
        return make(options);
    },std::move(clock),stop,minimum_epoch,delivery_margin_seconds);
}

template<class EpochClock>
void wait_for_playback(double epoch, EpochClock clock, std::stop_token stop = {}) {
    auto previous=clock();
    if(!std::isfinite(previous) || previous<0)throw Error("invalid transmit clock while waiting");
    for (;;) {
        if (stop.stop_requested()) throw Error("transmission cancelled");
        const auto now=clock();
        if(now<previous)throw Error("transmit clock moved backwards while waiting");
        previous=now;
        const auto remaining = epoch - now;
        if (!std::isfinite(remaining) || remaining < -.25)
            throw Error("transmit clock missed the scheduled start");
        if (remaining <= 0) return;
        execution::sleep_for(std::chrono::duration<double>(std::min(.01, remaining)));
    }
}
}
