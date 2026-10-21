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
