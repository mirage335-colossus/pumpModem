#pragma once
#include "datapump/modem.hpp"
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace datapump::tuning {
enum class PatternMode {
    auto_keystream, auto_pattern, auto_tone,
    pattern_3, pattern_4, pattern_6, pattern_8, pattern_12, pattern_16,
    tone_1, tone_2, tone_3, tone_4, tone_8, tone_32, tone_128,
    tone_1024, tone_4096, tone_16384
};
PatternMode parse_pattern_mode(std::string_view name);
std::string_view pattern_mode_name(PatternMode mode);
std::span<const PatternMode> pattern_modes();
bool tone_mode(PatternMode mode);
inline constexpr double default_receive_target_db_hz = 60;
inline constexpr std::size_t maximum_receive_targets = 16;
inline constexpr std::size_t maximum_receive_target_text = 512;
struct ReceiveTargets {
    std::vector<double> values{default_receive_target_db_hz};
    std::string canonical = "60";
    bool reset = false;
};
// One invalid token resets the complete list. Accept at most 16 finite
// decimal targets in -200..200 dB-Hz and preserve first-occurrence order.
ReceiveTargets parse_receive_targets(std::string_view text);
struct Plan {
    modem::Config config;
    double estimated_processing_gain_db = 0;
    double target_symbol_snr_db = 0;
    double estimated_symbol_snr_db = 0;
    double required_spreading = 1;
    bool target_supported = false;
    std::string explanation;
};
// target_snr_db_hz is C/N0: signal power / noise power in a 1 Hz bandwidth.
// Results are integration estimates, not empirical sensitivity guarantees.
inline constexpr double maximum_bandwidth_hz = 30000000;
// Automatic real-PCM plans center narrow audio bands at 1500 Hz. Wider bands
// retain the 0.75 * bandwidth carrier. The logical clock covers both the band
// and carrier with four samples per hertz, keeping the nominal upper edge in
// the resampler's flat passband. Hardware clocks are negotiated independently.
// An explicit carrier changes the internal clock as needed; omitted carriers
// preserve the existing automatic recommendation.
std::uint32_t recommended_sample_rate(double bandwidth_hz,
    std::optional<double> carrier_hz = std::nullopt);
double recommended_carrier_hz(double bandwidth_hz);
Plan resolve(double bandwidth_hz, double target_snr_db_hz, PatternMode mode,
             bool encryption, std::optional<double> carrier_hz = std::nullopt);
// Selected bandwidth and pattern mode stay fixed. Targets resolving to the
// same waveform profile share one receiver hypothesis.
std::vector<modem::Config> receive_profiles(double bandwidth_hz,
    std::span<const double> targets_db_hz, PatternMode mode, bool encryption);
// Preserve the caller's carrier, sample clock, DSSS settings, seeds, epoch and
// resource limit while replacing only the selected pattern integration plan.
// Tone profiles always clear Data encryption, Scrambler and DSSS material.
std::vector<modem::Config> receive_profiles(const modem::Config& base,
    std::span<const double> targets_db_hz, PatternMode mode, bool encryption);
struct SimulationPreset {
    std::string_view name;
    bool enabled = false;
    double transmit_dbm = 0;
    double attenuation_db = 0;
};
std::span<const SimulationPreset> simulation_presets();
SimulationPreset parse_simulation_preset(std::string_view name);
struct LinkBudget {
    double received_power_dbm = 0;
    double noise_power_dbm = 0; // In the configured channel bandwidth.
    double snr_db = 0;         // In the configured channel bandwidth.
    double snr_db_hz = 0;      // C/N0, independent of channel bandwidth.
    double sample_snr_db = 0;  // For modem::ChannelConfig::snr_db (Fs/2 noise).
};
LinkBudget link_budget(const SimulationPreset& preset, double bandwidth_hz,
                       std::uint32_t sample_rate, double noise_figure_db = 10);
}
