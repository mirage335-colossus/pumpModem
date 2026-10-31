#pragma once

// Evaluator-only matched-filter EVM. The receiver never reads this result,
// pulse, source symbols, physical mask or fixed clock gauge. It measures the
// received 38--40 s APSK probe, including uncertain output and hop edges;
// it cannot measure samples absent from the capture or invented FIR padding.
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace helper_complete_apsk_metrics {
constexpr double tau = 2 * std::numbers::pi;
enum class Region : unsigned { beginning, interior, end, boundary, all, count };
enum class Selection : unsigned { all, supported, uncertain, common, count };
constexpr unsigned region_count = static_cast<unsigned>(Region::count);
constexpr unsigned selection_count = static_cast<unsigned>(Selection::count);

struct Options {
    double sample_rate = 24000, dwell_seconds = .4, edge_seconds = .06;
    double probe_hz = 1600, mix_reference_hz = 2400, fixed_gauge_seconds = 0;
    // Output sample n is dated at n/sample_rate + this receiver-provided
    // source-time offset. This is distinct from the evaluator-only gauge.
    double output_time_offset_seconds = 0, other_output_time_offset_seconds = 0;
    double capture_first_seconds = 38, capture_end_seconds = 40;
    double boundary_uncertainty_seconds = .008;
    double common_date_tolerance_samples = 1e-5;
    // Conditional clock-rate bound; reject a footprint that jumps over an
    // unrepresented capture interval rather than fabricating signal there.
    double maximum_input_stride_samples = 1.5;
    double other_fixed_gauge_seconds = std::numeric_limits<double>::quiet_NaN();
    unsigned samples_per_symbol = 80, span_symbols = 8;
    unsigned first_symbol = 8, end_symbol = 592;
};
struct Statistic {
    std::uint64_t symbols = 0, footprint_crosses_nominal_hop = 0;
    std::uint64_t footprint_boundary_ambiguous = 0;
    double noisy_evm = 0, clean_evm = 0;
    // Preserve the sums so independently processed contiguous capture
    // segments can be combined without assuming equal APSK symbol power.
    double reference_power = 0, noisy_error_power = 0, clean_error_power = 0;
};
struct Result {
    std::array<std::array<Statistic, selection_count>, region_count> region{};
    std::uint64_t considered = 0, footprint_unavailable = 0;
    std::uint64_t common_date_mismatch = 0, common_gauge_mismatch = 0, common_output_time_mismatch = 0;
};

// Trace must expose first/noisy/clean/input_dates/supported/physically_disturbed
// and contains()/support(), as in the existing standalone delayed test.
// Physical class 1 is disturbed but still captured; 2=unknown and 3=missing
// cannot certify a complete matched-filter footprint and are excluded. Pulse
// and symbol callbacks are evaluator-only known waveform parameters. For
// common selection the whole output footprint, source dates, and fixed gauge
// must match the other trace; it never fits phase or timing to the probe.
template<class Trace, class Pulse, class Symbol>
Result evaluate(const Trace& trace, const Options& o, double probe_amplitude,
                Pulse pulse, Symbol symbol, const Trace* other = nullptr) {
    if (!(std::isfinite(o.sample_rate) && o.sample_rate > 0 &&
          std::isfinite(o.dwell_seconds) && o.dwell_seconds > 0 &&
          std::isfinite(o.edge_seconds) && o.edge_seconds > 0 && o.edge_seconds * 2 < o.dwell_seconds &&
          std::isfinite(o.probe_hz) && std::isfinite(o.mix_reference_hz) &&
          std::isfinite(o.fixed_gauge_seconds) && std::isfinite(o.output_time_offset_seconds) &&
          std::isfinite(o.other_output_time_offset_seconds) && std::isfinite(o.capture_first_seconds) &&
          std::isfinite(o.capture_end_seconds) && o.capture_first_seconds < o.capture_end_seconds &&
          std::isfinite(o.boundary_uncertainty_seconds) && o.boundary_uncertainty_seconds >= 0 &&
          std::isfinite(o.common_date_tolerance_samples) && o.common_date_tolerance_samples >= 0 &&
          std::isfinite(o.maximum_input_stride_samples) && o.maximum_input_stride_samples > 0 &&
          std::isfinite(probe_amplitude) && probe_amplitude > 0 && o.samples_per_symbol > 0 &&
          o.samples_per_symbol <= 4096 && o.span_symbols > 0 && o.span_symbols <= 16 &&
          o.first_symbol < o.end_symbol && o.end_symbol <= 100000))
        throw std::invalid_argument("invalid complete APSK metrology options");
    const auto size = trace.noisy.size();
    if (trace.clean.size() != size || trace.input_dates.size() != size ||
        trace.supported.size() != size || trace.physically_disturbed.size() != size)
        throw std::invalid_argument("incomplete APSK trace");
    if (other && !std::isfinite(o.other_fixed_gauge_seconds))
        throw std::invalid_argument("missing comparison gauge");
    if (other && (other->input_dates.size() != other->noisy.size() ||
                  other->supported.size() != other->noisy.size()))
        throw std::invalid_argument("incomplete common APSK trace");
    const bool common_gauge = !other || o.fixed_gauge_seconds == o.other_fixed_gauge_seconds;
    const bool common_output_time = !other || o.output_time_offset_seconds == o.other_output_time_offset_seconds;
    const int span = static_cast<int>(o.samples_per_symbol * o.span_symbols);
    // Bounded stack use and evaluation work even for malformed options.
    if (span > 4096) throw std::invalid_argument("APSK aperture too large");
    std::array<double, 8193> taps{};
    double energy = 0;
    for (int j = -span; j <= span; ++j) {
        const double v = pulse(static_cast<double>(j) / o.samples_per_symbol);
        if (!std::isfinite(v)) throw std::invalid_argument("nonfinite APSK pulse");
        taps[static_cast<std::size_t>(j + span)] = v; energy += v * v;
    }
    if (!(energy > 0) || !std::isfinite(energy)) throw std::invalid_argument("empty APSK pulse");
    const double scale = std::sqrt(energy / o.samples_per_symbol) / energy;
    const double fixed_phase = tau * (o.mix_reference_hz - o.probe_hz) * o.fixed_gauge_seconds;
    struct Accum { Statistic result; double expected = 0, noisy = 0, clean = 0; };
    std::array<std::array<Accum, selection_count>, region_count> accum{};
    Result result;
    for (unsigned k = o.first_symbol; k < o.end_symbol; ++k) {
        ++result.considered;
        const double center = (o.capture_first_seconds + o.fixed_gauge_seconds - o.output_time_offset_seconds) * o.sample_rate +
                              static_cast<double>(k) * o.samples_per_symbol;
        if (!(center >= span) || center >= static_cast<double>(UINT64_MAX - span - 2)) {
            ++result.footprint_unavailable; continue;
        }
        const auto integer = static_cast<std::uint64_t>(std::floor(center));
        const double fraction = center - std::floor(center);
        const auto first = integer - static_cast<std::uint64_t>(span);
        const auto last = integer + static_cast<std::uint64_t>(span) + (fraction > 0 ? 1 : 0);
        if (!trace.contains(first) || !trace.contains(last)) { ++result.footprint_unavailable; continue; }
        bool present = true, supported = true, common = other && common_gauge && common_output_time;
        long double start_date = 0, end_date = 0, center_date = 0, previous_date = 0;
        for (std::uint64_t n = first; n <= last; ++n) {
            const auto idx = static_cast<std::size_t>(n - trace.first);
            const auto date = trace.input_dates[idx];
            if (!std::isfinite(date) || date / o.sample_rate < o.capture_first_seconds ||
                date / o.sample_rate >= o.capture_end_seconds || trace.physically_disturbed[idx] >= 2) {
                present = false; break;
            }
            if (n != first && (date <= previous_date || date - previous_date > o.maximum_input_stride_samples)) {
                present = false; break;
            }
            previous_date = date;
            if (n == first) start_date = date;
            if (n == last) end_date = date;
            if (n == integer) center_date = date;
            supported = supported && trace.supported[idx] != 0;
            if (common) {
                if (!other->contains(n) || !other->support(n)) common = false;
                else if (!std::isfinite(other->input_dates[static_cast<std::size_t>(n - other->first)]) ||
                         std::abs(date - other->input_dates[static_cast<std::size_t>(n - other->first)]) > o.common_date_tolerance_samples) {
                    common = false; ++result.common_date_mismatch;
                }
            }
        }
        if (!present || end_date < start_date) { ++result.footprint_unavailable; continue; }
        if (other && !common_gauge) ++result.common_gauge_mismatch;
        if (other && !common_output_time) ++result.common_output_time_mismatch;
        const long double dwell_samples = o.dwell_seconds * o.sample_rate;
        const auto dwell = std::floor(center_date / dwell_samples);
        const double local = static_cast<double>((center_date - dwell * dwell_samples) / o.sample_rate);
        const auto region = local < o.edge_seconds ? Region::beginning :
            (local >= o.dwell_seconds - o.edge_seconds ? Region::end : Region::interior);
        const bool crosses = std::floor(start_date / dwell_samples) != std::floor(end_date / dwell_samples);
        const long double uncertainty = o.boundary_uncertainty_seconds * o.sample_rate;
        const bool ambiguous = std::floor((start_date - uncertainty) / dwell_samples) !=
                               std::floor((end_date + uncertainty) / dwell_samples);
        std::complex<double> noisy{}, clean{};
        for (int j = -span; j <= span; ++j) {
            const auto n = static_cast<std::uint64_t>(static_cast<std::int64_t>(integer) + j);
            const auto mix = [&](std::uint64_t x) { return std::polar(1., -tau * std::remainder(o.probe_hz * x / o.sample_rate + o.probe_hz * o.output_time_offset_seconds, 1.) - fixed_phase); };
            auto a = std::complex<double>(trace.noisy[static_cast<std::size_t>(n - trace.first)]) * mix(n);
            auto b = std::complex<double>(trace.clean[static_cast<std::size_t>(n - trace.first)]) * mix(n);
            if (fraction > 0) {
                const auto idx = static_cast<std::size_t>(n + 1 - trace.first);
                const auto next = mix(n + 1);
                a = (1 - fraction) * a + fraction * std::complex<double>(trace.noisy[idx]) * next;
                b = (1 - fraction) * b + fraction * std::complex<double>(trace.clean[idx]) * next;
            }
            noisy += a * taps[static_cast<std::size_t>(j + span)];
            clean += b * taps[static_cast<std::size_t>(j + span)];
        }
        const std::complex<double> expected = probe_amplitude * symbol(k);
        const double power = std::norm(expected);
        if (!std::isfinite(power)) throw std::invalid_argument("nonfinite APSK symbol");
        const auto append = [&](Region r, Selection s) {
            auto& x = accum[static_cast<unsigned>(r)][static_cast<unsigned>(s)];
            ++x.result.symbols; x.result.footprint_crosses_nominal_hop += crosses;
            x.result.footprint_boundary_ambiguous += ambiguous;
            x.expected += power; x.noisy += std::norm(noisy * scale - expected);
            x.clean += std::norm(clean * scale - expected);
        };
        const auto add_regions = [&](Selection selection) {
            append(region, selection); append(Region::all, selection);
            if (region != Region::interior) append(Region::boundary, selection);
        };
        add_regions(Selection::all); add_regions(supported ? Selection::supported : Selection::uncertain);
        if (supported && common) add_regions(Selection::common);
    }
    for (unsigned r = 0; r < region_count; ++r) for (unsigned s = 0; s < selection_count; ++s) {
        const auto& x = accum[r][s]; result.region[r][s] = x.result;
        result.region[r][s].reference_power = x.expected;
        result.region[r][s].noisy_error_power = x.noisy;
        result.region[r][s].clean_error_power = x.clean;
        if (x.expected > 0) {
            result.region[r][s].noisy_evm = std::sqrt(x.noisy / x.expected);
            result.region[r][s].clean_evm = std::sqrt(x.clean / x.expected);
        }
    }
    return result;
}
}
