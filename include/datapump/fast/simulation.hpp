#pragma once

#include "datapump/fast/profile.hpp"

#include <complex>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>

namespace datapump::fast {

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
};

struct SimulationReport {
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
};

void validate(const Profile& profile, const SimulationSettings& settings);

// The source supplies the whole analytic audio waveform, including its pulse
// tail. Its real part is the modem's ordinary PCM. A zero read ends the source.
// This channel supplies the delayed tail and a separately sampled quiet window
// through the same continuous noise and impairment state.
class SimulationChannel {
public:
    using Source = std::function<std::size_t(std::span<std::complex<float>>)>;

    SimulationChannel(const Profile& profile, const SimulationSettings& settings, Source source);
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
