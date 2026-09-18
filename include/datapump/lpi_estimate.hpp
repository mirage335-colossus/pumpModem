#pragma once
#include "datapump/transfer.hpp"

namespace datapump::lpi {
// Advisory weak-signal radiometer model; never a modem admission criterion.
inline constexpr double detection_probability = .90;
inline constexpr double false_alarm_probability = .01;
enum class Status { available, public_waveform, outside_weak_signal_model, numeric_limit };
struct Estimate {
    Status status = Status::public_waveform;
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
    // Whole burst, including settling/filter/suppression time, divided by the
    // modeled detection time. This is exposure, not a probability or safe quota.
    double burst_exposure_ratio = 0;
};

// cn0_db_hz is received C/N0 at BOTH listeners, not automatically the TX
// design target. Uses the actual sampled symbol/chip geometry. Assumes an
// unkeyed listener knowing the band, on-air window and stationary AWGN power.
// Transfer enables private Scrambler patterns for every selected non-tone key,
// including manual callers whose input modem.scramble is false.
// Only in-band SNR <= -10 dB receives a numerical time estimate; see
// docs/lpi-estimates.md for the model and physical limitations.
Estimate estimate(const transfer::Estimate& transmission,
                  const transfer::Options& options, double cn0_db_hz);
}
