#pragma once
#include "datapump/transfer.hpp"
#include <cstdint>
#include <optional>
#include <stop_token>

namespace datapump::lpi {
// Relative weak-signal radiometer model; never a modem admission criterion.
inline constexpr double detection_probability = .90;
inline constexpr double false_alarm_probability = .01;
inline constexpr double receiver_reference_symbol_snr_db = tuning::pattern_target_symbol_snr_db;
enum class Status { available, outside_weak_signal_model, numeric_limit };
struct Hopping {
    // Hypothetical genuine hopping among disjoint equal-width bands. Every band
    // is recorded simultaneously, with known hopset, dwell and boundary times;
    // the observer does not know the active channel sequence.
    unsigned channels = 200;
    double dwell_seconds = .4;
};
enum class ObserverStrategy { known_band_radiometer, hopset_aggregate, dwell_channel_maximum };
enum class ChannelBankStatus { not_requested, available, insufficient_time_bandwidth, not_faster, numeric_limit };
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
    // Optional hypothetical genuine hopping: every channel is captured at once.
    // Fake GUI animation does not change the actual transmitted waveform.
    bool hypothetical_hopping = false;
    ObserverStrategy observer_strategy = ObserverStrategy::known_band_radiometer;
    ChannelBankStatus channel_bank_status = ChannelBankStatus::not_requested;
    unsigned hopping_channels = 1;
    double hopping_dwell_seconds = 0;
    double captured_signal_fraction = 1;
    double captured_noise_bandwidth_hz = 0; // K disjoint known channel bands; gaps excluded.
    double single_channel_detection_seconds = 0;
    double aggregate_detection_seconds = 0;
    double channel_bank_detection_seconds = 0; // Zero if not evaluated/unsupported/not faster.
    std::uint64_t channel_bank_dwells = 0;
    std::uint64_t channel_bank_samples_per_cell = 0;
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
// docs/lpi-estimates.md for the model and physical limitations. Optional hopping
// compares a full-hopset aggregate radiometer with a globally corrected maximum
// of independent Gaussian channel/dwell energy cells. The faster defined test
// is selected before observing data. It is not an optimal-observer bound.
// The cell test uses whole independent complex samples, the first dwell or later
// complete dwell endpoints, and explicit numerical limits; unsupported bank
// coverage leaves the aggregate model available with channel_bank_status set.
Estimate estimate(const transfer::Estimate& transmission,
                  const transfer::Options& options,
                  std::optional<Hopping> hopping = std::nullopt,
                  std::stop_token stop = {});
}
