#pragma once
#include "datapump/streaming_modem.hpp"
#include "datapump/pattern_pulse.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <thread>

namespace datapump::detail {
struct ScheduledTransmission {
    std::unique_ptr<modem::StreamingTransmitter> transmitter;
    std::uint64_t epoch = 0;
    double playback_epoch = 0;
};

// Call after opening the output device, before requesting its first samples.
// The prefix precedes the whole-second payload origin. Preparation may take
// longer than anticipated, so retry with a later origin before emitting audio.
template<class Factory, class EpochClock>
ScheduledTransmission schedule_transmission(const modem::Config& config, Factory make,
        EpochClock clock, std::stop_token stop = {}, std::uint64_t minimum_epoch = 0) {
    const auto prefix = (static_cast<double>(modem::training_sample_count(config)) +
        static_cast<double>(modem::pattern_pulse_padding_samples(config))) / config.sample_rate;
    double lead = .05;
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
        result.transmitter = make(result.epoch);
        const auto ready = clock();
        if (!std::isfinite(ready) || ready < now) throw Error("transmit clock moved backwards during preparation");
        if (ready < result.playback_epoch) return result;
        lead = std::max(2 * lead, ready - now + .05);
    }
    throw Error("could not prepare transmission before its scheduled whole second");
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
        std::this_thread::sleep_for(std::chrono::duration<double>(std::min(.01, remaining)));
    }
}
}
