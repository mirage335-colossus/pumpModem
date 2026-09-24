#pragma once
#include "datapump/modem.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <numbers>
#include <span>
#include <vector>

namespace datapump::gui::plots {
inline constexpr std::size_t waveform_kernel_radius = 32;
// Keep the original samples. A useful timebase prevents hundreds of carrier
// cycles being rasterized into a few hundred pixels as an apparent square wave.
// Optional captured context on either side avoids extrapolating the displayed
// trace when band-limited reconstruction is used. Overview retains all samples.
inline std::span<const float> waveform_window(std::span<const float> samples,
                                             const modem::Config& config, double zoom = 1,
                                             std::size_t context = 0) {
    if (samples.size() < 2) return samples;
    if (!(zoom > 0) || !std::isfinite(zoom)) throw Error("invalid waveform zoom");
    const auto count = config.carrier_hz > 0 ?
        std::ceil(4. * config.sample_rate / config.carrier_hz * zoom) + 1 :
        static_cast<double>(samples.size());
    const auto size = static_cast<std::size_t>(std::clamp(count, 2., static_cast<double>(samples.size())));
    const auto guard = context <= (samples.size() - size) / 2 ? context : 0;
    return samples.subspan(samples.size() - size - guard, size);
}

// A line between sparse PCM samples visibly flattens a perfectly clean carrier.
// Reconstruct the sampled signal with a finite Blackman-windowed sinc instead;
// this uses no knowledge of the carrier and preserves all sample knots exactly.
// Up to 64 measured samples contribute to each interpolated point. Near an
// incomplete capture boundary, use the available samples and retain unity DC
// gain. The usual view has full captured guards on both sides.
// Return an empty result for a dense overview; its raw min/max envelope is more
// faithful than a subpixel interpolated trace. Work and allocation are bounded
// by the display width, with a hard cap of 8,193 vertices.
inline std::vector<double> waveform_reconstruction(std::span<const float> samples,
                                                  std::size_t first, std::size_t count,
                                                  std::size_t width) {
    if (first > samples.size() || count > samples.size() - first) throw Error("invalid waveform window");
    if (count < 2 || count > width || count > 8193) return {};
    const auto subdivisions = std::max<std::size_t>(1, 2 * std::min<std::size_t>(width, 4096) / (count - 1));
    const auto vertices = (count - 1) * subdivisions + 1;
    std::vector<double> result(vertices);
    constexpr double radius = static_cast<double>(waveform_kernel_radius);
    for (std::size_t i = 0; i < count; ++i) result[i * subdivisions] = samples[first + i];
    // Reuse each fractional-phase kernel across the window, so repainting does
    // not calculate trigonometric functions once for every sample/vertex pair.
    for (std::size_t phase = 1; phase < subdivisions; ++phase) {
        std::array<double, 2 * waveform_kernel_radius> weights{};
        const auto fraction = static_cast<double>(phase) / static_cast<double>(subdivisions);
        for (std::size_t tap = 0; tap < weights.size(); ++tap) {
            const auto distance = radius - 1 - static_cast<double>(tap) + fraction;
            const auto angle = std::numbers::pi * distance;
            const auto window = .42 + .5 * std::cos(angle / radius) + .08 * std::cos(2 * angle / radius);
            weights[tap] = std::sin(angle) / angle * window;
        }
        for (std::size_t i = 0; i < count - 1; ++i) {
            const auto base = first + i;
            const auto begin = base >= waveform_kernel_radius - 1 ? base - (waveform_kernel_radius - 1) : 0;
            const auto end = base + std::min(waveform_kernel_radius, samples.size() - base - 1) + 1;
            auto tap = waveform_kernel_radius - 1 - (base - begin);
            double sum = 0, weight_sum = 0;
            for (auto sample = begin; sample < end; ++sample, ++tap) {
                sum += samples[sample] * weights[tap];
                weight_sum += weights[tap];
            }
            result[i * subdivisions + phase] = sum / weight_sum;
        }
    }
    return result;
}
struct Column { float low, high, first, last; };
inline std::vector<Column> waveform_columns(std::span<const float> samples, std::size_t width) {
    const auto count = std::min(samples.size(), width);
    std::vector<Column> result;
    result.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto bucket = samples.subspan(i * samples.size() / count,
            (i + 1) * samples.size() / count - i * samples.size() / count);
        const auto [low, high] = std::minmax_element(bucket.begin(), bucket.end());
        result.push_back({*low, *high, bucket.front(), bucket.back()});
    }
    return result;
}

// Peak pooling keeps every FFT bin, including a narrow carrier between display
// columns. Rows retain measured dB values: one shared color scale applies to
// the whole history instead of independently recoloring each row's noise floor.
class SpectrumHistory {
public:
    static constexpr std::size_t capacity = 160;
    void clear() { rows_.clear(); upper_db_ = max_hz_ = 0; }
    void push(std::span<const double> bins, double bin_hz) {
        if (bins.empty()) return;
        if (!(bin_hz > 0) || !std::isfinite(bin_hz)) throw Error("invalid spectrum frequency scale");
        const auto edge = bin_hz * static_cast<double>(bins.size() - 1);
        if (!rows_.empty() && std::abs(edge - max_hz_) > 1e-9 * std::max(1., edge)) clear();
        max_hz_ = edge;
        const auto columns = std::min<std::size_t>(256, bins.size());
        std::vector<double> row(columns, -240.);
        for (std::size_t i = 0; i < columns; ++i) {
            const auto first = i * bins.size() / columns, last = (i + 1) * bins.size() / columns;
            for (auto bin = first; bin < last; ++bin)
                if (std::isfinite(bins[bin])) row[i] = std::max(row[i], bins[bin]);
            if (row[i] > upper_db_) upper_db_ = std::ceil(row[i] / 20) * 20;
        }
        if (rows_.size() == capacity) rows_.pop_front();
        rows_.push_back(std::move(row));
    }
    double intensity(double db) const { return std::clamp((db - lower_db()) / 100., 0., 1.); }
    double lower_db() const { return upper_db_ - 100; }
    double upper_db() const { return upper_db_; }
    double max_hz() const { return max_hz_; }
    const std::deque<std::vector<double>>& rows() const { return rows_; }
private:
    std::deque<std::vector<double>> rows_;
    double upper_db_ = 0, max_hz_ = 0;
};
}
