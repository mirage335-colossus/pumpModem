#include "ssb_frontend.hpp"
#include "datapump/execution.hpp"

#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace datapump::fast::detail {
namespace {

void check_parameters(std::uint32_t rate, double carrier, double cutoff) {
    if (rate < 44100 || rate > 192000 || !std::isfinite(carrier) || carrier <= 0 ||
        carrier >= rate / 2. || !std::isfinite(cutoff) || cutoff <= 0 ||
        cutoff >= double(rate) / double(rate / 12000) / 2.)
        throw std::invalid_argument("Invalid Fast SSB frontend rate, carrier or cutoff");
}

std::size_t filter_radius(std::uint32_t rate) {
    return static_cast<std::size_t>(std::lround(64. * rate / 12000.));
}

} // namespace

SsbFrontendGeometry make_ssb_frontend_geometry(std::uint32_t rate, double carrier, double cutoff) {
    check_parameters(rate, carrier, cutoff);
    SsbFrontendGeometry g{rate, carrier, cutoff, rate / 12000, {}};
    const auto radius = filter_radius(rate);
    g.taps.resize(2 * radius + 1);
    for (std::size_t offset = 0; offset <= radius; ++offset) {
        const double x = double(offset);
        const double angle = std::numbers::pi * x / double(radius);
        const double window = .42 + .5 * std::cos(angle) + .08 * std::cos(2 * angle);
        const double sinc = offset ? std::sin(2 * std::numbers::pi * cutoff * x / rate) /
            (std::numbers::pi * x) : 2 * cutoff / rate;
        // Explicit zero endpoints make the finite support exact.
        const double tap = offset == radius ? 0. : window * sinc;
        g.taps[radius - offset] = g.taps[radius + offset] = tap;
    }
    // Compensate the sum explicitly: long double is not wider than double on
    // every native target, including MSVC.
    double sum = 0, correction = 0;
    for (const auto tap : g.taps) {
        const auto add = tap - correction;
        const auto next = sum + add;
        correction = (next - sum) - add;
        sum = next;
    }
    for (auto& tap : g.taps) tap /= sum;
    return g;
}

SsbFrontend::SsbFrontend(SsbFrontendGeometry geometry, Sink sink, std::size_t chunk)
    : geometry_(std::move(geometry)), sink_(std::move(sink)) {
    check_parameters(geometry_.input_rate, geometry_.carrier_hz, geometry_.cutoff_hz);
    const auto radius = filter_radius(geometry_.input_rate);
    if (!sink_ || !chunk || chunk > 4096 || geometry_.decimation != geometry_.input_rate / 12000 ||
        geometry_.taps.size() != 2 * radius + 1)
        throw std::invalid_argument("Invalid Fast SSB frontend geometry or sink");
    for (std::size_t i = 0; i < geometry_.taps.size(); ++i)
        if (!std::isfinite(geometry_.taps[i]) || geometry_.taps[i] != geometry_.taps[2 * radius - i])
            throw std::invalid_argument("Fast SSB frontend requires finite symmetric FIR taps");
    if (std::abs(std::accumulate(geometry_.taps.begin(), geometry_.taps.end(), 0.) - 1) > 1e-12)
        throw std::invalid_argument("Fast SSB frontend requires DC-normalized FIR taps");
    history_.resize(2 * geometry_.taps.size());
    output_.resize(chunk);
    until_output_ = 2 * radius;
    oscillator_step_ = std::polar(1., -2 * std::numbers::pi * geometry_.carrier_hz / geometry_.input_rate);
}

void SsbFrontend::deliver() {
    if (!output_size_) return;
    const SsbFrontendOutput block{first_output_center_, geometry_.decimation,
        std::span<const Complex>(output_).first(output_size_)};
    output_size_ = 0;
    try {
        sink_(block);
    } catch (...) {
        finished_ = true;
        throw;
    }
}

std::size_t SsbFrontend::push(std::span<const float> input, std::stop_token stop) {
    if (finished_) throw std::logic_error("Fast SSB frontend is finished");
    if (input.size() > std::numeric_limits<std::uint64_t>::max() - input_samples_)
        throw std::length_error("Fast SSB frontend input extent overflow");
    const auto length = geometry_.taps.size();
    const auto radius = geometry_.radius();
    std::size_t consumed = 0;
    for (const auto value : input) {
        if (consumed % 256 == 0) {
            execution::checkpoint();
            if (stop.stop_requested()) break;
        }
        if (!std::isfinite(value)) {
            // Preserve prior valid observations even on an invalid next sample.
            deliver();
            throw std::invalid_argument("Fast SSB frontend received nonfinite PCM");
        }
        // Re-anchor at fixed input positions, never at push boundaries. This
        // bounds recurrence drift without a trigonometric call per input sample.
        if (input_samples_ % 4096 == 0) {
            const auto cycles = std::remainder(static_cast<long double>(input_samples_) *
                geometry_.carrier_hz / geometry_.input_rate, 1.L);
            oscillator_ = std::polar(1., static_cast<double>(-2 * std::numbers::pi_v<long double> * cycles));
        }
        const auto mixed = (2 * double(value)) * oscillator_;
        oscillator_ *= oscillator_step_;
        history_[history_at_] = history_[history_at_ + length] = mixed;
        if (++history_at_ == length) history_at_ = 0;
        ++input_samples_;
        ++consumed;
        if (until_output_) {
            --until_output_;
            continue;
        }
        const auto* samples = history_.data() + history_at_;
        auto filtered = geometry_.taps[radius] * samples[radius];
        for (std::size_t i = 0; i < radius; ++i)
            filtered += geometry_.taps[i] * (samples[i] + samples[length - 1 - i]);
        if (!output_size_) first_output_center_ = input_samples_ - 1 - radius;
        output_[output_size_++] = filtered;
        ++output_samples_;
        until_output_ = geometry_.decimation - 1;
        if (output_size_ == output_.size()) deliver();
    }
    deliver();
    return consumed;
}

std::size_t SsbFrontend::workspace_bytes() const noexcept {
    return sizeof(*this) + geometry_.taps.capacity() * sizeof(double) +
        (history_.capacity() + output_.capacity()) * sizeof(Complex);
}

} // namespace datapump::fast::detail
