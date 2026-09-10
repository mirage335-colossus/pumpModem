#pragma once
#include "datapump/modem.hpp"
#include <span>
#include <string>
#include <string_view>

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
struct Plan {
    modem::Config config;
    double estimated_processing_gain_db = 0;
    double target_symbol_snr_db = 10;
    double estimated_symbol_snr_db = 0;
    double required_spreading = 1;
    bool target_supported = false;
    std::string explanation;
};
// target_snr_db_hz is C/N0: signal power / noise power in a 1 Hz bandwidth.
// Results are integration estimates, not empirical sensitivity guarantees.
Plan resolve(double bandwidth_hz, double target_snr_db_hz, PatternMode mode,
             bool encryption);
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
