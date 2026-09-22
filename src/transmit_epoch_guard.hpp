#pragma once

#include "datapump/pattern_pulse.hpp"
#include "datapump/symbol_schedule.hpp"
#include <algorithm>

namespace datapump::detail {
// Account for every symbol that can contribute to the generated prefix. The
// pulse support reaches padding samples before a symbol's nominal start; its
// leading padding therefore cancels here. Reserving a zero-valued edge sample
// is conservative. Never infer exposure from the nominal payload cursor alone.
inline std::uint64_t exposed_transmit_epoch(const modem::Config& config,
        std::uint64_t epoch, std::uint64_t total_samples, std::uint64_t emitted) {
    const auto training=modem::training_sample_count(config);
    const auto padding=modem::pattern_pulse_padding_samples(config);
    const auto overhead=training+2*padding+modem::suppression_sample_count(config);
    const auto symbol=modem::symbol_sample_count(config);
    if(total_samples<=overhead || emitted<=training)return epoch;
    const auto symbols=(total_samples-overhead)/symbol;
    if(!symbols)return epoch;
    const auto last=std::min(symbols-1,(emitted-training-1)/symbol);
    return modem::symbol_stream_address(epoch,config.stream_phase_samples,last,
        symbol,config.sample_rate).epoch;
}

// An automatic payload starts on a whole second after its prefix. Waiting
// until this playback time is deliberately conservative about rounding and
// the scheduler's preparation lead; no received timestamp chooses this wait.
inline double transmit_epoch_lock_seconds(const modem::Config& config,
        std::uint64_t used_epoch, double now) {
    const auto prefix=(static_cast<long double>(modem::training_sample_count(config))+
        modem::pattern_pulse_padding_samples(config))/config.sample_rate;
    const auto remaining=static_cast<long double>(used_epoch)+1-prefix-now;
    return static_cast<double>(std::max(0.L,remaining));
}
}
