#pragma once
#include "datapump/modem.hpp"
#include <array>

namespace datapump::live::detail {
inline constexpr std::size_t signal_window_size=2048;
struct SignalPlots {
    std::vector<float> waveform;
    std::vector<double> spectrum;
    std::vector<std::complex<double>> constellation;
};
SignalPlots signal_plots(std::span<const float> samples,const modem::Config& config,
                         std::uint64_t first_sample=0);
// Input callback sizes and UI polling cadence cannot change the signal clock.
// Retain only a fixed contiguous tail, even when drawing skips older frames.
class SignalWindow {
public:
    void push(std::span<const float> samples);
    void reset(std::uint64_t first_sample=0) noexcept { next_=size_=0; total_=first_sample; }
    std::uint64_t samples_seen() const noexcept { return total_; }
    SignalPlots frame(const modem::Config& config) const;
private:
    std::array<float,signal_window_size> samples_{};
    std::size_t next_=0,size_=0;
    std::uint64_t total_=0;
};
}
