#pragma once

#include "search_fft.hpp"
#include <complex>
#include <cstddef>
#include <optional>
#include <span>
#include <stop_token>
#include <vector>

namespace datapump::fast::detail {

struct SsbFoldedPeak {
    // Circular delay within one known word, in proposal grid samples.
    std::size_t delay_samples;
    double frequency_hz;
    double coherence;
};

// Correlates complete repeated public words on a uniform proposal grid. One
// returned phase can correspond to several absolute waveform epochs.
class SsbFoldedPreamble {
public:
    using Complex = std::complex<double>;

    SsbFoldedPreamble(std::span<const Complex> reference_word,
                      double proposal_sample_rate,double max_abs_doppler_hz);

    // Searches 1..32 complete words. Coherence is squared projection divided
    // by observed and template energy. Zero energy or cancellation gives
    // no peak. The plan and work arrays belong to one serial caller.
    std::optional<SsbFoldedPeak> search(std::span<const Complex> complete_words,
                                       std::stop_token stop={});
    std::size_t workspace_bytes() const noexcept;

private:
    double sample_rate_;
    double max_doppler_;
    modem::detail::search_fft::DoublePlan fft_;
    double reference_energy_;
    std::vector<Complex> reference_spectrum_;
    std::vector<Complex> folded_;
    std::vector<Complex> spectrum_;
};

} // namespace datapump::fast::detail
