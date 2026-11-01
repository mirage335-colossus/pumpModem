#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace datapump::triad {

// Per-end RF/audio error assumptions for a synthetic link. The RF fields are
// fractional ppm errors applied at the configured RF carrier. Phase noise is
// a Wiener phase process in radians/sqrt(second), not a calibrated oscillator
// phase-noise mask.
struct Oscillator {
    double rf_ppm = 0;
    double rf_drift_ppm_per_second = 0;
    double phase_noise_rad_per_sqrt_second = 0;
    double audio_ppm = 0;
};

struct SimulationConfig {
    double sample_rate = 12'000;
    double occupied_bandwidth_hz = 2'250;
    double rf_hz = 30'000'000;
    // Nominal analog/audio conversion IF whose residual survives complex
    // direct conversion when the independent audio sample clocks differ.
    // Zero preserves the original zero-centered baseband simulation.
    double nominal_conversion_if_hz = 0;
    Oscillator tx{};
    Oscillator rx{};
    std::uint64_t seed = 1;

    // Receiver time minus transmitter time. Positive values put the signal
    // later in the capture. Negative values require a matching public capture
    // lead; the simulator never recenters from the simulated timing truth.
    double timing_offset_seconds = 0;
    // Capture begins this many seconds before its nominal t=0 coordinate.
    // This is a public search window, independent of simulated timing truth.
    double capture_lead_seconds = 0;
    // Public post-observation allowance. This is additional captured noise/
    // channel output, never a manufactured physical-completion indication.
    double capture_trail_seconds = 0;
    double static_doppler_hz = 0;

    // Illustrative shared two-ray spread and optional three-band amplitude
    // perturbation. Zero delay/Doppler and zero depths is the explicit bypass.
    double spread_delay_ms = 0;
    double spread_doppler_hz = 0;
    double perturbation_slow_depth = 0;
    double perturbation_fast_depth = 0;

    // SNR is referred to input mean signal power divided by noise power in the
    // occupied bandwidth. Complex sample noise is scaled by Fs/BW, so changing
    // the simulation sample rate does not improve the physical SNR. No
    // received-signal normalization is performed.
    double snr_db = 0;
    bool noise_enabled = true;
    bool signal_enabled = true;

    // Optional repeated erasure window relative to each 400 ms dwell. A zero
    // duration disables this impairment. Erasures remove both signal and noise
    // after channel processing; delayed multipath is therefore lost too.
    double dropout_ms = 0;
    double dropout_start_ms = 0;
    std::size_t dropout_every_n_dwells = 1;

    // The IQ source supplies its own shaped active/quiet envelope. These
    // offsets only translate the already-shaped complex source frequency.
    // They are repeated once per dwell. An empty vector is a non-hopping path.
    bool fhss = false;
    double dwell_seconds = 0.4;
    std::vector<double> hop_offsets_hz{};
    double retune_offset_seconds = 0;

    // A deterministic slow gain tracker may be enabled to study amplitude
    // patterns through AGC. It is applied after propagation and noise.
    bool slow_agc = false;
};

struct Capture {
    std::vector<std::complex<double>> iq;
    std::size_t source_samples = 0;
    std::size_t tail_samples = 0;
    std::size_t timing_pad_samples = 0;
    std::size_t erased_samples = 0;
    std::size_t out_of_passband_samples = 0;
    double input_power = 0;
    double noise_reference_bandwidth_hz = 0;
    // Complex sample variance; integrated noise power in the occupied band is
    // noise_power * noise_reference_bandwidth_hz / sample_rate.
    double noise_power = 0;
    double observed_power = 0;
    double capture_signal_power = 0;
    double expected_max_cfo_hz = 0;
    Oscillator tx_realization{};
    Oscillator rx_realization{};
};

// Conservative bound for independent end errors. RF ppm is scaled at rf_hz;
// audio ppm is scaled only at nominal_conversion_if_hz. Drifts are bounded
// over duration_seconds about each configured starting offset.
double oscillator_bound_hz(double rf_hz, double per_end_ppm_bound,
                           double duration_seconds,
                           double per_end_drift_ppm_per_second = 0,
                           double nominal_conversion_if_hz = 0,
                           double per_end_audio_ppm_bound = 0,
                           double per_end_audio_drift_ppm_per_second = 0);

// Simulate an independently generated complex-baseband source. The receiver
// consumes only Capture::iq; channel truth remains in SimulationConfig and
// is not included in any decoding interface. The input must already contain
// its transmit ramps/quiet intervals. Propagated output is never re-gated.
Capture simulate(std::span<const std::complex<double>> source,
                 const SimulationConfig& config);

} // namespace datapump::triad
