#pragma once
#include "datapump/transfer.hpp"

namespace datapump::lpi {
// Relative weak-signal radiometer model; never a modem admission criterion.
inline constexpr double detection_probability = .90;
inline constexpr double false_alarm_probability = .01;
inline constexpr double receiver_reference_symbol_snr_db = tuning::pattern_target_symbol_snr_db;
enum class Status { available, outside_weak_signal_model, numeric_limit };
struct Estimate {
    Status status = Status::numeric_limit;
    // True when the actual transfer is public (including tone). The estimate
    // then assumes private encrypted patterns at the current timing, without
    // enabling encryption, generating a key or changing the transmit profile.
    bool hypothetical_encryption = false;
    // C/N0 normalized so ONE configured symbol has the receiver's existing
    // 18 dB Es/N0 design reference. Neither simulated nor measured link power.
    double reference_cn0_db_hz = 0;
    double observation_bandwidth_hz = 0;
    double in_band_snr_db = 0;
    double noise_rise_db = 0;
    double symbol_seconds = 0;
    double detection_seconds = 0; // Observer time at the normalized reference.
    // Observer on-air time / receiver's one-symbol observation time, at the
    // normalized reference power and equal C/N0 for both. The receiver's one
    // bit is a design reference, not a measured reception or a 1-in-N success
    // rate. equivalent_symbols is the TOTAL ratio N:1, not N additional bits.
    double equivalent_symbols = 0;
    double additional_symbols = 0; // max(0, equivalent_symbols - 1)
    // Current draft's whole burst, including settling/filter/suppression time,
    // divided by modeled detection time. Hypothetical encryption does not
    // reencode the draft or predict different HMAC/pulse-tail overhead. This
    // compares exposure durations, not a probability or safe quota.
    double burst_exposure_ratio = 0;
};

// Normalizes C/N0 at BOTH listeners to the receiver's one-symbol design
// reference. Uses actual sampled symbol/chip geometry; no simulation, live
// link, TX/RX target power or oscillator input is consumed. Assumes an unkeyed
// listener knowing the band, on-air window and stationary AWGN power.
// Models private patterns even with encryption off, flagging that scenario as
// hypothetical. For tone, assumes the corresponding pattern's pulse shaping
// at the same chip/symbol timing. Transfer enables private Scrambler patterns
// for every selected non-tone key, including manual callers whose input
// modem.scramble is false. No retuning, key generation or encoding takes place.
// Only in-band SNR <= -10 dB receives a numerical time estimate; see
// docs/lpi-estimates.md for the model and physical limitations.
Estimate estimate(const transfer::Estimate& transmission,
                  const transfer::Options& options);
}
