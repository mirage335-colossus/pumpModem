#pragma once
#include "datapump/transfer.hpp"

namespace datapump::lpi {
// Advisory weak-signal radiometer model; never a modem admission criterion.
inline constexpr double detection_probability = .90;
inline constexpr double false_alarm_probability = .01;
enum class Status { available, outside_weak_signal_model, numeric_limit };
struct Estimate {
    Status status = Status::numeric_limit;
    // True when the actual transfer is public (including tone). The estimate
    // then assumes private encrypted patterns at the current timing, without
    // enabling encryption, generating a key or changing the transmit profile.
    bool hypothetical_encryption = false;
    double cn0_db_hz = 0;
    double observation_bandwidth_hz = 0;
    double in_band_snr_db = 0;
    double noise_rise_db = 0;
    double symbol_seconds = 0;
    // Continuous on-air time and equivalent wire symbols at 90% detection,
    // 1% false alarm per known observation window. Fractional symbols matter:
    // an energy detector need not wait for a complete modem symbol.
    double detection_seconds = 0;
    double equivalent_symbols = 0;
    // Current draft's whole burst, including settling/filter/suppression time,
    // divided by modeled detection time. Hypothetical encryption does not
    // reencode the draft or predict different HMAC/pulse-tail overhead. This
    // compares exposure durations, not a probability or safe quota.
    double burst_exposure_ratio = 0;
};

// cn0_db_hz is received C/N0 at BOTH listeners, not automatically the TX
// design target. Uses the actual sampled symbol/chip geometry. Assumes an
// unkeyed listener knowing the band, on-air window and stationary AWGN power.
// Models private patterns even with encryption off, flagging that scenario as
// hypothetical. For tone, assumes the corresponding pattern's pulse shaping
// at the same chip/symbol timing. Transfer enables private Scrambler patterns
// for every selected non-tone key, including manual callers whose input
// modem.scramble is false. No retuning, key generation or encoding takes place.
// Only in-band SNR <= -10 dB receives a numerical time estimate; see
// docs/lpi-estimates.md for the model and physical limitations.
Estimate estimate(const transfer::Estimate& transmission,
                  const transfer::Options& options, double cn0_db_hz);
}
