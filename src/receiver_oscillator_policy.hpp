#pragma once
// Ordinary unknown-peer receive authority, independent of Live execution
// medium. A sampled source does not make the configured peer's UTC steering
// known to ordinary Live reception. transfer::simulate deliberately remains a
// distinct known-source, fixed-rate comparator with its own narrower bank.
#include "datapump/transfer.hpp"
#include "datapump/pattern_search.hpp"

namespace datapump::transfer {
inline double receiver_utc_rate_correction(const modem::Config& config,const Options& options) {
    return effective_clock_sync(options)?modem::utc_transmit_rate_limit(config):0;
}
inline modem::OscillatorPatternSearch receiver_oscillator_search(const modem::Config& config,const Options& options) {
    return modem::oscillator_pattern_search(config,receiver_utc_rate_correction(config,options));
}
}
