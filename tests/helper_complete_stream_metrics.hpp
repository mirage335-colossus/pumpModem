#pragma once

// Evaluator-only metrology for the standalone delayed Helper study. It never
// supplies observations, masks, thresholds, decisions or coefficients to the
// receiver. The fixed gauge must be selected once before evaluating the probe.
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace helper_complete_stream_metrics {

constexpr double pi = std::numbers::pi;
constexpr double tau = 2 * pi;
enum class Region : std::size_t { beginning, interior, end, boundary, count };
enum class Selection : std::size_t { all, supported, uncertain, common, count };
constexpr std::size_t region_count = static_cast<std::size_t>(Region::count);
constexpr std::size_t selection_count = static_cast<std::size_t>(Selection::count);

struct Options {
    double sample_rate = 24000;
    double dwell_seconds = .4;
    double edge_seconds = .06;
    double probe_hz = 1600;
    double mix_reference_hz = 2400;
    double fixed_gauge_seconds = 0;
    double capture_begin_seconds = 38;
    double capture_end_seconds = 40;
    std::size_t frequency_lag = 128;
    std::size_t minimum_frequency_pairs_per_dwell = 256;
    // Require a common support comparison to refer to the same source date,
    // not merely the same nominal output index.
    double common_date_tolerance_samples = 1e-5;
    // Required when `other` is provided: both traces must retain the same
    // one fixed evaluator gauge, not be independently fitted or aligned.
    double other_fixed_gauge_seconds = std::numeric_limits<double>::quiet_NaN();
};

struct Statistic {
    std::uint64_t passed = 0, physically_clean = 0, physically_disturbed = 0;
    std::uint64_t physically_missing = 0, physically_unknown = 0, phase_samples = 0, noisy_phase_samples = 0;
    std::uint64_t frequency_dwells = 0;
    double phase_rms_deg = 0, phase_p95_deg = 0, phase_p99_deg = 0, phase_worst_deg = 0;
    double noisy_phase_rms_deg = 0, frequency_rms_hz = 0, frequency_worst_hz = 0;
};
struct Result {
    std::array<std::array<Statistic, selection_count>, region_count> region{};
    std::uint64_t invalid_capture_coordinates = 0;
    std::uint64_t common_map_compared = 0, common_map_mismatch = 0, common_gauge_mismatch = 0;
};

namespace detail {
struct Accum {
    Statistic out;
    long double phase_sum = 0, noisy_phase_sum = 0, frequency_sum = 0;
    // Fixed bins bound memory even for arbitrarily long probe traces. Percentile
    // values are reported as the conservative upper edge of a 0.25-degree bin.
    std::array<std::uint64_t, 720> histogram{};
    std::complex<long double> lag_sum{};
    std::uint64_t lag_pairs = 0;
    void finish_dwell(const Options& options) {
        if (lag_pairs >= options.minimum_frequency_pairs_per_dwell && std::norm(lag_sum) > 0) {
            const double frequency = static_cast<double>(std::arg(lag_sum)) * options.sample_rate /
                                     (tau * static_cast<double>(options.frequency_lag));
            frequency_sum += frequency * frequency;
            out.frequency_worst_hz = std::max(out.frequency_worst_hz, std::abs(frequency));
            ++out.frequency_dwells;
        }
        lag_sum = {}; lag_pairs = 0;
    }
    double percentile(double fraction) const {
        if (!out.phase_samples) return 0;
        const auto threshold = static_cast<std::uint64_t>(std::ceil(fraction * out.phase_samples));
        std::uint64_t seen = 0;
        for (std::size_t i = 0; i < histogram.size(); ++i) {
            seen += histogram[i];
            if (seen >= threshold) return static_cast<double>(i + 1) * .25;
        }
        return 180;
    }
    void finish(const Options& options) {
        finish_dwell(options);
        if (out.phase_samples) {
            out.phase_rms_deg = std::sqrt(static_cast<double>(phase_sum / out.phase_samples));
            out.phase_p95_deg = percentile(.95); out.phase_p99_deg = percentile(.99);
        }
        if (out.noisy_phase_samples)
            out.noisy_phase_rms_deg = std::sqrt(static_cast<double>(noisy_phase_sum / out.noisy_phase_samples));
        if (out.frequency_dwells)
            out.frequency_rms_hz = std::sqrt(static_cast<double>(frequency_sum / out.frequency_dwells));
    }
};
struct Lag {
    std::uint64_t output = 0, dwell = 0;
    std::complex<double> residual{};
    std::uint8_t regions = 0, selections = 0;
    bool usable = false;
};
}

// Trace is deliberately duck-typed to the existing standalone study: first,
// noisy, clean, input_dates, supported and physically_disturbed. Classification
// is 0=clean, 1=disturbed, 2=unknown, 3=physically missing. A missing event
// carries no usable probe energy and must never be invented by this evaluator.
// `other` selects the intersection of receiver-supported outputs on the same
// output grid; it does not fit or align either result. Frequency is derived
// from fixed-lag, within-dwell clean-twin correlations, then summarized per
// dwell. It is not an independent high-SNR receiver measurement.
template<class Trace>
Result evaluate(const Trace& trace, const Options& o, const Trace* other = nullptr) {
    if (!(std::isfinite(o.sample_rate) && o.sample_rate > 0 &&
          std::isfinite(o.dwell_seconds) && o.dwell_seconds > 0 &&
          std::isfinite(o.edge_seconds) && o.edge_seconds > 0 &&
          o.edge_seconds * 2 < o.dwell_seconds && std::isfinite(o.probe_hz) &&
          std::isfinite(o.mix_reference_hz) && std::isfinite(o.fixed_gauge_seconds) &&
          std::isfinite(o.capture_begin_seconds) && std::isfinite(o.capture_end_seconds) &&
          o.capture_begin_seconds < o.capture_end_seconds && o.frequency_lag > 0 &&
          o.frequency_lag <= 65536 && o.minimum_frequency_pairs_per_dwell > 0 &&
          std::isfinite(o.common_date_tolerance_samples) && o.common_date_tolerance_samples >= 0))
        throw std::invalid_argument("invalid complete-stream metrology options");
    const auto n = trace.noisy.size();
    if (trace.clean.size() != n || trace.input_dates.size() != n || trace.supported.size() != n ||
        trace.physically_disturbed.size() != n)
        throw std::invalid_argument("incomplete complete-stream trace");
    if (other && !std::isfinite(o.other_fixed_gauge_seconds))
        throw std::invalid_argument("common comparison needs the other fixed gauge");
    std::array<std::array<detail::Accum, selection_count>, region_count> work{};
    std::vector<detail::Lag> ring(o.frequency_lag);
    Result result; std::uint64_t previous_dwell = std::numeric_limits<std::uint64_t>::max();
    const double fixed_phase = tau * (o.mix_reference_hz - o.probe_hz) * o.fixed_gauge_seconds;
    const long double dwell_samples = static_cast<long double>(o.dwell_seconds) * o.sample_rate;
    for (std::size_t i = 0; i < n; ++i) {
        const long double pos = trace.input_dates[i];
        if (!std::isfinite(pos) || pos < 0) { ++result.invalid_capture_coordinates; continue; }
        const long double seconds = pos / o.sample_rate;
        if (seconds < o.capture_begin_seconds || seconds >= o.capture_end_seconds) continue;
        const auto dwell = static_cast<std::uint64_t>(std::floor(pos / dwell_samples));
        const double local = static_cast<double>((pos - dwell * dwell_samples) / o.sample_rate);
        if (dwell != previous_dwell) {
            if (previous_dwell != std::numeric_limits<std::uint64_t>::max())
                for (auto& row : work) for (auto& v : row) v.finish_dwell(o);
            previous_dwell = dwell;
        }
        const auto region = local < o.edge_seconds ? Region::beginning :
            (local >= o.dwell_seconds - o.edge_seconds ? Region::end : Region::interior);
        const std::uint8_t regions = static_cast<std::uint8_t>(1u << static_cast<unsigned>(region)) |
            static_cast<std::uint8_t>(region == Region::interior ? 0 : (1u << static_cast<unsigned>(Region::boundary)));
        const auto output = trace.first + i;
        const bool supported = trace.supported[i] != 0;
        bool common = false;
        if (supported && other && other->support(output)) {
            ++result.common_map_compared;
            const auto j = static_cast<std::size_t>(output - other->first);
            const bool same_gauge = std::abs(o.other_fixed_gauge_seconds - o.fixed_gauge_seconds) <= 1e-15;
            if (!same_gauge) ++result.common_gauge_mismatch;
            common = same_gauge && j < other->input_dates.size() && std::isfinite(other->input_dates[j]) &&
                std::abs(pos - other->input_dates[j]) <= o.common_date_tolerance_samples;
            if (same_gauge && !common) ++result.common_map_mismatch;
        }
        const std::uint8_t selections = static_cast<std::uint8_t>(1u << static_cast<unsigned>(Selection::all)) |
            static_cast<std::uint8_t>(1u << static_cast<unsigned>(supported ? Selection::supported : Selection::uncertain)) |
            static_cast<std::uint8_t>(common ? (1u << static_cast<unsigned>(Selection::common)) : 0);
        const auto physical = trace.physically_disturbed[i];
        const auto clean = std::complex<double>(trace.clean[i]);
        const auto noisy = std::complex<double>(trace.noisy[i]);
        const bool usable = physical < 2 && std::norm(clean) > 1e-12 && std::isfinite(clean.real()) && std::isfinite(clean.imag());
        const double reference_phase = tau * std::remainder(o.probe_hz * static_cast<double>(output) / o.sample_rate, 1.) + fixed_phase;
        const auto residual = clean * std::polar(1., -reference_phase);
        const auto noisy_residual = noisy * std::polar(1., -reference_phase);
        auto old = ring[static_cast<std::size_t>(output % o.frequency_lag)];
        for (std::size_t r = 0; r < region_count; ++r) if (regions & (1u << r))
            for (std::size_t s = 0; s < selection_count; ++s) if (selections & (1u << s)) {
                auto& a = work[r][s]; ++a.out.passed;
                if (physical == 0) ++a.out.physically_clean;
                else if (physical == 1) ++a.out.physically_disturbed;
                else if (physical == 3) ++a.out.physically_missing;
                else ++a.out.physically_unknown;
                if (!usable) continue;
                const double phase = std::abs(std::arg(residual)) * 180 / pi;
                ++a.out.phase_samples; a.phase_sum += phase * phase;
                if (std::isfinite(noisy.real()) && std::isfinite(noisy.imag()) && std::norm(noisy)>0) {
                    const double noisy_phase = std::abs(std::arg(noisy_residual)) * 180 / pi;
                    ++a.out.noisy_phase_samples; a.noisy_phase_sum += noisy_phase * noisy_phase;
                }
                a.out.phase_worst_deg = std::max(a.out.phase_worst_deg, phase);
                ++a.histogram[std::min<std::size_t>(719, static_cast<std::size_t>(phase * 4))];
                if (old.usable && old.output + o.frequency_lag == output && old.dwell == dwell &&
                    (old.regions & (1u << r)) && (old.selections & (1u << s))) {
                    const auto correlation = residual * std::conj(old.residual);
                    a.lag_sum += std::complex<long double>(correlation); ++a.lag_pairs;
                }
            }
        ring[static_cast<std::size_t>(output % o.frequency_lag)] = {output, dwell, residual, regions, selections, usable};
    }
    for (std::size_t r = 0; r < region_count; ++r) for (std::size_t s = 0; s < selection_count; ++s) {
        work[r][s].finish(o); result.region[r][s] = work[r][s].out;
    }
    return result;
}
}
