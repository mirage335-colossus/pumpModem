#include "datapump/robust_audio_frontend.hpp"

#include "search_fft.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <vector>

namespace datapump::live::detail {
namespace {
using triad::v3::Complex;

constexpr std::size_t calibration_frames = 16;
constexpr double calibration_margin_db = 3.;

std::size_t calibration_frame_size(std::uint32_t rate) {
    if (rate < 64 || rate > 192000)
        throw Error("Robust audio noise calibration requires a supported sample rate");
    const auto target = static_cast<std::size_t>((rate + 7U) / 8U);
    const auto size = std::bit_ceil(std::max<std::size_t>(32, target));
    if (!size || size > std::numeric_limits<std::size_t>::max() / calibration_frames)
        throw Error("Robust audio noise calibration size is not representable");
    return size;
}

double median(std::array<double, calibration_frames> values) {
    const auto middle = values.begin() + calibration_frames / 2;
    std::nth_element(values.begin(), middle, values.end());
    const auto upper = *middle;
    const auto lower = *std::max_element(values.begin(), middle);
    return (lower + upper) / 2;
}

Complex carrier(std::uint64_t sample, double frequency, std::uint32_t rate) {
    const auto cycles = std::remainder(static_cast<long double>(sample) * frequency / rate, 1.L);
    return std::polar(1., static_cast<double>(2 * std::numbers::pi_v<long double> * cycles));
}
}

std::size_t noise_calibration_samples(std::uint32_t rate) {
    return calibration_frames * calibration_frame_size(rate);
}

std::size_t noise_calibration_workspace_bytes(std::uint32_t rate) {
    const auto n = calibration_frame_size(rate);
    const auto bins = n / 2 + 1;
    constexpr auto max = std::numeric_limits<std::size_t>::max();
    const auto add = [](std::size_t a, std::size_t b) {
        if (b > std::numeric_limits<std::size_t>::max() - a)
            throw Error("Robust audio noise calibration workspace is not representable");
        return a + b;
    };
    // FFT input, plan twiddles, PSD, frame statistics and
    // caller-owned PCM are all live at their high-water mark here.
    std::size_t bytes = noise_calibration_samples(rate) * sizeof(float);
    if (n > max / (2 * sizeof(std::complex<double>)))
        throw Error("Robust audio noise calibration workspace is not representable");
    bytes = add(bytes, 2 * n * sizeof(std::complex<double>) - sizeof(std::complex<double>));
    if (bins > max / sizeof(double))
        throw Error("Robust audio noise calibration workspace is not representable");
    bytes = add(bytes, bins * sizeof(double));
    return add(bytes, 2 * calibration_frames * sizeof(double));
}

NoiseCalibration calibrate_noise_dbfs_hz(std::span<const float> pcm, std::uint32_t rate,
                                         std::stop_token stop) {
    using datapump::modem::detail::search_fft::DoublePlan;
    using datapump::modem::detail::search_fft::fft;
    const auto n = calibration_frame_size(rate);
    const auto required = calibration_frames * n;
    if (pcm.size() < required)
        throw Error("Robust noise calibration needs a few seconds of captured audio");

    double peak = 0, sum = 0;
    for (std::size_t i = 0; i < required; ++i) {
        if ((i & 4095U) == 0 && stop.stop_requested())
            throw Error("Robust audio noise calibration was cancelled");
        const auto sample = static_cast<double>(pcm[i]);
        if (!std::isfinite(sample))
            throw Error("Robust audio noise calibration received nonfinite audio");
        peak = std::max(peak, std::abs(sample));
        sum += sample;
    }
    if (peak >= .999)
        throw Error("Robust audio input is clipping; reduce the input level before calibration");
    const auto dc_offset = sum / static_cast<double>(required);

    DoublePlan plan(n, stop);
    std::vector<std::complex<double>> frame(n);
    std::vector<double> averaged(n / 2 + 1, 0);
    std::array<double, calibration_frames> frame_power{};
    double window_power = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const auto w = .5 - .5 * std::cos(2 * std::numbers::pi * static_cast<double>(i) /
                                          static_cast<double>(n - 1));
        window_power += w * w;
    }
    for (std::size_t f = 0; f < calibration_frames; ++f) {
        if (stop.stop_requested()) throw Error("Robust audio noise calibration was cancelled");
        const auto start = f * n;
        double power = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const auto sample = static_cast<double>(pcm[start + i]) - dc_offset;
            power += sample * sample;
            const auto w = .5 - .5 * std::cos(2 * std::numbers::pi * static_cast<double>(i) /
                                              static_cast<double>(n - 1));
            frame[i] = {sample * w, 0};
        }
        frame_power[f] = power / static_cast<double>(n);
        fft(frame, false, plan, stop);
        for (std::size_t k = 0; k < averaged.size(); ++k) {
            const auto one_sided = (k == 0 || k == n / 2) ? 1. : 2.;
            averaged[k] += one_sided * std::norm(frame[k]) /
                           (static_cast<double>(rate) * window_power * calibration_frames);
        }
    }
    // A single frame >9 dB above the median indicates the captured level was
    // changing enough that one stationary calibration is not representative.
    const auto typical_power = median(frame_power);
    if (!(typical_power > 0))
        throw Error("Robust audio input is silent; connect an audio source before calibration");
    const auto largest_frame = *std::max_element(frame_power.begin(), frame_power.end());
    if (largest_frame > typical_power * 8.)
        throw Error("Robust audio input level is unstable; keep the channel idle during calibration");

    const auto density = *std::max_element(averaged.begin(), averaged.end());
    if (!(density > 0) || !std::isfinite(density))
        throw Error("Robust audio noise calibration could not measure a usable noise floor");
    return {10 * std::log10(density) + calibration_margin_db, dc_offset};
}

float real_pcm(Complex baseband, std::uint64_t sample, double if_hz, std::uint32_t rate, double scale) {
    return static_cast<float>(std::sqrt(2.) * (baseband * carrier(sample, if_hz, rate)).real() * scale);
}

std::size_t AudioInput::required_halo_samples(std::uint32_t rate, double carrier_hz,
                                             double lower_hz, double upper_hz) {
    if (!rate || !std::isfinite(carrier_hz) || !std::isfinite(lower_hz) || !std::isfinite(upper_hz) ||
        lower_hz <= 0 || lower_hz > carrier_hz || upper_hz < carrier_hz || upper_hz >= rate / 2.)
        throw Error("Invalid Robust audio projection band");
    const auto edge = std::min(lower_hz, rate / 2. - upper_hz);
    const auto half = std::ceil(8. * rate / edge);
    if (!std::isfinite(half) || half < 1 || half > 2048)
        throw Error("Robust live audio requires more separation from DC or Nyquist");
    return static_cast<std::size_t>(half);
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
    : rate_(rate), carrier_(carrier_hz), origin_(first_input_sample),
      half_(required_halo_samples(rate, carrier_hz, lower_hz, upper_hz)) {
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

void AudioInput::set_dc_offset(double offset) {
    if (input_ || !std::isfinite(offset) || std::abs(offset) >= .999)
        throw Error("Invalid Robust audio projection DC offset");
    dc_offset_ = offset;
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
        samples_[at_] = samples_[at_ + length_] = observed ? pcm[row] - dc_offset_ : 0;
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
