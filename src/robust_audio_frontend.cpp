#include "datapump/robust_audio_frontend.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace datapump::live::detail {
namespace {
using triad::v3::Complex;

Complex carrier(std::uint64_t sample, double frequency, std::uint32_t rate) {
    const auto cycles = std::remainder(static_cast<long double>(sample) * frequency / rate, 1.L);
    return std::polar(1., static_cast<double>(2 * std::numbers::pi_v<long double> * cycles));
}
}

float real_pcm(Complex baseband, std::uint64_t sample, double if_hz, std::uint32_t rate, double scale) {
    return static_cast<float>(std::sqrt(2.) * (baseband * carrier(sample, if_hz, rate)).real() * scale);
}

// At an audio rate with admitted nonoverlapping real spectral images, retain
// the positive sideband, then translate it to the DATA center. The Fejer taper
// gives a nonnegative normalized frequency-domain convolution of the ideal
// Hilbert response. Its operator norm is at most one. For normalized real PCM
// with covariance <= sigma² I, the real-augmented analytic output covariance
// is <= sigma² I, including after rotation. This supports the declared proper
// complex covariance envelope E|n|² = 2 sigma². Actual hardware noise can
// violate that model and live field coverage remains conditional.
AudioInput::AudioInput(std::uint32_t rate, double carrier_hz, double lower_hz, double upper_hz,
                       std::uint64_t first_input_sample)
    : rate_(rate), carrier_(carrier_hz), origin_(first_input_sample) {
    if (!rate || !std::isfinite(carrier_) || !std::isfinite(lower_hz) || !std::isfinite(upper_hz) ||
        lower_hz <= 0 || lower_hz > carrier_ || upper_hz < carrier_ || upper_hz >= rate / 2.)
        throw Error("Invalid Robust audio projection band");
    const auto edge = std::min(lower_hz, rate / 2. - upper_hz);
    const auto half = std::ceil(8. * rate / edge);
    if (!std::isfinite(half) || half < 1 || half > 2048)
        throw Error("Robust live audio requires more separation from DC or Nyquist");
    half_ = static_cast<std::size_t>(half);
    if (origin_ > std::numeric_limits<std::uint64_t>::max() - half_)
        throw Error("Robust audio origin cannot contain the projection halo");
    length_ = 2 * half_ + 1;
    samples_.resize(2 * length_);
    validity_.resize(length_);
    invalid_ = length_;
    for (std::size_t offset = 1; offset <= half_; offset += 2)
        taps_.push_back(2 / (std::numbers::pi * static_cast<double>(offset)) *
            (1 - static_cast<double>(offset) / static_cast<double>(half_ + 1)));
}

std::size_t AudioInput::working_bytes() const noexcept {
    return sizeof(*this) + (samples_.capacity() + taps_.capacity()) * sizeof(double) +
        validity_.capacity() * sizeof(std::uint8_t);
}

std::uint64_t AudioInput::project(std::span<const float> pcm, std::vector<Complex>& output,
                                  std::span<const std::uint8_t> valid,
                                  std::vector<std::uint8_t>* output_valid) {
    if ((!valid.empty() && (valid.size() != pcm.size() || !output_valid)) ||
        pcm.size() > std::numeric_limits<std::uint64_t>::max() - origin_ - input_)
        throw Error("Invalid Robust audio projection chunk extent");
    for (std::size_t i = 0; i < pcm.size(); ++i) {
        if (!valid.empty() && valid[i] > 1) throw Error("Invalid Robust audio sample validity");
        if ((valid.empty() || valid[i]) && !std::isfinite(pcm[i]))
            throw Error("Robust capture returned nonfinite audio");
    }
    const auto prehistory = length_ - std::min<std::uint64_t>(length_, input_);
    if (!output_valid && invalid_ > prehistory)
        throw Error("Robust audio projection requires a validity mask while the FIR contains an erasure");
    output.clear();
    if (output_valid) output_valid->clear();
    std::uint64_t first = origin_ + (input_ > half_ ? input_ - half_ : 0);
    for (std::size_t row = 0; row < pcm.size(); ++row) {
        const bool observed = valid.empty() || valid[row];
        if (!validity_[at_]) --invalid_;
        if (!observed) ++invalid_;
        validity_[at_] = static_cast<std::uint8_t>(observed);
        samples_[at_] = samples_[at_ + length_] = observed ? pcm[row] : 0;
        if (++at_ == length_) at_ = 0;
        if (input_ >= length_ - 1) {
            const auto center = input_ - half_;
            const auto* history = samples_.data() + at_ + half_;
            double quadrature = 0;
            for (std::size_t i = 0; i < taps_.size(); ++i) {
                const auto offset = static_cast<std::ptrdiff_t>(2 * i + 1);
                quadrature += taps_[i] * (history[-offset] - history[offset]);
            }
            if (output.empty()) first = origin_ + center;
            output.push_back(Complex{*history, quadrature} * std::numbers::sqrt2 / 2. *
                             std::conj(carrier(center, carrier_, rate_)));
            if (output_valid) output_valid->push_back(static_cast<std::uint8_t>(invalid_ == 0));
        }
        ++input_;
    }
    return first;
}

} // namespace datapump::live::detail
