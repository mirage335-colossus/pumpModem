#pragma once
#include "datapump/modem.hpp"
#include <algorithm>
#include <cmath>
#include <deque>
#include <span>
#include <vector>

namespace datapump::gui::plots {
// Keep the original samples. A useful timebase prevents hundreds of carrier
// cycles being rasterized into a few hundred pixels as an apparent square wave.
inline std::span<const float> waveform_window(std::span<const float> samples,
                                             const modem::Config& config, double zoom = 1) {
    if (samples.size() < 2) return samples;
    if (!(zoom > 0) || !std::isfinite(zoom)) throw Error("invalid waveform zoom");
    const auto count = config.carrier_hz > 0 ?
        std::ceil(12. * config.sample_rate / config.carrier_hz * zoom) + 1 :
        static_cast<double>(samples.size());
    return samples.last(static_cast<std::size_t>(std::clamp(count, 2., static_cast<double>(samples.size()))));
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
        if (rows_.size() == 160) rows_.pop_front();
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
