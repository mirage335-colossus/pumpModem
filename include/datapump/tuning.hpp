#pragma once
#include "datapump/modem.hpp"
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace datapump::tuning {
// target_snr_db_hz is C/N0: signal power / noise power in a 1 Hz bandwidth.
// Results are integration estimates, not empirical sensitivity guarantees.
inline constexpr double minimum_bandwidth_hz = modem::minimum_bandwidth_hz;
inline constexpr double maximum_bandwidth_hz = modem::maximum_bandwidth_hz;
// Ideal AWGN Shannon-Hartley channel capacity in bits/s. The target is C/N0
// in dB-Hz, so in-band linear SNR is 10^(target_snr_db_hz/10)/bandwidth_hz.
// This theoretical ceiling is independent of modem framing and coding.
double shannon_capacity_bps(double bandwidth_hz, double target_snr_db_hz);
// Static radial Doppler calculator. Positive velocity means receding and
// lowers the physical carrier. This is not transmit precompensation or drift
// tracking; a reflected path's effective radial velocity is supplied by the
// caller, without an automatic round-trip multiplier.
struct Doppler {
    double velocity_c = 0;
    double frequency_ratio = 1;
    double frequency_shift_percent = 0;
    std::string canonical = "0.000000c";
};
// Accept c, knots, mph, kph or a signed frequency shift in percent (%).
// Velocity must be strictly below light speed; the shifted carrier must remain
// positive. Canonical text uses c and preserves the parsed double precision.
Doppler parse_doppler(std::string_view text);
double doppler_carrier_hz(double configured_physical_carrier_hz, const Doppler& doppler);
// Suggested display geometry only; no receiver profile is selected.
std::uint32_t recommended_sample_rate(double bandwidth_hz,
    std::optional<double> carrier_hz = std::nullopt);
double recommended_carrier_hz(double bandwidth_hz);
// Receiver execution only. The requested aliases remain distinct when saved;
// they do not select transmitted patterns, cipher addresses or search coverage.
modem::OuterDsssVersion parse_outer_dsss_version(std::string_view name);
std::string_view outer_dsss_version_id(modem::OuterDsssVersion version);
modem::SearchArithmetic parse_search_arithmetic(std::string_view name);
std::string_view search_arithmetic_id(modem::SearchArithmetic arithmetic);
modem::ReceiverStrategy parse_receiver_strategy(std::string_view name);
std::string_view receiver_strategy_id(modem::ReceiverStrategy strategy);
void validate_receiver_cpu_budget(double ratio);
struct SimulationPreset {
    std::string_view name;
    bool enabled = false;
    double transmit_dbm = 0;
    double attenuation_db = 0;
};
std::span<const SimulationPreset> simulation_presets();
SimulationPreset parse_simulation_preset(std::string_view name);
// Illustrative residual clock error and Wiener phase diffusion for simulation,
// not measured device specifications. GPS discipline alone does not establish
// short-term phase stability; the underlying oscillator and loop also matter.
struct OscillatorPreset {
    std::string_view id;
    std::string_view name;
    double clock_error_ppm = 100;
    double phase_noise_degrees_per_sqrt_second = .5;
    // RF-only radio references cannot describe the independent audio clock.
    bool baseband_supported = true;
};
std::span<const OscillatorPreset> oscillator_presets();
OscillatorPreset parse_oscillator_preset(std::string_view name);
modem::OscillatorModel oscillator_model(const OscillatorPreset&);
// Delay and Doppler spread are channel dispersion, separate from radial motion.
struct SpreadPreset {
    std::string_view id, name;
    double delay_ms, doppler_hz;
    bool default_perturbation;
};
std::span<const SpreadPreset> spread_presets();
SpreadPreset parse_spread_preset(std::string_view name);
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
