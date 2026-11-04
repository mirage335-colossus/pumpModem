#pragma once
#include "datapump/modem.hpp"
#include <array>
#include <complex>
#include <optional>
#include <span>

namespace datapump::live::detail {
inline constexpr std::size_t signal_window_size=2048;
struct SignalPlots {
    std::vector<float> waveform;
    std::vector<double> spectrum;
    // Display-only native bins. These preserve phase without another FFT.
    // The observed interval is [start,start+count); startup zero padding is
    // never presented as a complete window. Live supplies stream identity.
    std::vector<std::complex<float>> spectrum_complex;
    std::uint64_t spectrum_start_sample=0;
    std::size_t spectrum_sample_count=0;
    std::vector<std::complex<double>> constellation;
};
enum class SignalView { all, environment, constellation };
SignalPlots signal_plots(std::span<const float> samples,const modem::Config& config,
                         std::uint64_t first_sample=0,SignalView view=SignalView::all);
// Match partial startup FFT noise bandwidth to a complete Hann window, using
// display metadata only. Full windows and unreferenced hardware stay unchanged.
std::optional<double> spectrum_display_gain(std::optional<double> reference_gain_db,
                                             std::size_t waveform_samples);
struct HelperSpectrumMeasurement {
    double pilot_snr_db=0;
    double cn0_db_hz=0;
    double noise_band_lower_hz=0;
    double noise_band_upper_hz=0;
    double reference_bandwidth_hz=0;
    double pilot_power=0;
    std::uint32_t sample_rate=0;
    std::uint64_t end_sample=0;
    // Median-based white-noise-equivalent floor; narrow spurs are excluded
    // statistically, while pilot_snr_db retains all power in the band.
    double floor_snr_db=0;
    // Largest absolute raw capture sample in this same window (1 = full scale).
    double capture_peak=0;
    // Estimated summed power of resolved, unaliased pilot harmonic bands
    // relative to pilot power, in dBc. Null means absent bands or below the
    // measured floor. These bands can contain other signals and are not THD.
    std::optional<double> harmonic_distortion_db;
    std::size_t harmonic_count=0;
    // Uncorrected harmonic-band power relative to the pilot, including the
    // noise and any other signals in these bands. This is not a THD estimate.
    std::optional<double> harmonic_band_level_db;
    // Strongest individually resolved harmonic band, after subtracting noise
    // estimated from its local sidebands, relative to the pilot. A line at a
    // harmonic frequency can also be another signal, so this is not THD.
    std::optional<double> strongest_harmonic_level_db;
    std::size_t strongest_harmonic_order=0;
};
// Display-only measurement from one complete raw capture window. Estimate the
// mean noise density above the pilot, away from its lobe and the auxiliary tone.
// Prefer 300–3000 Hz where at least 16 bins are available, otherwise use up to
// 2700 Hz above the guard. Refer SNR to 2700 Hz by extrapolating that density.
// A separate median-based floor estimate describes noise in the same band.
// No receiver state is used.
std::optional<HelperSpectrumMeasurement> helper_spectrum_measurement(
    std::span<const float> waveform,std::uint32_t sample_rate,double pilot_hz,
    std::uint64_t end_sample);
// Storage for the live environment monitor. Construct once per prepared live
// format and reuse for each frame; callers must consume the borrowed view
// synchronously before the next call.
struct EnvironmentFrameWorkspace {
    std::array<float,signal_window_size> waveform{};
    std::array<std::complex<double>,signal_window_size> bins{};
    std::array<double,signal_window_size/2+1> spectrum{};
    std::array<std::complex<float>,signal_window_size/2+1> spectrum_complex{};
};
struct EnvironmentFrameView {
    std::span<const float> waveform;
    std::span<const double> spectrum;
    std::span<const std::complex<float>> spectrum_complex;
    std::uint64_t spectrum_start_sample=0;
    std::size_t spectrum_sample_count=0;
};
// Input callback sizes and UI polling cadence cannot change the signal clock.
// Retain at least one nominal 60 Hz frame for the constellation. Waveform and
// spectrum still use their fixed 2,048-sample tail. No display clock is needed.
class SignalWindow {
public:
    explicit SignalWindow(const modem::Config& config = {});
    static std::size_t sample_capacity(const modem::Config& config);
    static std::size_t constellation_capacity(const modem::Config& config);
    void push(std::span<const float> samples);
    void reset(std::uint64_t first_sample=0) noexcept { next_=size_=0; total_=first_sample; }
    std::uint64_t samples_seen() const noexcept { return total_; }
    std::size_t sample_storage_bytes() const noexcept { return samples_.capacity()*sizeof(float); }
    SignalPlots frame(const modem::Config& config,SignalView view=SignalView::all) const;
    EnvironmentFrameView environment_frame(EnvironmentFrameWorkspace& workspace) const noexcept;
private:
    std::vector<float> samples_;
    std::size_t next_=0,size_=0;
    std::uint64_t total_=0;
};
}
