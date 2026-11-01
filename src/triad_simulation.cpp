#include "datapump/triad_simulation.hpp"

#include "channel_perturbation.hpp"
#include "channel_spread.hpp"
#include "datapump/types.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
#include <string>

namespace datapump::triad {
namespace {
using Complex = std::complex<double>;
constexpr double tau = 2.0 * std::numbers::pi;

std::uint64_t mix64(std::uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

struct Random {
    std::uint64_t state;
    explicit Random(std::uint64_t seed) : state(seed) {}
    std::uint64_t next() { state = mix64(state); return state; }
    double uniform() {
        return (static_cast<double>(next() >> 11) + .5) / 9007199254740992.0;
    }
    double normal() {
        const auto u1 = std::max(uniform(), std::numeric_limits<double>::min());
        const auto u2 = uniform();
        return std::sqrt(-2.0 * std::log(u1)) * std::cos(tau * u2);
    }
};

bool finite_nonnegative(double x) { return std::isfinite(x) && x >= 0; }

void validate(const SimulationConfig& c) {
    const double values[] = {c.sample_rate, c.occupied_bandwidth_hz, c.rf_hz, c.nominal_conversion_if_hz,
        c.tx.rf_ppm, c.tx.rf_drift_ppm_per_second, c.tx.phase_noise_rad_per_sqrt_second, c.tx.audio_ppm,
        c.rx.rf_ppm, c.rx.rf_drift_ppm_per_second, c.rx.phase_noise_rad_per_sqrt_second, c.rx.audio_ppm,
        c.timing_offset_seconds, c.capture_lead_seconds, c.capture_trail_seconds, c.static_doppler_hz, c.spread_delay_ms, c.spread_doppler_hz,
        c.perturbation_slow_depth, c.perturbation_fast_depth, c.snr_db,
        c.dropout_ms, c.dropout_start_ms, c.dwell_seconds, c.retune_offset_seconds};
    for (const auto value : values)
        if (!std::isfinite(value)) throw Error("triad simulation configuration contains a non-finite value");
    if (!(c.sample_rate > 0) || !(c.occupied_bandwidth_hz > 0) || !(c.rf_hz > 0) ||
        c.nominal_conversion_if_hz < 0 ||
        c.occupied_bandwidth_hz >= c.sample_rate || c.dwell_seconds <= 0)
        throw Error("triad simulation sample clock, bandwidth, RF carrier or dwell is invalid");
    if (!finite_nonnegative(c.spread_delay_ms) || !finite_nonnegative(c.spread_doppler_hz) ||
        !finite_nonnegative(c.perturbation_slow_depth) || !finite_nonnegative(c.perturbation_fast_depth) ||
        !finite_nonnegative(c.dropout_ms) || !finite_nonnegative(c.dropout_start_ms) ||
        !finite_nonnegative(c.tx.phase_noise_rad_per_sqrt_second) ||
        !finite_nonnegative(c.rx.phase_noise_rad_per_sqrt_second))
        throw Error("triad simulation impairment magnitude must be nonnegative");
    if (c.dropout_every_n_dwells == 0)
        throw Error("triad simulation dropout cadence must be positive");
    if (c.fhss && c.hop_offsets_hz.empty())
        throw Error("triad simulation FHSS requires an explicit hop-offset calendar");
    if (c.hop_offsets_hz.size() > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max() / 2))
        throw Error("triad FHSS calendar exceeds supported dwell coordinates");
    for (const auto hop : c.hop_offsets_hz)
        if (!std::isfinite(hop)) throw Error("triad simulation hop offset is non-finite");
    if (c.dropout_start_ms + c.dropout_ms > c.dwell_seconds * 1000.0)
        throw Error("triad simulation dropout window extends beyond its dwell");
    const double tx_scale = 1.0 + c.tx.audio_ppm * 1e-6;
    const double rx_scale = 1.0 + c.rx.audio_ppm * 1e-6;
    if (tx_scale <= 0 || rx_scale <= 0)
        throw Error("triad simulation audio clock rate must remain positive");
    if (!finite_nonnegative(c.capture_lead_seconds) || !finite_nonnegative(c.capture_trail_seconds) ||
        c.timing_offset_seconds < -c.capture_lead_seconds)
        throw Error("triad capture lead does not cover the configured timing hypothesis");
    const auto dwell_limit = static_cast<double>(std::numeric_limits<std::int64_t>::max() / 4);
    if (std::abs(c.timing_offset_seconds / c.dwell_seconds) >= dwell_limit ||
        c.capture_lead_seconds / c.dwell_seconds >= dwell_limit ||
        std::abs(c.retune_offset_seconds / c.dwell_seconds) >= dwell_limit)
        throw Error("triad timing exceeds supported dwell coordinates");
}

// Sixteen-tap Blackman-windowed sinc interpolation, normalized at each
// fractional coordinate. This is used for independent audio clocks and the
// delayed ray; it neither clamps the source index nor wraps out-of-range data.
Complex interpolate(std::span<const Complex> x, long double position) {
    if (position >= 0 && position < static_cast<long double>(x.size()) &&
        position == std::floor(position))
        return x[static_cast<std::size_t>(position)];
    if (position < -8.0L || position >= static_cast<long double>(x.size()) + 8.0L) return {};
    const auto center = static_cast<std::int64_t>(std::floor(position));
    Complex value{};
    double norm = 0;
    for (std::int64_t tap = center - 7; tap <= center + 8; ++tap) {
        const double d = static_cast<double>(position - static_cast<long double>(tap));
        if (std::abs(d) >= 8.0) continue;
        const double z = (d + 8.0) / 16.0;
        const double window = .42 - .5 * std::cos(tau * z) + .08 * std::cos(2.0 * tau * z);
        const double coefficient = (std::abs(d) < 1e-12 ? 1.0 :
            std::sin(std::numbers::pi * d) / (std::numbers::pi * d)) * window;
        if (tap >= 0 && static_cast<std::uint64_t>(tap) < x.size())
            value += coefficient * x[static_cast<std::size_t>(tap)];
        norm += coefficient;
    }
    return std::abs(norm) > 1e-12 ? value / norm : Complex{};
}

std::int64_t dwell_index(double t, double dwell) {
    return static_cast<std::int64_t>(std::floor(t / dwell));
}

std::size_t calendar_index(std::int64_t dwell, std::size_t count) {
    const auto n = static_cast<std::int64_t>(count);
    auto result = dwell % n;
    if (result < 0) result += n;
    return static_cast<std::size_t>(result);
}

double hop_at(const SimulationConfig& c, double t) {
    if (!c.fhss) return 0;
    return c.hop_offsets_hz[calendar_index(dwell_index(t, c.dwell_seconds), c.hop_offsets_hz.size())];
}

long double integrated_hop_cycles(const SimulationConfig& c, double t,
                                  std::span<const long double> prefix) {
    if (!c.fhss || c.hop_offsets_hz.empty() || t == 0) return 0;
    const auto dwells = c.hop_offsets_hz.size();
    const long double dwell = c.dwell_seconds;
    const auto whole = dwell_index(t, c.dwell_seconds);
    const long double within = static_cast<long double>(t) - static_cast<long double>(whole) * dwell;
    const auto count = static_cast<std::int64_t>(dwells);
    auto cycles = whole / count;
    auto remainder = whole % count;
    if (remainder < 0) { --cycles; remainder += count; }
    long double integral = static_cast<long double>(cycles) * dwell * prefix.back() +
        dwell * prefix[static_cast<std::size_t>(remainder)];
    integral += within * c.hop_offsets_hz[static_cast<std::size_t>(remainder)];
    return integral;
}

double keyed_phase(std::uint64_t seed, std::int64_t dwell, std::uint64_t domain) {
    const auto bits = mix64(seed ^ mix64(static_cast<std::uint64_t>(dwell)) ^ domain);
    return tau * (static_cast<double>(bits >> 11) + .5) / 9007199254740992.0;
}

bool dropout_at(const SimulationConfig& c, double t) {
    if (c.dropout_ms <= 0) return false;
    const auto d = dwell_index(t, c.dwell_seconds);
    if (d < 0 || static_cast<std::uint64_t>(d) % c.dropout_every_n_dwells != 0) return false;
    const double within = t - static_cast<double>(d) * c.dwell_seconds;
    const double start = c.dropout_start_ms * .001;
    const double end = start + c.dropout_ms * .001;
    return within >= start && within < end;
}

} // namespace

double oscillator_bound_hz(double rf_hz, double per_end_ppm_bound,
                           double duration_seconds,
                           double per_end_drift_ppm_per_second,
                           double nominal_conversion_if_hz,
                           double per_end_audio_ppm_bound,
                           double per_end_audio_drift_ppm_per_second) {
    if (!finite_nonnegative(rf_hz) || !finite_nonnegative(per_end_ppm_bound) ||
        !finite_nonnegative(duration_seconds) || !finite_nonnegative(per_end_drift_ppm_per_second) ||
        !finite_nonnegative(nominal_conversion_if_hz) || !finite_nonnegative(per_end_audio_ppm_bound) ||
        !finite_nonnegative(per_end_audio_drift_ppm_per_second))
        throw Error("triad oscillator bound arguments must be finite and nonnegative");
    const double rf = rf_hz * 2.0 * (per_end_ppm_bound +
        per_end_drift_ppm_per_second * duration_seconds) * 1e-6;
    const double audio = nominal_conversion_if_hz * 2.0 * (per_end_audio_ppm_bound +
        per_end_audio_drift_ppm_per_second * duration_seconds) * 1e-6;
    return rf + audio;
}

Capture simulate(std::span<const Complex> source, const SimulationConfig& c) {
    validate(c);
    Capture result;
    result.source_samples = source.size();
    result.tx_realization = c.tx;
    result.rx_realization = c.rx;
    if (source.empty()) return result;

    long double source_energy = 0;
    for (const auto sample : source) {
        if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag()))
            throw Error("triad simulation source contains a non-finite sample");
        source_energy += std::norm(sample);
    }
    result.input_power = static_cast<double>(source_energy / static_cast<long double>(source.size()));
    if (!std::isfinite(result.input_power))
        throw Error("triad source mean power is outside the representable range");
    result.noise_reference_bandwidth_hz = c.occupied_bandwidth_hz;
    const double noise_reference = result.input_power > 0 ? result.input_power : 1.0;
    const double delay_seconds = c.spread_delay_ms * .001;
    const double delay_samples_real = delay_seconds * c.sample_rate;
    if (delay_samples_real > static_cast<double>(std::numeric_limits<std::size_t>::max() / 4))
        throw Error("triad simulation spread delay exceeds sample coordinates");
    const bool spread = c.spread_delay_ms > 0 || c.spread_doppler_hz > 0;
    const bool perturb = c.spread_doppler_hz > 0 &&
        (c.perturbation_slow_depth > 0 || c.perturbation_fast_depth > 0);
    const auto txscale = 1.0 + c.tx.audio_ppm * 1e-6;
    const auto rxscale = 1.0 + c.rx.audio_ppm * 1e-6;
    const auto perturb_tail = perturb ? modem::detail::FrequencySelectivePerturbation::decay_samples(
        c.sample_rate * rxscale, c.occupied_bandwidth_hz) : 0;
    const auto integer_coordinate = [](double value) {
        return std::abs(value - std::round(value)) < 1e-9;
    };
    const bool resampled = txscale != rxscale ||
        !integer_coordinate((c.capture_lead_seconds + c.timing_offset_seconds) * c.sample_rate * txscale) ||
        !integer_coordinate(delay_seconds * c.sample_rate * txscale);
    const auto interpolation_tail = resampled ? std::ceil(8.L * rxscale / txscale) : 0.L;
    const long double delay_tail_real = static_cast<long double>(delay_seconds) *
        c.sample_rate * rxscale;
    if (!std::isfinite(delay_tail_real) || delay_tail_real >
        static_cast<long double>(std::numeric_limits<std::size_t>::max() / 2))
        throw Error("triad simulation channel tail exceeds sample coordinates");
    const auto delay_tail = static_cast<std::size_t>(std::ceil(delay_tail_real));
    if (perturb_tail > std::numeric_limits<std::size_t>::max() - delay_tail)
        throw Error("triad simulation channel tail exceeds sample coordinates");
    if (!std::isfinite(interpolation_tail) || interpolation_tail >
        static_cast<long double>(std::numeric_limits<std::size_t>::max() - delay_tail - perturb_tail))
        throw Error("triad interpolation tail exceeds sample coordinates");
    result.tail_samples = delay_tail + static_cast<std::size_t>(perturb_tail) +
        static_cast<std::size_t>(interpolation_tail);
    const long double timing_pad_real = static_cast<long double>(c.capture_lead_seconds) *
        c.sample_rate * rxscale;
    if (!std::isfinite(timing_pad_real) || timing_pad_real >
        static_cast<long double>(std::numeric_limits<std::size_t>::max()))
        throw Error("triad capture lead exceeds sample coordinates");
    const auto timing_pad = static_cast<std::size_t>(std::ceil(timing_pad_real));
    result.timing_pad_samples = timing_pad;
    const double source_duration = static_cast<double>(source.size()) / c.sample_rate / txscale;
    if (!std::isfinite(source_duration) || source_duration / c.dwell_seconds >=
        static_cast<double>(std::numeric_limits<std::int64_t>::max() / 2))
        throw Error("triad source duration exceeds bounded dwell coordinates");
    const double output_end_time = std::max(0.0, source_duration + c.timing_offset_seconds) + c.capture_trail_seconds +
        static_cast<double>(result.tail_samples) / (c.sample_rate * rxscale);
    const long double output_real = std::ceil((static_cast<long double>(c.capture_lead_seconds) +
        output_end_time) * c.sample_rate * rxscale);
    if (!std::isfinite(output_real) || output_real > static_cast<long double>(std::numeric_limits<std::size_t>::max()))
        throw Error("triad simulation output extent exceeds sample coordinates");
    const auto output_count = static_cast<std::size_t>(output_real);
    if (output_count > std::vector<Complex>().max_size() ||
        output_count > default_memory_limit / sizeof(Complex) ||
        source.size() > default_memory_limit / sizeof(Complex) - output_count)
        throw Error("triad simulation output exceeds its bounded 256 MiB workspace");

    const auto no_oscillator_effects = c.tx.rf_ppm == 0 && c.tx.rf_drift_ppm_per_second == 0 &&
        c.tx.phase_noise_rad_per_sqrt_second == 0 && c.tx.audio_ppm == 0 &&
        c.rx.rf_ppm == 0 && c.rx.rf_drift_ppm_per_second == 0 &&
        c.rx.phase_noise_rad_per_sqrt_second == 0 && c.rx.audio_ppm == 0;
    const bool exact_bypass = c.signal_enabled && !c.noise_enabled && !c.fhss &&
        no_oscillator_effects && c.timing_offset_seconds == 0 && c.capture_lead_seconds == 0 && c.capture_trail_seconds == 0 &&
        c.static_doppler_hz == 0 && !spread && !perturb &&
        c.dropout_ms == 0 && !c.slow_agc;
    if (exact_bypass) {
        result.iq.assign(source.begin(), source.end());
        result.capture_signal_power = result.input_power;
        result.observed_power = result.input_power;
        return result;
    }

    const double maximum_hop = c.fhss && !c.hop_offsets_hz.empty() ? std::max(
        std::abs(*std::min_element(c.hop_offsets_hz.begin(), c.hop_offsets_hz.end())),
        std::abs(*std::max_element(c.hop_offsets_hz.begin(), c.hop_offsets_hz.end()))) : 0.0;
    const double rf_max_cfo = (c.rf_hz + maximum_hop) * (
        std::abs(c.tx.rf_ppm) + std::abs(c.rx.rf_ppm) +
        (std::abs(c.tx.rf_drift_ppm_per_second) + std::abs(c.rx.rf_drift_ppm_per_second)) *
            (source_duration + c.capture_lead_seconds + std::abs(c.timing_offset_seconds))
    ) * 1e-6 + std::abs(c.static_doppler_hz);
    const double audio_if_max_cfo = c.nominal_conversion_if_hz *
        (std::abs(txscale - 1.0) + std::abs(rxscale - 1.0));
    const double max_cfo = rf_max_cfo + audio_if_max_cfo;
    if (!std::isfinite(max_cfo)) throw Error("triad duration-bounded CFO is not representable");
    if (c.sample_rate <= 2.0 * (max_cfo + c.occupied_bandwidth_hz / 2.0))
        throw Error("triad simulation sample rate aliases the duration-bounded CFO and occupied band");
    result.expected_max_cfo_hz = max_cfo;
    const double noise_variance = noise_reference * (c.sample_rate / c.occupied_bandwidth_hz) *
        std::pow(10.0, -c.snr_db / 10.0);
    if (!std::isfinite(noise_variance))
        throw Error("triad noise power is outside the representable range");

    // Apply independent TX oscillator error before the path. Phase diffusion
    // and retune phase are seeded independently from AWGN and the channel.
    std::vector<Complex> transmitted(source.size());
    Random tx_noise(c.seed ^ 0x3254494154545801ULL);
    const double dt = 1.0 / (c.sample_rate * txscale);
    double tx_phase = 0;
    auto previous_tx_dwell = std::numeric_limits<std::int64_t>::min();
    for (std::size_t i = 0; i < source.size(); ++i) {
        const double t = static_cast<double>(i) * dt;
        const auto di = dwell_index(t, c.dwell_seconds);
        if (c.fhss && di != previous_tx_dwell) {
            tx_phase += keyed_phase(c.seed, di, 0x5458484f50504841ULL);
            previous_tx_dwell = di;
        }
        const double hop = hop_at(c, t);
        const double rf_error = (c.rf_hz + hop) *
            (c.tx.rf_ppm + c.tx.rf_drift_ppm_per_second * t) * 1e-6;
        const double audio_if_error = c.nominal_conversion_if_hz * (txscale - 1.0);
        tx_phase += tau * (rf_error + audio_if_error) * dt +
            c.tx.phase_noise_rad_per_sqrt_second * std::sqrt(dt) * tx_noise.normal();
        transmitted[i] = c.signal_enabled ? source[i] * std::polar(1.0, tx_phase) : Complex{};
    }

    std::unique_ptr<modem::detail::SpreadRay> ray;
    if (spread) ray = std::make_unique<modem::detail::SpreadRay>(c.sample_rate * rxscale,
        c.spread_doppler_hz, c.seed ^ 0x325449415443484eULL);
    std::unique_ptr<modem::detail::FrequencySelectivePerturbation> selective;
    if (perturb) selective = std::make_unique<modem::detail::FrequencySelectivePerturbation>(
        c.sample_rate * rxscale, 0.0, c.occupied_bandwidth_hz, c.spread_doppler_hz,
        c.perturbation_slow_depth, c.perturbation_fast_depth, c.seed ^ 0x3254494154504552ULL);

    result.iq.assign(output_count, {});
    Random rx_noise(c.seed ^ 0x3254494154525802ULL);
    Random thermal(c.seed ^ 0x3254494154415747ULL);
    result.noise_power = c.noise_enabled ? noise_variance : 0;
    double rx_phase = 0;
    auto previous_rx_dwell = std::numeric_limits<std::int64_t>::min();
    double agc_power = noise_reference;
    const double agc_alpha = std::exp(-1.0 / (c.sample_rate * rxscale * .75));
    long double captured_energy = 0;
    long double signal_energy = 0;
    std::vector<long double> hop_prefix(c.hop_offsets_hz.size() + 1, 0.0L);
    for (std::size_t i = 0; i < c.hop_offsets_hz.size(); ++i)
        hop_prefix[i + 1] = hop_prefix[i] + c.hop_offsets_hz[i];
    for (std::size_t n = 0; n < output_count; ++n) {
        const double t = static_cast<double>(n) / (c.sample_rate * rxscale) - c.capture_lead_seconds;
        // Positive clock ppm speeds up source time independently at each end.
        const double tx_emission_time = t - c.timing_offset_seconds;
        const double tx_clock_time = tx_emission_time * txscale;
        const long double direct_pos = static_cast<long double>(tx_clock_time * c.sample_rate);
        const long double delayed_pos = static_cast<long double>(
            (tx_clock_time - delay_seconds * txscale) * c.sample_rate);
        Complex direct = interpolate(transmitted, direct_pos);
        Complex delayed = interpolate(transmitted, delayed_pos);

        const double rx_time = t + c.retune_offset_seconds;
        const double rx_hop = hop_at(c, rx_time);
        const double direct_hop = hop_at(c, tx_emission_time);
        const double delayed_emission_time = tx_emission_time - delay_seconds;
        const double delayed_hop = hop_at(c, delayed_emission_time);
        const double selectivity_limit = c.occupied_bandwidth_hz * .5;
        bool out_of_band = false;
        if (std::abs(direct_hop - rx_hop) > selectivity_limit) {
            direct = {};
            out_of_band = true;
        }
        if (std::abs(delayed_hop - rx_hop) > selectivity_limit) {
            delayed = {};
            out_of_band = true;
        }
        if (out_of_band) ++result.out_of_passband_samples;

        const auto direct_hop_phase = tau * std::remainder(
            integrated_hop_cycles(c, tx_emission_time, hop_prefix) -
                integrated_hop_cycles(c, rx_time, hop_prefix), 1.0);
        const auto delayed_hop_phase = tau * std::remainder(
            integrated_hop_cycles(c, delayed_emission_time, hop_prefix) -
                integrated_hop_cycles(c, rx_time, hop_prefix), 1.0);
        direct *= std::polar(1.0, static_cast<double>(direct_hop_phase));
        delayed *= std::polar(1.0, static_cast<double>(delayed_hop_phase));

        // A physical delay contributes RF carrier phase as well as delayed
        // envelope samples. This keeps channel phase frequency-selective.
        delayed *= std::polar(1.0, -tau * std::remainder(c.rf_hz * delay_seconds, 1.0));
        Complex propagated = spread ? ray->process(direct, delayed) : direct;
        if (selective) propagated = selective->process(propagated);

        const auto rx_di = dwell_index(rx_time, c.dwell_seconds);
        if (c.fhss && rx_di != previous_rx_dwell) {
            rx_phase += keyed_phase(c.seed ^ 0x5258484f50504841ULL, rx_di, 0x5258504841534531ULL);
            previous_rx_dwell = rx_di;
        }
        const double rx_true_time = t;
        // Positive static Doppler is a received-signal frequency rise, so it
        // appears with the opposite sign in the receiver's local oscillator.
        const double rx_rf_error = (c.rf_hz + rx_hop) *
            (c.rx.rf_ppm + c.rx.rf_drift_ppm_per_second * rx_true_time) * 1e-6 - c.static_doppler_hz;
        const double rx_audio_if_error = c.nominal_conversion_if_hz * (rxscale - 1.0);
        const double rx_dt = 1.0 / (c.sample_rate * rxscale);
        rx_phase += tau * (rx_rf_error + rx_audio_if_error) * rx_dt +
            c.rx.phase_noise_rad_per_sqrt_second * std::sqrt(rx_dt) * rx_noise.normal();
        propagated *= std::polar(1.0, -rx_phase);
        signal_energy += std::norm(propagated);

        if (c.noise_enabled) {
            const double sigma = std::sqrt(noise_variance * .5);
            propagated += Complex{sigma * thermal.normal(), sigma * thermal.normal()};
        }
        if (c.slow_agc) {
            agc_power = agc_alpha * agc_power + (1.0 - agc_alpha) * std::norm(propagated);
            const double gain = std::clamp(1.0 / std::sqrt(std::max(agc_power, 1e-12)), .02, 50.0);
            propagated *= gain;
        }
        if (dropout_at(c, t)) {
            propagated = {};
            ++result.erased_samples;
        }
        result.iq[n] = propagated;
        captured_energy += std::norm(propagated);
    }
    result.capture_signal_power = result.iq.empty() ? 0.0 :
        static_cast<double>(signal_energy / static_cast<long double>(result.iq.size()));
    result.observed_power = result.iq.empty() ? 0.0 :
        static_cast<double>(captured_energy / static_cast<long double>(result.iq.size()));
    return result;
}

} // namespace datapump::triad
