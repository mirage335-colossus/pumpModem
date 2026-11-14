#pragma once

#include "appraisal/hf_fullwave_kernel.hpp"
#include "appraisal/hf_magnetoionic_kernel.hpp"
#include "appraisal/hf_ionosphere.hpp"
#include "datapump/triad_modem_v3.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <vector>

namespace appraisal {
using Complex = datapump::triad::v3::Complex;
using Source = datapump::triad::v3::EmittedSource;

// All times are physical seconds from sample zero. Initial path phase includes
// the carrier phase at t=0. Geometric phase thereafter is -2*pi*RF*(tau(t)-tau(0));
// residual Doppler is a separate, explicitly non-geometric phase contribution.
struct Path {
    double power = 1, phase_radians = 0;
    double delay_seconds = 0, delay_rate = 0;
    double residual_doppler_hz = 0, residual_drift_hz_s = 0;
    double gaussian_width_2sigma_hz = 0;
    bool frozen_gaussian = false;
    // Engineering phase-only sensitivity, not an inferred ionospheric law:
    // amplitude*sin(2*pi*t/period), with t continuing through source silence.
    double phase_wander_amplitude_radians = 0;
    double phase_wander_period_seconds = 7200;
    std::optional<HfModeProfile> hf;
};
struct Fade {
    double first_seconds = -1, stop_seconds = -1, ramp_seconds = 0;
    double voltage_floor = 0;
};
struct Resonator {
    bool enabled = false;
    double inductance_h = 0, capacitance_f = 0, resistance_ohm = 0;
    double detuning_hz = 0, detuning_rate_hz_s = 0;
};
struct Noise {
    // E|n|^2 in the declared normalized input plane; no realized-power scaling.
    double white_variance = 0, colored_variance = 0, colored_corner_hz = 1;
    double impulse_rate_hz = 0, impulse_rms = 0, impulse_decay_seconds = .01;
    double tone_amplitude = 0, tone_hz = 0, tone_sweep_hz_s = 0;
};
struct Frontend {
    double lowpass_hz = 0, clip_magnitude = 0;
    double agc_target = 0, agc_attack_seconds = .01, agc_release_seconds = 1;
    unsigned adc_bits = 0;
};
struct LunarSpectralSpec {
    double limb_width_hz = 88, dish_m = 3, bulk_delay_seconds = 2.4720213723;
};
struct HfFullwaveReflectionSpec {
    static constexpr double transition_first_seconds = 60., transition_seconds = 120.;
    static constexpr double minimum_source_seconds = 540.;
};
struct HfMagnetoionicReflectionSpec {
    // Fixed laboratory x-polarized transmitter; receiver selects x (0) or y (1).
    // These are antenna projections, not a receiver supplied with both fields.
    unsigned receive_component = 0;
    HfMagnetoionicPath path = HfMagnetoionicPath::tilted_lower;
    double transition_first_seconds = 60., transition_seconds = 600.;
};
struct ChannelSpec {
    double rf_hz = 1e6;
    std::vector<Path> paths{{}};
    std::optional<LunarSpectralSpec> lunar;
    std::optional<HfFullwaveReflectionSpec> fullwave_reflection;
    std::optional<HfMagnetoionicReflectionSpec> magnetoionic_reflection;
    Resonator transmitter;
    Noise noise;
    Frontend frontend;
    Fade fade;
    unsigned interpolation_half_width = 16;
    std::size_t maximum_workspace_bytes = 64u * 1024u * 1024u;
    // Place the capture within the analytic HF trajectory without shifting the
    // source/calendar or additive-noise clock. Its physical reference remains
    // t=0; this does not rebase each ray's initial delay or carrier phase.
    double hf_trajectory_offset_seconds = 0;
    // Additive-noise RNG root only. Absent preserves the historical seed route.
    std::optional<std::uint64_t> noise_seed;
};
struct ChannelWorkspaceError : std::invalid_argument { using std::invalid_argument::invalid_argument; };
struct FullwaveReflectionUnavailable : std::invalid_argument { using std::invalid_argument::invalid_argument; };
struct MagnetoionicReflectionUnavailable : std::invalid_argument { using std::invalid_argument::invalid_argument; };
struct HfFullwaveReflectionReport {
    struct Region {
        std::uint64_t samples = 0;
        long double incident_energy = 0, reflected_energy = 0;
    };
    HfFullwaveKernelTableChecks checks;
    std::string table_sha256;
    std::size_t table_workspace_bytes = 0, stream_workspace_bytes = 0;
    double preparation_seconds = 0, retarded_peak_error_bound = 0, carrier_flight_phase_bound = 0;
    double source_minimum_frequency_hz = 0, source_maximum_frequency_hz = 0;
    std::array<Region, 3> regions;
};
struct HfMagnetoionicReflectionReport {
    struct Region {
        std::uint64_t samples = 0;
        long double incident_energy = 0;
        std::array<long double, 2> reflected_component_energy{};
    };
    HfMagnetoionicKernelChecks checks;
    std::string table_sha256;
    std::size_t table_workspace_bytes = 0, stream_workspace_bytes = 0;
    double preparation_seconds = 0;
    std::array<double, 2> retarded_peak_error_bound{};
    double source_minimum_frequency_hz = 0, source_maximum_frequency_hz = 0;
    HfMagnetoionicReflectionSpec spec;
    std::array<Region, 3> regions;
};
struct ChannelReport {
    std::uint64_t samples = 0, source_samples = 0, impulse_events = 0, clipped_samples = 0;
    std::size_t workspace_bytes = 0;
    long double input_energy = 0, signal_energy = 0, output_energy = 0, additive_noise_energy = 0;
    double maximum_delay_seconds = 0, tail_seconds = 0, minimum_agc_gain = 1;
    std::optional<HfFullwaveReflectionReport> fullwave_reflection;
    std::optional<HfMagnetoionicReflectionReport> magnetoionic_reflection;
};
// A bounded, single-owner, forward renderer. It never rewinds or reseeds the
// canonical Emission. Horizon is an evaluator/resource bound, never RX truth.
class Channel {
public:
    Channel(Source, ChannelSpec, std::uint64_t seed, std::uint64_t horizon_samples, std::stop_token = {});
    ~Channel();
    Channel(Channel&&) noexcept;
    Channel& operator=(Channel&&) noexcept;
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;
    // Checks cancellation before/after a read and at most every 64 rendered
    // samples, including source silence. A cancelled read throws; report().samples
    // identifies the fully rendered prefix, whose state remains consistent.
    std::size_t read(std::span<Complex>, std::stop_token = {});
    const ChannelReport& report() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
double resonator_tail_seconds(const Resonator&, double relative_amplitude = 1e-6);
Complex resonator_transfer(const Resonator&, double offset_hz);
double channel_tail_seconds(const ChannelSpec&, double horizon_seconds);

struct WaveformSettings {
    double rate_hz = 36, requested_bit_seconds = 16, sample_rate_hz = 960;
    unsigned added_symbols = 10;
    bool fhss = false;
};
struct PublicBounds {
    double arrival_low_seconds = 0, arrival_high_seconds = .02;
    double frequency_bound_hz = 0, atmospheric_hz = 0;
    double spread_seconds = 0, doppler_width_hz = 0;
    bool varying = false;
    double delay_excursion_seconds = 0;
};
struct Scenario {
    std::string id, family, evidence, provenance, assumptions;
    ChannelSpec channel;
    WaveformSettings waveform;
    PublicBounds bounds;
    double reference_cn0_db_hz = 32;
    // Positive values bound the qualified numerical episode, measured from
    // channel time zero. Zero supplies no finite-horizon qualification.
    double numerical_horizon_seconds = 0;
    // Informational physical link coordinates, only present where connected.
    double reference_received_dbm = 0, reference_noise_dbm_hz = 0;
    bool physical_level_connected = false;
};
std::vector<std::string> scenario_names();
Scenario make_scenario(const std::string& id);
// Power weights include the beams, with a fixed wide-beam normalization.
// This is a frozen rotating-sphere hypothesis, not an ephemeris solver.
std::vector<Path> lunar_paths(unsigned rings, unsigned azimuths, double limb_width_hz,
                              double dish_m, double rf_hz, double bulk_delay_seconds);
}
