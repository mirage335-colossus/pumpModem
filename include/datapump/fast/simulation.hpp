#pragma once

#include "datapump/fast/channel_assumptions.hpp"
#include "datapump/fast/profile.hpp"

#include <complex>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>

namespace datapump::fast {

// Select whether modeled link budget/noise describes a receiver simulation or
// whether only explicit waveform impairments are included in playback.
enum class ChannelPurpose { receiver_simulation, live_playback };
// Fixed retains the illustrative two constant-frequency rays. Gaussian uses
// two independent diffuse gains centered at zero, each with Doppler width
// B=2 sigma. Its finite native process has spectral tails, not hard support.
enum class SpreadEvolution { fixed, gaussian };

struct SimulationSettings {
    double transmit_watts = 40;
    double path_loss_db = 130;
    std::optional<double> noise_dbm_hz = -174;
    // Ppm values are the declared relative (two-end) bounds. Phase diffusion
    // values are relative radians per square root of a second.
    double baseband_ppm = 0;
    double baseband_phase_rad_sqrt_s = 0;
    double shift_ppm = 0;
    double shift_phase_rad_sqrt_s = 0;
    bool shared_reference = false;
    // The modeled translation oscillator frequency is never an audio carrier.
    double shift_hz = 0;
    double delay_ms = 0;
    double doppler_hz = 0;
    double selective_slow_depth = 0;
    double selective_fast_depth = 0;
    double margin = 1;
    std::optional<std::uint64_t> seed;
    SpreadEvolution spread_evolution = SpreadEvolution::fixed;
};

ChannelAssumptions channel_assumptions(const SimulationSettings&) noexcept;

struct SimulationReport {
    // Link-budget values describe equivalent receiver-side normalized noise
    // only when this flag is true. A live playback has no synthetic link budget.
    bool link_budget_applied = true;
    std::uint64_t seed = 0;
    std::uint64_t source_samples = 0;
    std::uint64_t output_samples = 0;
    std::uint64_t propagation_tail_samples = 0;
    std::optional<std::uint64_t> quiet_start_sample;
    double received_power_dbm = 0;
    std::optional<double> cn0_db_hz;
    double noise_variance = 0;
    double nominal_signal_power = 0;
    double sample_clock_error_ppm = 0;
    double shift_error_hz = 0;
    double clock_bound_ppm = 0;
    double frequency_bound_hz = 0;
    double spread_delay_ms = 0;
    double spread_doppler_hz = 0;
    double selective_spacing_hz = 0;
    SpreadEvolution spread_evolution = SpreadEvolution::fixed;
    double gaussian_doppler_width_hz = 0;
    // Audio-band admission allowance: +/-3B for Gaussian rays, plus the
    // selective-AM allowance. This is an engineering tail convention, not a
    // guaranteed spectral bound or a receiver success declaration.
    double propagation_frequency_allowance_hz = 0;
};

void validate(const Profile& profile, const SimulationSettings& settings,
              ChannelPurpose purpose = ChannelPurpose::receiver_simulation);

// The source supplies the whole analytic audio waveform, including its pulse
// tail. Its real part is the modem's ordinary PCM. A zero read ends the source.
// Receiver simulation adds link-budget noise at the receiver boundary after
// propagation. Live playback applies only selected waveform impairments: it
// adds no synthetic path loss or receiver noise. Both modes preserve channel
// tails and a separately sampled quiet interval.
class SimulationChannel {
public:
    using Source = std::function<std::size_t(std::span<std::complex<float>>)>;

    SimulationChannel(const Profile& profile, const SimulationSettings& settings, Source source,
                      ChannelPurpose purpose = ChannelPurpose::receiver_simulation);
    ~SimulationChannel();
    SimulationChannel(SimulationChannel&&) noexcept;
    SimulationChannel& operator=(SimulationChannel&&) noexcept;
    SimulationChannel(const SimulationChannel&) = delete;
    SimulationChannel& operator=(const SimulationChannel&) = delete;

    std::size_t read(std::span<float> output);
    const SimulationReport& report() const;
    std::size_t workspace_bytes() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace datapump::fast
