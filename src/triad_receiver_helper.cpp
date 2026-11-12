#include "triad_receiver_helper.hpp"

#include "datapump/execution.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <limits>
#include <numeric>
#include <numbers>
#include <optional>
#include <utility>

namespace datapump::triad::v3::receiver_detail {
namespace {
constexpr double pi = std::numbers::pi;
constexpr double tau = 2 * pi;
constexpr std::size_t maximum_samples = 8'000'000;
constexpr std::size_t maximum_profile_points = 16'385;
constexpr std::size_t maximum_fit_bins = 300'000;
// Two pilots, each with its direct and Doppler-separated line.
constexpr std::size_t maximum_targets = helper_widest_wave;
// Frequency interval lists and result records. One fit has at most one
// region for every two of its grid points, and eight apertures' unions and
// their merge stay below 13 MB in the worst case.
constexpr std::size_t record_bytes = 16U << 20;
constexpr double coarse_guard = .1, minimum_coarse_seconds = 8;
constexpr double filter_half_support = .3, onset_padding = .15;
constexpr double frequency_seconds = 36, frequency_guard = .02;
constexpr double competitive_drop = 18;
using Interval = std::array<double, 2>;

void check_stop(std::stop_token stop) {
    if (stop.stop_requested()) throw Error("Helper acquisition cancelled");
    // Acquisition can share its owner's lanes with preparation.
    execution::checkpoint();
}
std::size_t checked_mul(std::size_t a, std::size_t b) {
    if (b && a > std::numeric_limits<std::size_t>::max() / b)
        throw Error("Helper workspace overflow");
    return a * b;
}
std::size_t next_power_two(std::size_t n) {
    std::size_t result = 1;
    while (result < n) result = checked_mul(result, 2);
    return result;
}
bool bounded_uncertainty(const ReceiverConfig& c) {
    for (double value : {c.clock_bound_ppm * 1e-6, c.endpoint_clock_bound_ppm * 1e-6, c.spread_delay_seconds,
                        c.delay_excursion_seconds, c.random_doppler_width_hz, c.spread_doppler_hz,
                        c.delay_motion_doppler_bound_hz, c.capture_tail_bound_seconds,
                        c.relative_phase_diffusion_rad_sqrt_s})
        if (!std::isfinite(value) || value < 0) return false;
    return true;
}
bool finite_calendar(const ReceiverSlotPlan& s) {
    return std::isfinite(s.arrival_lower_seconds) && std::isfinite(s.arrival_upper_seconds) &&
        s.arrival_lower_seconds <= s.arrival_upper_seconds && std::isfinite(s.helper_begin_seconds) &&
        std::isfinite(s.helper_end_seconds) && std::isfinite(s.source_start_seconds);
}
// A source-clock date reaches global receiver sample time at
// (capture_lead + arrival + source_date / tx_scale + settling + guard) * rx_scale.
// The Helper date axis subtracts the nominal global capture lead again.
// Both endpoint clocks are independently bounded. Multiplication by a
// positive receive scale is monotone in arrival, but its extremum changes
// clock endpoint when that intermediate date is negative. Evaluate the
// corners rather than assuming every absolute date follows time zero.
long double global_capture_lead(const ReceiverConfig& c, const ReceiverSlotPlan& s) {
    return s.window_capture_lead_seconds + static_cast<long double>(s.first_sample) / c.sample_rate_hz;
}
Interval observed_bounds(Interval arrival, long double source_date, double endpoint, long double lead,
                         double settling = 0, double guard = 0) {
    Interval result{std::numeric_limits<double>::infinity(),
                    -std::numeric_limits<double>::infinity()};
    for (const auto value : arrival) for (const auto tx : {1 - endpoint, 1 + endpoint})
        for (const auto rx : {1 - endpoint, 1 + endpoint}) {
            const double date = static_cast<double>((value + source_date / tx + settling + guard) * rx +
                                                    lead * (static_cast<long double>(rx) - 1));
            if (!std::isfinite(date)) throw Error("nonfinite public Helper clock envelope");
            result[0] = std::min(result[0], date);
            result[1] = std::max(result[1], date);
        }
    return result;
}
// Invert the same affine clock model over every endpoint combination. A
// first-order clock padding omits both capture lead and the receive divisor.
Interval arrival_bounds(Interval observed, long double source_date, double endpoint, long double lead) {
    Interval result{std::numeric_limits<double>::infinity(),
                    -std::numeric_limits<double>::infinity()};
    for (const auto value : observed) for (const auto tx : {1 - endpoint, 1 + endpoint})
        for (const auto rx : {1 - endpoint, 1 + endpoint}) {
            const auto inverse_rx = 1 / static_cast<long double>(rx);
            const double date = static_cast<double>(value * inverse_rx + lead * (inverse_rx - 1) - source_date / tx);
            if (!std::isfinite(date)) throw Error("nonfinite public Helper arrival envelope");
            result[0] = std::min(result[0], date);
            result[1] = std::max(result[1], date);
        }
    return result;
}
std::size_t prefix_samples(const ReceiverConfig& c, const ReceiverSlotPlan& s) {
    if (s.stop_sample < s.first_sample || !std::isfinite(c.sample_rate_hz) || c.sample_rate_hz <= 0)
        throw Error("invalid Helper window geometry");
    const auto samples = s.stop_sample - s.first_sample;
    if (samples > std::numeric_limits<std::size_t>::max()) throw Error("Helper window exceeds host extent");
    long double end_seconds = s.window_capture_lead_seconds + s.source_start_seconds +
        std::max(0., s.arrival_upper_seconds) + 1;
    if (bounded_uncertainty(c) && finite_calendar(s) && c.endpoint_clock_bound_ppm * 1e-6 < .1) {
        const auto latest = observed_bounds({s.arrival_lower_seconds, s.arrival_upper_seconds},
            s.source_start_seconds, c.endpoint_clock_bound_ppm * 1e-6, global_capture_lead(c, s))[1];
        end_seconds = std::max(end_seconds, latest + s.window_capture_lead_seconds + 1);
    }
    const long double end = std::ceil(end_seconds * c.sample_rate_hz);
    if (!std::isfinite(end)) throw Error("nonfinite Helper prefix");
    return end <= 0 ? 0 : end >= samples ? static_cast<std::size_t>(samples) : static_cast<std::size_t>(end);
}
// A job's value, or the failure a serial pass would have met at its position.
template<class T> struct Outcome {
    std::optional<T> value;
    std::exception_ptr failure;
    template<class Job> bool run(Job&& job) {
        try { value.emplace(job()); } catch (...) { failure = std::current_exception(); }
        return !failure;
    }
    T take() {
        if (failure) std::rethrow_exception(failure);
        return std::move(*value);
    }
};
// Runs independent jobs, at most `width` at once, and returns after them. A
// job records its own outcome and reports whether it succeeded. The caller
// replays values, early returns and failures in serial order, so nothing
// after the first failure is read and a serial pass stops there as before.
template<class Job> void each(const LaneWave& wave, std::size_t count, std::size_t width,
                              std::stop_token stop, Job&& job) {
    if (!wave || width <= 1 || count <= 1) {
        for (std::size_t index = 0; index < count; ++index) if (!job(index, stop)) return;
        return;
    }
    std::atomic<bool> failed = false;
    try {
        for (std::size_t first = 0; first < count && !failed; first += width)
            wave(std::min(width, count - first), [&](std::size_t index, std::stop_token job_stop) {
                if (!job(first + index, job_stop)) failed = true;
            });
    } catch (const Error&) {
        check_stop(stop);
        throw;
    }
}
std::vector<double> linspace(double lo, double hi, std::size_t n) {
    if (!n) throw Error("empty Helper lattice");
    std::vector<double> result(n, lo);
    if (n > 1) {
        const double step = (hi - lo) / static_cast<double>(n - 1);
        for (std::size_t i = 1; i + 1 < n; ++i) result[i] = lo + i * step;
        result.back() = hi;
    }
    return result;
}
std::vector<Interval> merge(std::vector<Interval> values) {
    std::sort(values.begin(), values.end());
    std::vector<Interval> result;
    for (const auto& v : values) {
        if (!result.empty() && v[0] <= result.back()[1]) result.back()[1] = std::max(result.back()[1], v[1]);
        else result.push_back(v);
    }
    return result;
}
std::vector<Interval> regions(const std::vector<double>& grid, const std::vector<double>& scores, double drop) {
    const double threshold = *std::max_element(scores.begin(), scores.end()) - drop;
    std::vector<Interval> result;
    for (std::size_t i = 0; i < scores.size();) {
        if (scores[i] < threshold) { ++i; continue; }
        const auto first = i;
        while (i + 1 < scores.size() && scores[i + 1] >= threshold) ++i;
        result.push_back({grid[first], grid[i++]});
    }
    return result;
}
struct Spectrum { std::vector<double> frequency, power; };
Spectrum periodogram(std::span<const Complex> input, double fs, std::stop_token stop) {
    const auto n = input.size();
    if (n < 2) throw Error("Helper spectral aperture too short");
    std::vector<Complex> spectrum(n);
    double energy = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if ((i & 4095U) == 0) check_stop(stop);
        const double w = .5 - .5 * std::cos(tau * static_cast<double>(i) / static_cast<double>(n - 1));
        // Preserve the canonical single-precision aperture before its FFT;
        // the native transform itself accumulates in double precision.
        const auto v = std::complex<float>(static_cast<float>(input[i].real()), static_cast<float>(input[i].imag())) * static_cast<float>(w);
        spectrum[i] = Complex(v.real(), v.imag());
        energy += w * w;
    }
    DftPlan plan(n, stop);
    std::vector<Complex> scratch(DftPlan::scratch_samples(n));
    plan.transform(spectrum, false, scratch, stop);
    Spectrum result;
    result.frequency.resize(n); result.power.resize(n);
    const auto split = (n + 1) / 2;
    for (std::size_t i = 0; i < n; ++i) {
        const auto j = (i + split) % n;
        const auto bin = static_cast<std::int64_t>(i) - static_cast<std::int64_t>(n / 2);
        result.frequency[i] = static_cast<double>(bin) * fs / n;
        const auto value = std::complex<float>(static_cast<float>(spectrum[j].real()), static_cast<float>(spectrum[j].imag()));
        result.power[i] = (static_cast<double>(value.real()) * value.real() + static_cast<double>(value.imag()) * value.imag()) / energy;
    }
    return result;
}
struct Profile { std::vector<double> scores, peaks; };
Profile whittle(std::span<const double> power, std::span<const double> frequencies,
                const std::vector<double>& grid, double separation, double sigma,
                unsigned iterations, std::stop_token stop) {
    const auto n = power.size();
    if (n < 2 || n != frequencies.size()) throw Error("Helper spectral fit needs two bins");
    const double sum = std::accumulate(power.begin(), power.end(), 0.);
    const double mean = std::max(sum / n, std::numeric_limits<double>::min());
    const double total = sum / mean;
    const double df = frequencies[1] - frequencies[0];
    const auto radius = static_cast<std::int64_t>(std::ceil(6 * sigma / df)) + 1;
    const auto offset = static_cast<std::int64_t>(std::nearbyint(separation / df));
    std::vector<std::int64_t> offsets;
    offsets.reserve(static_cast<std::size_t>(4 * radius + 2));
    for (auto k = -radius; k <= radius; ++k) { offsets.push_back(k); offsets.push_back(k + offset); }
    std::sort(offsets.begin(), offsets.end());
    offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
    if (offsets.size() > n) throw Error("Helper fitting window does not contain Gaussian cores");
    Profile out{std::vector<double>(grid.size()), std::vector<double>(grid.size())};
    std::vector<double> observed(offsets.size()), k0(offsets.size()), k1(offsets.size());
    constexpr double floor = 1e-6;
    const double outside = static_cast<double>(n - offsets.size());
    for (std::size_t g = 0; g < grid.size(); ++g) {
        if ((g & 31U) == 0) check_stop(stop);
        const auto center = static_cast<std::int64_t>(std::nearbyint((grid[g] - frequencies[0]) / df));
        double selected_sum = 0;
        for (std::size_t j = 0; j < offsets.size(); ++j) {
            const auto index = center + offsets[j];
            if (index < 0 || static_cast<std::uint64_t>(index) >= n)
                throw Error("Helper fitting window does not contain every Gaussian core");
            const double x = frequencies[static_cast<std::size_t>(index)] - grid[g];
            k0[j] = std::exp(-.5 * (x / sigma) * (x / sigma));
            k1[j] = std::exp(-.5 * ((x - separation) / sigma) * ((x - separation) / sigma));
            observed[j] = power[static_cast<std::size_t>(index)] / mean;
            selected_sum += observed[j];
        }
        const double outside_sum = total - selected_sum;
        double weight0 = .5, weight1 = .5, background = 1;
        for (unsigned iteration = 0; iteration < iterations; ++iteration) {
            double num0 = 0, num1 = 0, den0 = 0, den1 = 0;
            double num_background = 0, den_background = 0;
            for (std::size_t j = 0; j < offsets.size(); ++j) {
                const double inv = 1 / std::max(background + k0[j] * weight0 + k1[j] * weight1, floor);
                const double numerator = observed[j] * inv * inv;
                num0 += k0[j] * numerator; num1 += k1[j] * numerator;
                den0 += k0[j] * inv; den1 += k1[j] * inv;
                num_background += numerator; den_background += inv;
            }
            weight0 *= num0 / std::max(den0, floor); weight1 *= num1 / std::max(den1, floor);
            num_background += outside_sum / (background * background);
            den_background += outside / background;
            background = std::max(floor, background * num_background / std::max(den_background, floor));
        }
        double cost = outside * std::log(background) + outside_sum / background;
        for (std::size_t j = 0; j < offsets.size(); ++j) {
            const double variance = background + k0[j] * weight0 + k1[j] * weight1;
            cost += std::log(variance) + observed[j] / variance;
        }
        out.scores[g] = n - cost;
        out.peaks[g] = 1 + std::max(weight0, weight1) / background;
    }
    return out;
}
struct Fit { Profile profile; std::vector<double> grid; double step = 0, low = 0, high = 0; bool outside = false; };
Fit fit_pilot(const Spectrum& spectrum, double center, double bound, double audio,
              double separation, double sigma, double duration, double fs,
              unsigned iterations, std::stop_token stop) {
    Fit result;
    const double allowance = std::abs(center) * audio;
    result.low = -bound - allowance; result.high = bound + allowance;
    result.step = std::max(1 / duration, sigma / 4);
    const double points = std::max(2., std::ceil((result.high - result.low) / result.step) + 1);
    if (points > maximum_profile_points) throw Error("Helper profile work limit");
    result.grid = linspace(result.low, result.high, static_cast<std::size_t>(points));
    const double pad = 6 * sigma + 3 / duration;
    const double lo = center + result.low - pad, hi = center + result.high + separation + pad;
    if (lo <= -fs / 2 || hi >= fs / 2) { result.outside = true; return result; }
    const auto a = std::lower_bound(spectrum.frequency.begin(), spectrum.frequency.end(), lo);
    const auto b = std::upper_bound(spectrum.frequency.begin(), spectrum.frequency.end(), hi);
    const auto first = static_cast<std::size_t>(a - spectrum.frequency.begin());
    const auto count = static_cast<std::size_t>(b - a);
    if (iterations == 28 && count > maximum_fit_bins) throw Error("Helper fitting window work limit");
    std::vector<double> frequency(count);
    for (std::size_t i = 0; i < count; ++i) frequency[i] = spectrum.frequency[first + i] - center;
    result.profile = whittle(std::span(spectrum.power).subspan(first, count), frequency, result.grid,
                             separation, sigma, iterations, stop);
    return result;
}
// The aperture window that every refinement evaluation applies.
std::vector<double> refinement_window(std::size_t samples, std::stop_token stop) {
    std::vector<double> window(samples);
    for (std::size_t i = 0; i < samples; ++i) {
        if ((i & 4095U) == 0) check_stop(stop);
        window[i] = .5 - .5 * std::cos(tau * i / static_cast<double>(samples - 1));
    }
    return window;
}
double refine_frequency(std::span<const Complex> input, std::span<const double> window, double fs,
                        double frequency, std::stop_token stop) {
    const double duration = input.size() / fs, radius = 1 / duration;
    const double tolerance = 1 / (1000 * duration);
    auto objective = [&](double f) {
        Complex sum{};
        for (std::size_t i = 0; i < input.size(); ++i) {
            if ((i & 4095U) == 0) check_stop(stop);
            const double w = window[i];
            const double t = (static_cast<double>(i) - (input.size() - 1) / 2.) / fs;
            sum += input[i] * w * std::polar(1., -tau * std::remainder(f * t, 1.));
        }
        return -std::norm(sum);
    };
    // Bounded Brent minimization uses the same one-bin bracket, absolute
    // tolerance and 24-evaluation ceiling as the canonical Python stage.
    const double golden = .5 * (3 - std::sqrt(5.));
    const double root_epsilon = std::sqrt(2.2e-16);
    double a = frequency - radius, b = frequency + radius;
    double x = a + golden * (b - a), second = x, third = x;
    double value = objective(x), second_value = value, third_value = value;
    double previous_step = 0, step = 0;
    unsigned evaluations = 1;
    auto sign = [](double v) { return v < 0 ? -1. : 1.; };
    while (true) {
        const double midpoint = .5 * (a + b);
        const double tol = root_epsilon * std::abs(x) + tolerance / 3;
        if (std::abs(x - midpoint) <= 2 * tol - .5 * (b - a)) return x;
        bool use_golden = true;
        if (std::abs(previous_step) > tol) {
            const double r = (x - second) * (value - third_value);
            double q = (x - third) * (value - second_value);
            double p = (x - third) * q - (x - second) * r;
            q = 2 * (q - r);
            if (q > 0) p = -p;
            q = std::abs(q);
            const double saved = previous_step;
            previous_step = step;
            if (std::abs(p) < std::abs(.5 * q * saved) && p > q * (a - x) && p < q * (b - x)) {
                step = p / q;
                const double trial = x + step;
                if (trial - a < 2 * tol || b - trial < 2 * tol) step = tol * sign(midpoint - x);
                use_golden = false;
            }
        }
        if (use_golden) { previous_step = x >= midpoint ? a - x : b - x; step = golden * previous_step; }
        const double trial = x + sign(step) * std::max(std::abs(step), tol);
        const double trial_value = objective(trial);
        ++evaluations;
        if (trial_value <= value) {
            if (trial >= x) a = x; else b = x;
            third = second; third_value = second_value;
            second = x; second_value = value;
            x = trial; value = trial_value;
        } else {
            if (trial < x) a = trial; else b = trial;
            if (trial_value <= second_value || second == x) {
                third = second; third_value = second_value;
                second = trial; second_value = trial_value;
            } else if (trial_value <= third_value || third == x || third == second) {
                third = trial; third_value = trial_value;
            }
        }
        if (evaluations >= 24 || !std::isfinite(value)) throw Error("Helper persistent frequency refinement failed");
    }
}
std::vector<double> lowpass(std::size_t half, double fs, double cutoff) {
    std::vector<double> taps(checked_add(checked_mul(half, 2), 1));
    const double denominator = std::cyl_bessel_i(0., 8.);
    double sum = 0;
    for (std::size_t i = 0; i < taps.size(); ++i) {
        const double x = static_cast<double>(i) - half;
        const double z = half ? x / half : 0;
        const double window = std::cyl_bessel_i(0., 8 * std::sqrt(std::max(0., 1 - z * z))) / denominator;
        taps[i] = (x == 0 ? 2 * cutoff / fs : std::sin(tau * cutoff * x / fs) / (pi * x)) * window;
        sum += taps[i];
    }
    for (auto& value : taps) value /= sum;
    return taps;
}
// The transform plan and filter response shared by every pilot branch. Its
// radix-two length needs no transform scratch, so branches can share it.
struct BranchFilter {
    BranchFilter(std::size_t input_rows, const std::vector<double>& taps, std::stop_token stop)
        : total(checked_add(input_rows, taps.size()) - 1), plan(next_power_two(total), stop),
          kernel(next_power_two(total)) {
        std::copy(taps.begin(), taps.end(), kernel.begin());
        plan.transform(kernel, false, {}, stop);
    }
    std::size_t total;
    DftPlan plan;
    std::vector<Complex> kernel;
};

std::vector<Complex> filtered_branch(std::span<const Complex> input, const BranchFilter& filter,
                                     std::size_t decimation, double fs, double lead, double target,
                                     std::stop_token stop) {
    const auto size = filter.kernel.size();
    std::vector<Complex> samples(size);
    for (std::size_t i = 0; i < input.size(); ++i) {
        if ((i & 4095U) == 0) check_stop(stop);
        samples[i] = input[i] * std::polar(1., -tau * std::remainder(target * (i / fs - lead), 1.));
    }
    filter.plan.transform(samples, false, {}, stop);
    for (std::size_t i = 0; i < size; ++i) samples[i] *= filter.kernel[i];
    filter.plan.transform(samples, true, {}, stop);
    std::vector<Complex> result;
    result.reserve((filter.total - 1) / decimation + 1);
    for (std::size_t i = 0; i < filter.total; i += decimation) result.push_back(samples[i]);
    return result;
}
struct FrequencyResult { bool narrowed = false; std::string reason; std::vector<Interval> intervals; };
FrequencyResult final_frequency(const ReceiverConfig& c, const ReceiverSlotPlan& s,
                                std::span<const Complex> raw, std::span<const std::uint8_t> valid,
                                Interval arrival, double horizon, std::stop_token stop,
                                const LaneWave& wave) {
    FrequencyResult result;
    auto finish = [&](const char* reason) { result.reason = reason; return result; };
    const double fs = c.sample_rate_hz, lead = static_cast<double>(s.window_capture_lead_seconds);
    const double audio = c.clock_bound_ppm * 1e-6, endpoint = c.endpoint_clock_bound_ppm * 1e-6;
    const auto capture_lead = global_capture_lead(c, s);
    const double settling = std::max(c.spread_delay_seconds + c.delay_excursion_seconds, c.capture_tail_bound_seconds);
    const double begin = observed_bounds(arrival, static_cast<double>(s.helper_begin_seconds), endpoint,
                                         capture_lead, settling, frequency_guard)[1];
    const double data_start = observed_bounds(arrival, static_cast<double>(s.source_start_seconds), endpoint,
                                              capture_lead, 0, -frequency_guard)[0];
    const double end_limit = std::min(data_start,
        observed_bounds(arrival, static_cast<double>(s.helper_end_seconds), endpoint, capture_lead, 0, -frequency_guard)[0]);
    const double first_real = std::max(0., std::ceil((begin + lead) * fs));
    const double count_real = std::ceil(frequency_seconds * fs);
    if (count_real > maximum_samples) return finish("fft_work_limit");
    if (first_real > raw.size() || count_real > raw.size() - first_real || (first_real + count_real) / fs - lead > end_limit)
        return finish("insufficient_guaranteed_data_free_helper_support");
    const auto first = static_cast<std::size_t>(first_real), count = static_cast<std::size_t>(count_real);
    if (!valid.empty() && std::any_of(valid.begin() + first, valid.begin() + first + count, [](auto v) { return v != 1; }))
        return finish("known_erasure_in_helper_aperture");
    const double duration = count / fs;
    const double width = c.random_doppler_width_hz, separation = c.spread_doppler_hz;
    const double sigma = std::hypot(width / 2, .75 / duration);
    const auto spectrum = periodogram(raw.subspan(first, count), fs, stop);
    const double looks = std::max(1., std::ceil(horizon / .25));
    const double z = std::sqrt(2 * std::log(2 * looks / .001));
    const double q = c.relative_phase_diffusion_rad_sqrt_s;
    const double oscillator = 6 * q / (tau * std::sqrt(duration)) + z * q / (tau * std::sqrt(.25));
    // Weak-helper policy intentionally uses zero AM width allowance. The
    // selected physical modulation still remains in the complete input.
    const double common = .02 + 3 * width + c.delay_motion_doppler_bound_hz +
        separation * endpoint / (1 - endpoint) + oscillator;
    bool boundary = false, flat = false;
    std::vector<std::vector<Interval>> usable;
    const std::array pilots{c.helper.low_frequency_hz, c.helper.frequency_hz};
    std::array<Outcome<Fit>, 2> fits;
    each(wave, pilots.size(), pilots.size(), stop, [&](std::size_t index, std::stop_token job_stop) {
        return fits[index].run([&] {
            return fit_pilot(spectrum, pilots[index] - c.data_if_hz, c.frequency_bound_hz, audio, separation,
                             sigma, duration, fs, 28, job_stop);
        });
    });
    for (std::size_t index = 0; index < pilots.size(); ++index) {
        const double center = pilots[index] - c.data_if_hz;
        Fit fit;
        try { fit = fits[index].take(); }
        catch (const Error& error) {
            const std::string message = error.what();
            if (message == "Helper profile work limit") return finish("profile_work_limit");
            if (message == "Helper fitting window work limit") return finish("fit_window_work_limit");
            throw;
        }
        if (fit.outside) continue;
        const auto& scores = fit.profile.scores;
        const auto best = static_cast<std::size_t>(std::max_element(scores.begin(), scores.end()) - scores.begin());
        if (scores[best] < 18 || fit.profile.peaks[best] < 2) continue;
        const auto competitive = regions(fit.grid, scores, competitive_drop);
        if (scores[best] - *std::min_element(scores.begin(), scores.end()) <= competitive_drop) { flat = true; continue; }
        if (std::any_of(competitive.begin(), competitive.end(), [&](auto v) { return v[0] <= fit.low + fit.step || v[1] >= fit.high - fit.step; })) {
            boundary = true; continue;
        }
        const double allowance = common + std::abs(center) * audio + fit.step;
        std::vector<Interval> expanded;
        for (auto interval : competitive) expanded.push_back({interval[0] - allowance, interval[1] + allowance});
        usable.push_back(merge(std::move(expanded)));
    }
    if (flat) return finish("flat_profile");
    if (boundary) return finish("competitive_profile_at_prior_boundary");
    if (usable.empty()) return finish("no_usable_pilot");
    if (usable.size() == 2) {
        bool intersect = false;
        for (auto a : usable[0]) for (auto b : usable[1]) intersect |= std::max(a[0], b[0]) <= std::min(a[1], b[1]);
        if (!intersect) return finish("pilot_frequency_regions_inconsistent");
    }
    std::vector<Interval> all;
    for (const auto& pilot : usable) for (auto v : pilot) {
        const double lo = std::max(-c.frequency_bound_hz, v[0]), hi = std::min(c.frequency_bound_hz, v[1]);
        if (lo <= hi) all.push_back({lo, hi});
    }
    result.intervals = merge(std::move(all));
    double retained = 0;
    for (auto v : result.intervals) retained += v[1] - v[0];
    if (c.frequency_bound_hz <= 0 || result.intervals.empty() || retained / (2 * c.frequency_bound_hz) >= .8) {
        result.intervals.clear(); return finish("profile_or_allowance_too_broad");
    }
    result.narrowed = true;
    return finish("engineering_frequency_regions_selected");
}

// The guaranteed Helper-only support inside the prefix. It follows from the
// public calendar alone. Callers validate that calendar first.
struct Support {
    double settling = 0, end = 0;
    std::size_t first = 0, last = 0;
    bool enough = false;
};
Support coarse_support(const ReceiverConfig& c, const ReceiverSlotPlan& s, std::size_t prefix) {
    const double fs = c.sample_rate_hz, lead = static_cast<double>(s.window_capture_lead_seconds);
    const double endpoint = c.endpoint_clock_bound_ppm * 1e-6;
    const auto capture_lead = global_capture_lead(c, s);
    Support out;
    out.settling = std::max(c.spread_delay_seconds + c.delay_excursion_seconds, c.capture_tail_bound_seconds);
    const Interval arrival{s.arrival_lower_seconds, s.arrival_upper_seconds};
    const double begin = observed_bounds(arrival, static_cast<double>(s.helper_begin_seconds), endpoint,
                                         capture_lead, out.settling, coarse_guard)[1];
    out.end = std::min(observed_bounds(arrival, static_cast<double>(s.source_start_seconds), endpoint, capture_lead, 0, -coarse_guard)[0],
                       observed_bounds(arrival, static_cast<double>(s.helper_end_seconds), endpoint, capture_lead, 0, -coarse_guard)[0]);
    const double first_real = std::max(0., std::ceil((begin + lead) * fs));
    const double last_real = std::min(static_cast<double>(prefix), std::floor((out.end + lead) * fs));
    if (!std::isfinite(first_real) || !std::isfinite(last_real))
        throw Error("nonfinite public Helper support");
    out.enough = last_real >= first_real && (last_real - first_real) / fs >= minimum_coarse_seconds;
    if (out.enough) {
        out.first = static_cast<std::size_t>(first_real);
        out.last = static_cast<std::size_t>(last_real);
    }
    return out;
}
// The public onset search window and its grid.
struct OnsetWindow { double start = 0, earliest = 0, latest = 0; };
OnsetWindow onset_window(const ReceiverConfig& c, const ReceiverSlotPlan& s) {
    const double endpoint = c.endpoint_clock_bound_ppm * 1e-6;
    OnsetWindow out;
    out.start = static_cast<double>(s.helper_begin_seconds);
    const auto bounds = observed_bounds({s.arrival_lower_seconds, s.arrival_upper_seconds}, out.start, endpoint,
                                        global_capture_lead(c, s));
    out.earliest = bounds[0];
    out.latest = bounds[1];
    return out;
}
std::size_t onset_points(const OnsetWindow& window, double step) {
    const double points = std::max(1., std::ceil((window.latest - window.earliest) / step));
    // Any grid that fits a workspace quote converts exactly.
    if (!(points < static_cast<double>(std::numeric_limits<std::size_t>::max() / 1024)))
        throw Error("Helper workspace overflow");
    return static_cast<std::size_t>(points) + 1;
}

// One periodogram's live scratch: the complex aperture, its transform plan
// and scratch, and the returned frequency and power rows.
std::size_t periodogram_bytes(std::size_t samples) {
    return checked_add(checked_mul(samples, 2 * sizeof(Complex)), DftPlan::workspace_bytes(samples));
}
// One fit over a window of at most `bins` bins: the window's frequency copy,
// the kernel offsets, three kernel rows, and the profile grid with its scores
// and peaks. The fit pads its window by a full core on each side, so the
// offsets reserved for two cores stay within twice the window.
std::size_t fit_bytes(std::size_t bins) {
    return checked_mul(checked_add(checked_mul(bins, 6), 4 * maximum_profile_points), sizeof(double));
}
// A periodogram, then both pilots' fits beside its retained spectrum. The
// refinement window that can follow is smaller than either.
std::size_t spectral_bytes(std::size_t samples, std::size_t bins) {
    return std::max(periodogram_bytes(samples),
                    checked_add(checked_mul(samples, 2 * sizeof(double)), checked_mul(2, fit_bytes(bins))));
}

// Public acquisition geometry and the scratch it bounds. No extent depends
// on a received sample.
struct Shape {
    std::size_t half = 0, count = 0, decimation = 1, onset = 0;
    double onset_step = 0;
    // Branch filters that fit beneath the largest single step.
    std::size_t branch_lanes = 1;
    std::size_t bytes = 0;
};
Shape acquisition_shape(const ReceiverConfig& c, const ReceiverSlotPlan& s, std::size_t prefix,
                        const Support& support) {
    const double fs = c.sample_rate_hz, lead = static_cast<double>(s.window_capture_lead_seconds);
    Shape out;
    out.decimation = std::max<std::size_t>(1, static_cast<std::size_t>(fs / 100));
    out.half = static_cast<std::size_t>(std::ceil(filter_half_support * fs));
    out.count = std::min(prefix, static_cast<std::size_t>(std::max(0., std::ceil((support.end + lead + 2 * filter_half_support) * fs))));
    out.onset_step = std::max(1 / (fs / out.decimation), .02);
    out.onset = onset_points(onset_window(c, s), out.onset_step);
    const auto taps = checked_add(checked_mul(out.half, 2), 1);
    const auto total = checked_add(out.count, taps) - 1;
    const auto transform = next_power_two(total);
    const auto rows = (total - 1) / out.decimation + 1;
    const double aperture = std::ceil(frequency_seconds * fs);
    const auto frequency = aperture <= static_cast<double>(prefix)
        ? spectral_bytes(static_cast<std::size_t>(aperture), std::min(static_cast<std::size_t>(aperture), maximum_fit_bins)) : 0;
    const auto coarse = spectral_bytes(support.last - support.first, support.last - support.first);
    // The taps, the shared plan and response, and one branch in flight.
    const auto response = checked_add(checked_mul(taps, sizeof(double)),
                                      checked_mul(checked_mul(transform, 2) - 1, sizeof(Complex)));
    const auto branch = checked_mul(transform, sizeof(Complex));
    // Decimated branches with their energy and dates, two level index lists
    // that can hold twice their rows and one of them mid-growth, and the
    // score prefix. Then the onset grid's three rows and its region lists:
    // the regions, their padded copy and the merged result.
    const auto retained = checked_add(checked_mul(taps, sizeof(double)),
        checked_add(checked_mul(rows, maximum_targets * sizeof(Complex) + 8 * sizeof(double)),
                    checked_mul(out.onset, 3 * sizeof(double) + 3 * sizeof(Interval))));
    const auto filtering = checked_add(checked_add(response, branch), retained);
    const auto largest = std::max({coarse, filtering, checked_add(retained, frequency)});
    out.branch_lanes = 1 + std::min(maximum_targets - 1, (largest - filtering) / branch);
    out.bytes = checked_add(largest, record_bytes);
    return out;
}
// Acquisition returns or throws before any scratch unless these hold.
bool supported(const ReceiverConfig& c, const ReceiverSlotPlan& s, std::size_t prefix) {
    if (!c.helper_enabled || c.helper.pilot_dwell_gating || c.physical_rf_retuning) return false;
    if (!prefix || prefix > maximum_samples) return false;
    if (!bounded_uncertainty(c) || !finite_calendar(s)) return false;
    return std::max(c.clock_bound_ppm, c.endpoint_clock_bound_ppm) * 1e-6 < .1;
}
} // namespace

std::size_t helper_workspace(const ReceiverConfig& c, const ReceiverSlotPlan& s) {
    const auto prefix = prefix_samples(c, s);
    if (!supported(c, s, prefix)) return 0;
    const auto support = coarse_support(c, s, prefix);
    return support.enough ? acquisition_shape(c, s, prefix, support).bytes : 0;
}

HelperResult score_helper(const ReceiverConfig& c, const ReceiverSlotPlan& s,
                          std::span<const Complex> input, std::span<const std::uint8_t> validity,
                          std::size_t scratch_limit_bytes, std::stop_token stop, const LaneWave& wave) {
    HelperResult result;
    auto finish = [&](const char* reason) { result.reason = reason; return result; };
    check_stop(stop);
    if (!std::isfinite(c.frequency_bound_hz) || c.frequency_bound_hz < 0)
        throw Error("Helper frequency prior must be finite and nonnegative");
    if (!c.helper_enabled) return finish("helper_disabled");
    if (c.helper.pilot_dwell_gating || c.physical_rf_retuning) return finish("gated_or_retuned_pilots_unsupported");
    const auto prefix = prefix_samples(c, s);
    if (input.size() != s.stop_sample - s.first_sample || (!validity.empty() && validity.size() != input.size()))
        throw Error("Helper input does not match its public window");
    result.prefix_samples = prefix;
    if (prefix > maximum_samples) return finish("helper_work_limit");
    const auto raw = input.first(prefix);
    const auto valid = validity.empty() ? validity : validity.first(prefix);
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if ((i & 4095U) == 0) check_stop(stop);
        if (!std::isfinite(raw[i].real()) || !std::isfinite(raw[i].imag()) || (!valid.empty() && valid[i] > 1))
            throw Error("Helper input must be finite with a binary validity mask");
    }
    const double fs = c.sample_rate_hz, lead = static_cast<double>(s.window_capture_lead_seconds);
    const double audio = c.clock_bound_ppm * 1e-6, endpoint = c.endpoint_clock_bound_ppm * 1e-6;
    if (!bounded_uncertainty(c)) throw Error("invalid public Helper uncertainty");
    if (!finite_calendar(s)) throw Error("invalid public Helper calendar");
    if (std::max(audio, endpoint) >= .1) return finish("clock_envelope_unsupported");
    const auto support = coarse_support(c, s, raw.size());
    if (!support.enough) return finish("insufficient_guaranteed_coarse_support");
    const double settling = support.settling, end = support.end;
    const auto first = support.first, last = support.last;
    if (!valid.empty() && std::any_of(valid.begin(), valid.begin() + last, [](auto v) { return v != 1; }))
        return finish("known_erasure_in_helper_support");
    const auto shape = acquisition_shape(c, s, raw.size(), support);
    result.workspace_peak_bytes = shape.bytes;
    if (result.workspace_peak_bytes > scratch_limit_bytes) throw ReceiverWorkspaceError("Helper scratch exceeds its admitted memory lease");
    const double duration = (last - first) / fs;
    const double width = c.random_doppler_width_hz, separation = c.spread_doppler_hz, motion = c.delay_motion_doppler_bound_hz;
    const double sigma = std::hypot(width / 2, .75 / duration);
    std::vector<double> targets;
    {
        const auto spectrum = periodogram(raw.subspan(first, last - first), fs, stop);
        const std::array pilots{c.helper.low_frequency_hz, c.helper.frequency_hz};
        std::array<Outcome<Fit>, 2> fits;
        each(wave, pilots.size(), pilots.size(), stop, [&](std::size_t index, std::stop_token job_stop) {
            return fits[index].run([&] {
                return fit_pilot(spectrum, pilots[index] - c.data_if_hz, c.frequency_bound_hz, audio, separation,
                                 sigma, duration, fs, 20, job_stop);
            });
        });
        for (std::size_t index = 0; index < pilots.size(); ++index) {
            const double center = pilots[index] - c.data_if_hz;
            Fit fit;
            try { fit = fits[index].take(); }
            catch (const Error& error) {
                if (std::string(error.what()) == "Helper profile work limit") return finish("coarse_profile_work_limit");
                throw;
            }
            if (fit.outside) continue;
            const auto best = static_cast<std::size_t>(std::max_element(fit.profile.scores.begin(), fit.profile.scores.end()) - fit.profile.scores.begin());
            auto& diagnostic = result.coarse_pilots[index];
            diagnostic = {fit.profile.scores[best] >= 18 && fit.profile.peaks[best] >= 4,
                          fit.grid[best], fit.profile.scores[best], fit.profile.peaks[best]};
            if (diagnostic.usable) for (double offset : {0., separation}) {
                const double target = center + fit.grid[best] + offset;
                if (std::none_of(targets.begin(), targets.end(), [&](auto old) { return std::abs(target - old) < 1e-9; })) targets.push_back(target);
            }
        }
    }
    if (targets.empty()) return finish("no_coarse_pilot");
    if (width == 0 && motion == 0) {
        // Every target refines against the same immutable aperture and window.
        const auto window = refinement_window(last - first, stop);
        std::vector<Outcome<double>> refined(targets.size());
        each(wave, targets.size(), targets.size(), stop, [&](std::size_t index, std::stop_token job_stop) {
            return refined[index].run([&] {
                return refine_frequency(raw.subspan(first, last - first), window, fs, targets[index], job_stop);
            });
        });
        try {
            for (std::size_t index = 0; index < targets.size(); ++index) targets[index] = refined[index].take();
        } catch (const Error& error) {
            if (std::string(error.what()) == "Helper persistent frequency refinement failed")
                return finish("coarse_frequency_refinement_failed");
            throw;
        }
    }
    const double half_width = std::max(4., 3 * width + motion);
    const auto decimation = shape.decimation;
    const double output_fs = fs / decimation;
    if (half_width >= output_fs * .4) return finish("onset_filter_bandwidth_unsupported");
    const auto half = shape.half;
    const auto taps = lowpass(half, fs, half_width);
    const auto count = shape.count;
    std::vector<std::vector<Complex>> branches(targets.size());
    {
        // The shared response is released before the frequency apertures.
        const BranchFilter filter(count, taps, stop);
        std::vector<Outcome<std::vector<Complex>>> filtered(targets.size());
        each(wave, targets.size(), shape.branch_lanes, stop, [&](std::size_t index, std::stop_token job_stop) {
            return filtered[index].run([&] {
                return filtered_branch(raw.first(count), filter, decimation, fs, lead, targets[index], job_stop);
            });
        });
        for (std::size_t index = 0; index < targets.size(); ++index) branches[index] = filtered[index].take();
    }
    // Branch energy accumulates in target order.
    std::vector<double> energy(branches.front().size());
    for (const auto& branch : branches)
        for (std::size_t i = 0; i < energy.size(); ++i) energy[i] += std::norm(branch[i]);
    std::vector<double> dates(energy.size());
    for (std::size_t i = 0; i < dates.size(); ++i) dates[i] = (static_cast<double>(i * decimation) - half) / fs - lead;
    const auto window = onset_window(c, s);
    const double hstart = window.start, earliest = window.earliest, latest = window.latest;
    std::vector<std::size_t> noise_indices, signal_indices;
    double v0 = 0, v1 = 0;
    for (std::size_t i = 0; i < dates.size(); ++i) {
        if (dates[i] >= -lead + filter_half_support && dates[i] < earliest - filter_half_support - coarse_guard) { noise_indices.push_back(i); v0 += energy[i]; }
        if (dates[i] >= latest + settling + filter_half_support + coarse_guard && dates[i] < end - filter_half_support) { signal_indices.push_back(i); v1 += energy[i]; }
    }
    if (std::min(noise_indices.size(), signal_indices.size()) / output_fs < .25) return finish("insufficient_filtered_level_support");
    v0 /= noise_indices.size(); v1 /= signal_indices.size();
    if (std::min(v0, v1) <= 0) return finish("unusable_filtered_levels");
    result.onset_noise_level = v0; result.onset_helper_level = v1;
    const double ratio = v1 / v0;
    if (ratio < 1.25) return finish("weak_filtered_helper_onset");
    const double step = shape.onset_step;
    const auto grid = linspace(earliest, latest, shape.onset);
    std::vector<std::size_t> indices(grid.size());
    for (std::size_t i = 0; i < grid.size(); ++i) indices[i] = static_cast<std::size_t>(std::lower_bound(dates.begin(), dates.end(), grid[i]) - dates.begin());
    const auto a = indices.front(), b = indices.back();
    const double enbw = fs * std::inner_product(taps.begin(), taps.end(), taps.begin(), 0.);
    const double scale = std::min(1., enbw / output_fs);
    std::vector<double> prefix_score(b - a + 1), scores(grid.size());
    if (width == 0 && motion == 0) {
        std::vector<Complex> means;
        for (const auto& branch : branches) {
            Complex mean{}; for (auto index : signal_indices) mean += branch[index];
            means.push_back(mean / static_cast<double>(signal_indices.size()));
        }
        const double variance = v0 / branches.size();
        for (std::size_t i = a; i < b; ++i) {
            double evidence = 0;
            for (std::size_t j = 0; j < branches.size(); ++j) evidence += 2 * std::real(std::conj(means[j]) * branches[j][i]) - std::norm(means[j]);
            prefix_score[i - a + 1] = prefix_score[i - a] + evidence / variance;
        }
        for (std::size_t i = 0; i < grid.size(); ++i) scores[i] = -prefix_score[indices[i] - a] * scale;
    } else {
        for (std::size_t i = a; i < b; ++i) prefix_score[i - a + 1] = prefix_score[i - a] + energy[i];
        for (std::size_t i = 0; i < grid.size(); ++i) scores[i] = ((indices[i] - a) * std::log(ratio) + prefix_score[indices[i] - a] * (1 / v1 - 1 / v0)) * scale;
    }
    const auto best = static_cast<std::size_t>(std::max_element(scores.begin(), scores.end()) - scores.begin());
    result.onset_best_arrival_seconds = grid[best] - hstart;
    const double padding = onset_padding + step;
    std::vector<Interval> intervals;
    for (auto v : regions(grid, scores, 6)) {
        const auto arrival = arrival_bounds({v[0] - padding, v[1] + padding}, s.helper_begin_seconds,
                                            endpoint, global_capture_lead(c, s));
        intervals.push_back({std::max(s.arrival_lower_seconds, arrival[0]),
                             std::min(s.arrival_upper_seconds, arrival[1])});
    }
    result.onset_intervals = merge(std::move(intervals));
    if (result.onset_intervals.empty()) return finish("onset_region_empty");
    const Interval hull{result.onset_intervals.front()[0], result.onset_intervals.back()[1]};
    const auto cells = static_cast<std::size_t>(std::max(1., std::ceil((hull[1] - hull[0]) / 1.25)));
    if (cells > 8) return finish("onset_aperture_work_limit");
    result.onset_apertures = cells;
    const auto edges = linspace(hull[0], hull[1], cells + 1);
    std::vector<Interval> frequency_intervals;
    const double horizon = (s.stop_sample - s.first_sample) / fs;
    for (std::size_t i = 0; i < cells; ++i) {
        const auto frequency = final_frequency(c, s, raw, valid, {edges[i], edges[i + 1]}, horizon, stop, wave);
        if (!frequency.narrowed) return finish("onset_branch_frequency_fallback");
        frequency_intervals.insert(frequency_intervals.end(), frequency.intervals.begin(), frequency.intervals.end());
    }
    for (auto v : merge(std::move(frequency_intervals))) result.intervals.push_back({v[0], v[1]});
    result.frequency_observation_seconds = std::ceil(frequency_seconds * fs) / fs;
    result.narrowed = true;
    return finish("pilot_aware_onset_relative_frequency_proposal");
}

} // namespace datapump::triad::v3::receiver_detail
