#include "datapump/triad_modem_v3.hpp"

#include "channel_perturbation.hpp"
#include "channel_spread.hpp"
#include "triad_coordinate.hpp"
#include "triad_gaussian_fading.hpp"
#include "triad_gaussian_stream.hpp"
#include "triad_random_process.hpp"
#include "datapump/types.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <memory>
#include <numbers>
#include <random>
#include <utility>
#include <vector>

namespace datapump::triad::v3 {
namespace {
using detail::floor_coordinate;
using detail::GaussianStream;
using detail::signed_index;
using detail::unsigned_index;
using modem::detail::DispersiveSelectivePerturbation;
using modem::detail::SpreadRay;
using Complex = std::complex<double>;
constexpr double tau = 2 * std::numbers::pi;
constexpr std::size_t kHistoryLimitBytes = 64ULL * 1024 * 1024;
// Source readers of a channel with lanes. Each holds a copy of the source's
// caches; a block's source range rarely repays more.
constexpr std::size_t kSourceReaders = 8;

void require(bool condition, const char* message) {
    if (!condition) throw Error(message);
}

std::uint64_t mix64(std::uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

double keyed_phase(std::uint64_t seed, std::int64_t dwell, std::uint64_t domain) {
    const auto bits = mix64(seed ^ mix64(static_cast<std::uint64_t>(dwell)) ^ domain);
    return tau * (static_cast<double>(bits >> 11) + .5) / 9007199254740992.0;
}

struct Rng {
    std::mt19937_64 engine;
    std::normal_distribution<double> normal{0, 1};
    explicit Rng(std::uint64_t seed) : engine(seed) {}
    double gaussian() { return normal(engine); }
    double uniform() { return (static_cast<double>(engine() >> 11) + .5) / 9007199254740992.0; }
};

bool finite_nonnegative(double x) { return std::isfinite(x) && x >= 0; }

std::uint64_t root_seed(const std::optional<std::uint64_t>& selected, std::uint64_t legacy) {
    return selected.value_or(legacy);
}

void validate_converter(const ConverterConfig& c) {
    require(c.nominal_bits == 0 || (c.nominal_bits >= 2 && c.nominal_bits <= 24),
            "converter nominal bit depth must be 0 or 2..24");
    require(std::isfinite(c.full_scale) && c.full_scale > 0 &&
            finite_nonnegative(c.gain) && finite_nonnegative(c.dither_rms_lsb) &&
            finite_nonnegative(c.effective_bits), "invalid converter range, gain or noise");
    require(c.effective_bits == 0 || (c.nominal_bits && c.effective_bits >= 1 &&
            c.effective_bits <= c.nominal_bits), "effective bits must not exceed nominal converter bits");
    require(c.nominal_bits || (c.effective_bits == 0 && c.dither_rms_lsb == 0),
            "converter dither or effective resolution needs enabled quantization");
}

void validate_frontend(const SampledFrontendConfig& f, double sample_rate) {
    validate_converter(f.dac); validate_converter(f.adc);
    require(finite_nonnegative(f.tone_amplitude) && std::isfinite(f.tone_frequency_hz) &&
            std::abs(f.tone_frequency_hz) < sample_rate*.5 && f.tone_every_n_dwells > 0 &&
            finite_nonnegative(f.impulse_amplitude) && finite_nonnegative(f.impulse_rate_hz) &&
            finite_nonnegative(f.lowpass_hz) && f.lowpass_hz < sample_rate*.5 &&
            std::isfinite(f.gain_step_seconds) && (f.gain_step_seconds == -1 || f.gain_step_seconds >= 0) &&
            finite_nonnegative(f.gain_step_factor) && finite_nonnegative(f.overload_recovery_seconds),
            "invalid sampled frontend configuration");
    require(!f.overload_recovery_seconds || f.adc.nominal_bits,
            "overload recovery needs an enabled ADC range");
}

void validate_path_evolution(const PathEvolutionConfig& p) {
    require(finite_nonnegative(p.common_doppler_wander_hz) &&
            std::isfinite(p.common_doppler_knot_seconds) && p.common_doppler_knot_seconds > 0 &&
            finite_nonnegative(p.ray_coherence_seconds) &&
            finite_nonnegative(p.gaussian_doppler_width_hz) &&
            finite_nonnegative(p.random_doppler_width_hz) &&
            finite_nonnegative(p.bounded_spread_knot_seconds) &&
            finite_nonnegative(p.delay_excursion_seconds) &&
            std::isfinite(p.delay_knot_seconds) && p.delay_knot_seconds > 0 &&
            detail::SmoothRandomDelay::maximum_slope(p.delay_excursion_seconds,p.delay_knot_seconds) < 1 &&
            finite_nonnegative(p.diurnal_phase_amplitude_radians) &&
            std::isfinite(p.diurnal_period_seconds) && p.diurnal_period_seconds > 0 &&
            std::isfinite(p.outage_start_seconds) && std::isfinite(p.outage_end_seconds) &&
            std::isfinite(p.outage_gain) && p.outage_gain >= 0 && p.outage_gain <= 1 &&
            finite_nonnegative(p.outage_ramp_seconds), "invalid physical-path evolution");
    require(!p.bounded_spread_direct_power_fraction ||
            (p.bounded_spread_knot_seconds > 0 &&
             std::isfinite(*p.bounded_spread_direct_power_fraction) &&
             *p.bounded_spread_direct_power_fraction >= 0 &&
             *p.bounded_spread_direct_power_fraction <= 1),
            "fixed direct power needs the bounded ray law and a fraction in [0,1]");
    require(static_cast<int>(p.ray_coherence_seconds > 0) +
            static_cast<int>(p.gaussian_doppler_width_hz > 0) +
            static_cast<int>(p.random_doppler_width_hz > 0) <= 1,
            "OU, sampled Gaussian and fixed-time Gaussian ray evolution are mutually exclusive");
    require(static_cast<int>(p.bounded_spread_knot_seconds > 0) +
            static_cast<int>(p.ray_coherence_seconds > 0) +
            static_cast<int>(p.gaussian_doppler_width_hz > 0) +
            static_cast<int>(p.random_doppler_width_hz > 0) <= 1,
            "bounded spread cannot be combined with another stochastic ray-gain law");
    require(std::isfinite(6*(p.gaussian_doppler_width_hz+p.random_doppler_width_hz)),
            "Gaussian path capture allowance overflow");
    const bool outage_off = p.outage_start_seconds == -1 && p.outage_end_seconds == -1;
    require(outage_off || (p.outage_start_seconds >= 0 && p.outage_end_seconds > p.outage_start_seconds &&
            p.outage_ramp_seconds <= (p.outage_end_seconds-p.outage_start_seconds)*.5),
            "physical-path outage needs ordered endpoints and ramps within its interval");
}

double outage_gain(const PathEvolutionConfig& p, long double seconds) {
    if (p.outage_start_seconds < 0 || seconds < p.outage_start_seconds || seconds >= p.outage_end_seconds)
        return 1;
    if (p.outage_ramp_seconds == 0) return p.outage_gain;
    const auto edge = std::min(seconds-p.outage_start_seconds, p.outage_end_seconds-seconds);
    const double fraction = std::min(1., static_cast<double>(edge/p.outage_ramp_seconds));
    return 1-(1-p.outage_gain)*.5*(1-std::cos(std::numbers::pi*fraction));
}

Complex convert(Complex value, const ConverterConfig& c, Rng& rng,
                std::uint64_t& clipped) {
    value *= c.gain;
    require(std::isfinite(value.real()) && std::isfinite(value.imag()),
            "converter input overflow");
    if (!c.nominal_bits) return value;
    const double step = std::ldexp(c.full_scale, 1-static_cast<int>(c.nominal_bits));
    const double effective = c.effective_bits > 0 ? c.effective_bits : c.nominal_bits;
    const double noise = step*std::sqrt(std::expm1(2*std::log(2.)*(c.nominal_bits-effective))/12);
    require(std::isfinite(step) && step > 0 && std::isfinite(noise),
            "converter resolution outside numeric range");
    auto component = [&](double x) {
        if (noise > 0) x += noise*rng.gaussian();
        if (c.dither_rms_lsb > 0)
            x += (rng.uniform()-rng.uniform())*std::sqrt(6.)*c.dither_rms_lsb*step;
        if (x < -c.full_scale || x > c.full_scale-step) ++clipped;
        x = std::clamp(x, -c.full_scale, c.full_scale-step);
        return std::clamp(std::round(x/step)*step, -c.full_scale, c.full_scale-step);
    };
    // Explicit order avoids evaluation-order-dependent seeded realizations.
    const double real = component(value.real());
    const double imag = component(value.imag());
    return {real, imag};
}

std::int64_t dwell_index(double t, double dwell) {
    const long double q = floor_coordinate(static_cast<long double>(t) / dwell);
    require(std::isfinite(q) && q > static_cast<long double>(INT64_MIN) &&
            q < static_cast<long double>(INT64_MAX), "stream channel dwell coordinate overflow");
    return signed_index(q);
}

std::size_t calendar_index(std::int64_t dwell, std::size_t count) {
    const auto n = static_cast<std::int64_t>(count);
    auto result = dwell % n;
    if (result < 0) result += n;
    return static_cast<std::size_t>(result);
}

double hop_at(const SimulationConfig& c, double t) {
    if (!c.fhss) return 0;
    return c.hop_offsets_hz[calendar_index(dwell_index(t, c.dwell_seconds),
                                           c.hop_offsets_hz.size())];
}

long double hop_cycles(const SimulationConfig& c, double t,
                       const std::vector<long double>& prefix) {
    if (!c.fhss || t == 0) return 0;
    const auto whole = dwell_index(t, c.dwell_seconds);
    const long double within = static_cast<long double>(t) -
        static_cast<long double>(whole) * c.dwell_seconds;
    const auto n = static_cast<std::int64_t>(c.hop_offsets_hz.size());
    auto cycles = whole / n;
    auto remainder = whole % n;
    if (remainder < 0) { --cycles; remainder += n; }
    return static_cast<long double>(cycles) * c.dwell_seconds * prefix.back() +
        c.dwell_seconds * prefix[static_cast<std::size_t>(remainder)] +
        within * c.hop_offsets_hz[static_cast<std::size_t>(remainder)];
}

bool dropout_at(const SimulationConfig& c, double t) {
    if (!(c.dropout_ms > 0)) return false;
    const auto d = dwell_index(t, c.dwell_seconds);
    if (d < 0 || static_cast<std::uint64_t>(d) % c.dropout_every_n_dwells != 0) return false;
    const auto within = t - static_cast<double>(d) * c.dwell_seconds;
    return within >= c.dropout_start_ms * .001 &&
        within < (c.dropout_start_ms + c.dropout_ms) * .001;
}

// One source coordinate through sixteen Blackman-windowed sinc taps. history
// is a ring of generated samples, of which this coordinate's reader retains
// [first, end). It reads the ring only, so rows can be evaluated in any order.
Complex interpolate(const std::vector<Complex>& history, std::uint64_t first,
                    std::uint64_t end,
                    long double position) {
    if (!std::isfinite(position) || position < -8 || position >=
        static_cast<long double>(end) + 8) return {};
    const auto size = history.size();
    const auto center_ld = floor_coordinate(position);
    if (position == center_ld && position >= 0) {
        if (position >= static_cast<long double>(UINT64_MAX)) return {};
        const auto index = unsigned_index(position);
        return index >= first && index < end ? history[static_cast<std::size_t>(index % size)] : Complex{};
    }
    // The retained sample under each of the taps center-7 ... center+8.
    std::array<const Complex*, 16> retained{};
    if (center_ld >= 7 && center_ld < 0x1p62L) {
        const auto lowest = unsigned_index(center_ld) - 7;
        const auto from = std::max(lowest, first), to = std::min(lowest + 16, end);
        if (from < to) {
            auto slot = static_cast<std::size_t>(from % size);
            for (auto index = from; index < to; ++index) {
                retained[static_cast<std::size_t>(index - lowest)] = &history[slot];
                if (++slot == size) slot = 0;
            }
        }
    } else {
        for (int offset = -7; offset <= 8; ++offset) {
            const auto tap = center_ld + offset;
            if (!(tap >= 0 && tap < static_cast<long double>(UINT64_MAX))) continue;
            const auto index = unsigned_index(tap);
            if (index >= first && index < end)
                retained[static_cast<std::size_t>(offset + 7)] = &history[static_cast<std::size_t>(index % size)];
        }
    }
    // Silence under every tap sums to positive zero whatever the coefficients
    // are, and their sum is within 1e-3 of one, so the quotient is that zero.
    bool silent = true;
    for (const auto* sample : retained)
        if (sample && (sample->real() != 0 || sample->imag() != 0)) { silent = false; break; }
    if (silent) return {};
    Complex value{};
    double norm = 0;
    for (int offset = -7; offset <= 8; ++offset) {
        const auto tap = center_ld + offset;
        const double d = static_cast<double>(position - tap);
        if (std::abs(d) >= 8) continue;
        const double z = (d + 8) / 16;
        const double window = .42 - .5 * std::cos(tau * z) + .08 * std::cos(2 * tau * z);
        const double coefficient = (std::abs(d) < 1e-12 ? 1 :
            std::sin(std::numbers::pi * d) / (std::numbers::pi * d)) * window;
        // A tap outside the retained range added a zero product, which never
        // changes the sum: it starts at, and cancels to, positive zero.
        if (const auto* sample = retained[static_cast<std::size_t>(offset + 7)])
            value += coefficient * *sample;
        norm += coefficient;
    }
    return std::abs(norm) > 1e-12 ? value / norm : Complex{};
}

struct OscillatorState {
    double phase = 0, rw_frequency = 0;
    std::vector<double> flicker;
    std::vector<double> taus;
    // Factors of one phase step that depend on its fixed sample interval.
    double white_sigma = 0, random_walk_step = 0, wiener_step = 0, if_offset_hz = 0;
    std::vector<double> flicker_decay, flicker_innovation;
};

void validate_spectrum(const OscillatorSpectrum& s) {
    require(finite_nonnegative(s.white_fm_hz_sqrt_seconds) &&
            finite_nonnegative(s.random_walk_fm_hz_per_sqrt_second) &&
            finite_nonnegative(s.flicker_fm_rms_hz) &&
            finite_nonnegative(s.white_pm_rms_radians) &&
            std::isfinite(s.flicker_min_tau_seconds) && s.flicker_min_tau_seconds > 0 &&
            std::isfinite(s.flicker_max_tau_seconds) &&
            s.flicker_max_tau_seconds >= s.flicker_min_tau_seconds &&
            s.flicker_bands >= 1 && s.flicker_bands <= 64,
            "invalid finite-band oscillator spectrum");
}

// gaussian() returns the oscillator's standard-normal draws in order.
template<class Gaussian>
void initialize_spectrum(OscillatorState& state, const OscillatorSpectrum& s, Gaussian&& gaussian) {
    if (s.flicker_fm_rms_hz == 0) return;
    const auto count = s.flicker_bands;
    state.taus.resize(count);
    state.flicker.resize(count);
    const double lo = std::log(s.flicker_min_tau_seconds);
    const double hi = std::log(s.flicker_max_tau_seconds);
    const double component_rms = s.flicker_fm_rms_hz / std::sqrt(static_cast<double>(count));
    for (unsigned i = 0; i < count; ++i) {
        const double u = count == 1 ? .5 : static_cast<double>(i) / (count - 1);
        state.taus[i] = std::exp(lo + u * (hi - lo));
        state.flicker[i] = component_rms * gaussian();
    }
}

// The products of a phase step whose operands never change for one oscillator.
// Each keeps the operand order of the step it leaves.
void prepare_phase_step(OscillatorState& state, const Oscillator& base,
                        const OscillatorSpectrum& spectrum, double nominal_if_hz,
                        double audio_scale, double dt) {
    state.white_sigma = spectrum.white_fm_hz_sqrt_seconds / std::sqrt(dt);
    state.random_walk_step = spectrum.random_walk_fm_hz_per_sqrt_second * std::sqrt(dt);
    state.wiener_step = base.phase_noise_rad_per_sqrt_second * std::sqrt(dt);
    state.if_offset_hz = nominal_if_hz * (audio_scale - 1);
    state.flicker_decay.resize(state.flicker.size());
    state.flicker_innovation.resize(state.flicker.size());
    for (std::size_t i = 0; i < state.flicker.size(); ++i) {
        const double alpha = std::exp(-dt / state.taus[i]);
        state.flicker_decay[i] = alpha;
        state.flicker_innovation[i] =
            (spectrum.flicker_fm_rms_hz / std::sqrt(static_cast<double>(state.flicker.size()))) *
            std::sqrt(1 - alpha * alpha);
    }
}

// The standard-normal draws of one oscillator sample: its phase step's
// white, random-walk, flicker and Wiener draws, then its white PM draw.
std::uint64_t staged_draws(const OscillatorSpectrum& s) {
    return 4 + (s.flicker_fm_rms_hz > 0 ? s.flicker_bands : 0);
}

template<class Gaussian>
double oscillator_phase_step(OscillatorState& state, const Oscillator& base, Gaussian&& gaussian,
                             double rf_hz, double t, double dt) {
    const double white_frequency = state.white_sigma * gaussian();
    state.rw_frequency += state.random_walk_step * gaussian();
    double flicker_frequency = 0;
    for (std::size_t i = 0; i < state.flicker.size(); ++i) {
        state.flicker[i] = state.flicker_decay[i] * state.flicker[i] +
            state.flicker_innovation[i] * gaussian();
        flicker_frequency += state.flicker[i];
    }
    const double offset = rf_hz * (base.rf_ppm + base.rf_drift_ppm_per_second * t) * 1e-6 +
        state.if_offset_hz + white_frequency + state.rw_frequency +
        flicker_frequency;
    const double wiener = state.wiener_step * gaussian();
    const double result = tau * offset * dt + wiener;
    require(std::isfinite(result), "oscillator phase increment is non-finite");
    return result;
}

void validate_effects(const SimulationConfig& c, const Format& f) {
    const double finite_values[] = {c.sample_rate, c.occupied_bandwidth_hz, c.rf_hz,
        c.nominal_conversion_if_hz, c.tx.rf_ppm, c.tx.rf_drift_ppm_per_second,
        c.tx.phase_noise_rad_per_sqrt_second, c.tx.audio_ppm, c.rx.rf_ppm,
        c.rx.rf_drift_ppm_per_second, c.rx.phase_noise_rad_per_sqrt_second,
        c.rx.audio_ppm, c.timing_offset_seconds, c.capture_lead_seconds,
        c.capture_trail_seconds, c.static_doppler_hz, c.spread_delay_ms,
        c.spread_doppler_hz, c.perturbation_slow_depth,
        c.perturbation_fast_depth, c.dropout_ms, c.dropout_start_ms,
        c.retune_offset_seconds, c.dwell_seconds};
    for (double x : finite_values) require(std::isfinite(x), "non-finite streaming channel configuration");
    require(c.sample_rate == effective_sample_rate(f) && c.sample_rate > 0 &&
        c.occupied_bandwidth_hz > 0 && c.occupied_bandwidth_hz < c.sample_rate &&
        c.rf_hz >= 0 && c.nominal_conversion_if_hz >= 0 &&
        c.dwell_seconds > 0, "streaming channel rate or bandwidth is invalid");
    require(finite_nonnegative(c.spread_delay_ms) && finite_nonnegative(c.spread_doppler_hz) &&
        finite_nonnegative(c.perturbation_slow_depth) &&
        finite_nonnegative(c.perturbation_fast_depth) &&
        finite_nonnegative(c.tx.phase_noise_rad_per_sqrt_second) &&
        finite_nonnegative(c.rx.phase_noise_rad_per_sqrt_second),
        "streaming channel impairment is invalid");
    require(c.tx.audio_ppm > -1e6 && c.rx.audio_ppm > -1e6 &&
        c.capture_lead_seconds >= 0 && c.capture_trail_seconds >= 0 &&
        c.timing_offset_seconds >= -c.capture_lead_seconds && c.dropout_every_n_dwells > 0 &&
        c.dropout_ms >= 0 && c.dropout_start_ms >= 0 &&
        c.dropout_start_ms + c.dropout_ms <= c.dwell_seconds * 1000,
        "streaming channel timing or dropout is invalid");
    // Config owns a copy even on the explicit Off path. Bound that copy before
    // construction; inactive retained calendars must not allocate arbitrarily.
    require(c.hop_offsets_hz.size() <= 100'000,
            "FHSS channel calendar exceeds its bounded memory limit");
    if (c.fhss) {
        require(c.dwell_seconds == f.dwell_seconds && !c.hop_offsets_hz.empty(),
                "FHSS requires a matching 400-ms format and explicit hop calendar");
        for (double hop : c.hop_offsets_hz) require(std::isfinite(hop), "non-finite FHSS hop");
    }
    require(std::isfinite(c.static_doppler_hz), "invalid static Doppler");
}

StreamChannelConfig checked_config(const StreamChannelConfig& cfg, const Format& format) {
    validate_effects(cfg.effects, format);
    validate_spectrum(cfg.tx_spectrum);
    validate_spectrum(cfg.rx_spectrum);
    validate_frontend(cfg.frontend, cfg.effects.sample_rate*(1+cfg.effects.rx.audio_ppm*1e-6));
    validate_path_evolution(cfg.path_evolution);
    require(cfg.path_evolution.delay_excursion_seconds <= cfg.effects.spread_delay_ms*.001,
            "random delay excursion exceeds nominal excess delay");
    const bool spread_enabled = cfg.effects.spread_delay_ms > 0 || cfg.effects.spread_doppler_hz != 0;
    require(cfg.path_evolution.bounded_spread_knot_seconds == 0 || spread_enabled,
            "bounded spread evolution needs a configured two-path channel");
    require(cfg.path_evolution.bounded_spread_knot_seconds == 0 ||
            (cfg.path_evolution.delay_excursion_seconds == 0 &&
             cfg.path_evolution.diurnal_phase_amplitude_radians == 0 &&
             cfg.path_evolution.outage_start_seconds == -1 && cfg.path_evolution.outage_end_seconds == -1),
            "bounded spread cannot be combined with moving delay, diurnal phase or an outage");
    require((!cfg.selective_model.modulation_rate_hz ||
             finite_nonnegative(*cfg.selective_model.modulation_rate_hz)) &&
            finite_nonnegative(cfg.selective_model.correlation_spacing_hz) &&
            std::isfinite(cfg.selective_model.center_hz) &&
            finite_nonnegative(cfg.selective_tail_floor_seconds),
            "invalid versioned selective modulation model");
    require(cfg.selective_model.coefficient_law == SelectiveCoefficientLaw::Sinusoidal ||
            cfg.selective_model.coefficient_law == SelectiveCoefficientLaw::GaussianErf,
            "invalid selective coefficient law");
    require(std::isfinite(cfg.tx_frequency_drift_hz_per_second),
            "invalid supplemental TX frequency drift");
    require(cfg.missed_receiver_dwell_indices.size() <= 4096,
            "receiver missed-dwell schedule exceeds its 4096-entry bound");
    require(cfg.missed_receiver_dwell_indices.empty() || format.fhss || cfg.effects.fhss,
            "receiver missed-dwell indices require a public FHSS dwell calendar");
    for (std::size_t i = 1; i < cfg.missed_receiver_dwell_indices.size(); ++i)
        require(cfg.missed_receiver_dwell_indices[i - 1] < cfg.missed_receiver_dwell_indices[i],
                "receiver missed-dwell indices must be strictly increasing and unique");
    if (effective_sample_rate(format) < 64) {
        const auto fast_spectrum = [](const OscillatorSpectrum& s) {
            return s.white_fm_hz_sqrt_seconds != 0 || s.random_walk_fm_hz_per_sqrt_second != 0 ||
                s.flicker_fm_rms_hz != 0 || s.white_pm_rms_radians != 0;
        };
        require(!format.fhss && !cfg.effects.fhss && cfg.effects.dropout_ms == 0 && !cfg.effects.slow_agc &&
                !cfg.frontend.active() && !fast_spectrum(cfg.tx_spectrum) && !fast_spectrum(cfg.rx_spectrum),
                "sample rates below 64 Hz require FHSS/dropout/AGC/frontend/FM spectra/white PM off; refinement remains required");
    }
    return cfg;
}

EmittedSource pattern_descriptor(PatternSource& source) {
    EmittedSource result;
    result.format = source.format();
    result.geometry = source.geometry();
    result.sample_at = [&source](std::uint64_t n) {
        return source.sample_at(static_cast<long double>(n));
    };
    // The historical report accounts for channel storage only. Callers of
    // the explicit descriptor declare their source storage separately.
    return result;
}

} // namespace

struct StreamingChannel::Impl {
    Format format;
    Geometry geometry;
    std::function<Complex(std::uint64_t)> source_sample;
    StreamChannelConfig config;
    SimulationConfig& effects;
    double tx_scale = 1, rx_scale = 1, dt_tx = 0, dt_rx = 0;
    long double hop_origin_seconds = 0;
    std::uint64_t total = 0, cursor = 0, tx_next = 0;
    std::uint64_t delay_samples = 0, tx_interpolation_tail = 0, tail_samples = 0;
    // A ring of generated source samples. A received row reads the newest
    // retained_history of those generated when its own turn comes; the ring
    // also holds the samples of the block generated ahead of that row.
    std::vector<Complex> tx_history;
    std::uint64_t retained_history = 0, block_lookahead = 0;
    // Coordinates of a row that are the same for every row.
    long double tx_clock = 0, rx_clock = 0, tx_per_rx = 0, direct_origin = 0;
    long double fixed_delay_seconds = 0, fixed_delay_samples = 0;
    Complex still_hop_rotation{1, 0}, fixed_delay_rotation{1, 0};
    // One block of rows prepared ahead of the sequential pass. Each term
    // depends on its row's index and the generated source history alone.
    using Excursions = DispersiveSelectivePerturbation::Excursions;
    struct Reader {
        std::optional<DispersiveSelectivePerturbation::RandomExcursions> excursions;
        std::unique_ptr<std::array<detail::FixedGaussianRay, 2>> rays;
    };
    // The first row of a job whose fixed-time values could not be evaluated.
    // The sequential pass reports it where the unbatched reader met it.
    struct Failure {
        std::size_t row = SIZE_MAX;
        bool in_path = false, ran = false;
        std::exception_ptr error;
        // The first row whose coordinates could not be evaluated.
        std::size_t path_row = SIZE_MAX;
    };
    struct Block {
        std::vector<Complex> direct, delayed;
        std::vector<std::array<Complex, 2>> path_gain;
        std::vector<Excursions> excursion;
        // The moving echo delay of each row; others use the fixed delay.
        std::vector<long double> delay_seconds;
        // With lanes: each row's physical time, and the selective filter's
        // delayed branches when their shortest lag reaches back past the
        // block that the pass beside this preparation filters.
        std::vector<long double> physical_time;
        std::vector<std::array<Complex, 2>> branches;
        // Rows [0, carried) hold their coordinates; the sequential pass
        // evaluates those of a later row itself, which reports their failure.
        std::size_t carried = 0;
        std::uint64_t first = 0, source_begin = 0;
        // The row whose source history could not be generated, and why.
        std::size_t source_failure_row = SIZE_MAX;
        std::exception_ptr source_failure;
        Failure failure;
    };
    // With split stages the sequential pass over one block runs beside the
    // preparation of the next, so a block and its chains have two copies.
    std::array<Block, 2> blocks;
    std::size_t prepare_slot = 0;
    std::size_t block_rows = 0, lane_jobs = 1;
    std::vector<Reader> readers;
    std::vector<Failure> job_failures;
    // With lanes, the sequential states that never read the signal advance a
    // whole block ahead of the reader, each on a job of its own, and the next
    // block's source history is generated beside the rows of this one.
    enum Chain : std::size_t { ahead_chain, echo_chain, common_chain, chain_count };
    struct Chains {
        std::vector<Complex> echo_rotation, common_rotation;
        std::vector<detail::BoundedRandomSpread::State> echo;
        std::vector<double> common_doppler_hz;
        std::array<Failure, chain_count> failures;
    };
    std::array<Chains, 2> chain_sets;
    // With lanes, the receiver oscillator and the thermal noise of the rows,
    // and with a split transmit path the transmit oscillator of the source
    // samples, are staged. Their items take per_item draws each from one
    // GaussianStream. Only accepting the draws and the oscillator's phase
    // recurrence run in order, each as a job of its own; forming the draws
    // and the oscillator's rotations are chunks that the pool jobs share.
    // Every stage reads what the blocks before left, so a block's jobs form
    // the rotations of its own items, carry the recurrence one lead further,
    // form draws two leads and accept them three leads ahead. A chain that
    // is behind catches up on the reader first. Items and their values are
    // those of the unbatched reader; only where they are computed changes.
    enum Staged : std::size_t { receive_staged, transmit_staged, thermal_staged, staged_count };
    struct DrawChain {
        explicit DrawChain(std::uint64_t seed) : stream(seed) {}
        GaussianStream stream;
        bool active = false, recurrence = false;
        std::uint64_t per_item = 0, offset = 0, items = 0, lead = 0;
        // Items [0, phased) passed the recurrence; values [formed - ring, formed) exist.
        std::uint64_t phased = 0, formed = 0;
        // The first item whose recurrence failed, and why. Its consumer
        // reports it where the unbatched reader met it.
        std::uint64_t failure = UINT64_MAX;
        std::exception_ptr error;
        std::vector<double> phase, white_pm;
        std::vector<Complex> value;
        // The receiver's dwell index of each row, which the pass reads too.
        std::vector<std::int64_t> dwell;
        std::uint64_t draw(std::uint64_t item) const { return offset + item * per_item; }
        // The items whose draws are all formed.
        std::uint64_t drawn() const {
            const auto formed_draws = stream.finished();
            return formed_draws <= offset ? 0 : (formed_draws - offset) / per_item;
        }
        static std::uint64_t draw_capacity(std::uint64_t lead, std::uint64_t per_item) { return 4 * lead * per_item + 4; }
        static std::uint64_t pair_capacity(std::uint64_t lead, std::uint64_t per_item) { return lead * per_item + 2; }
        static std::size_t bytes(std::uint64_t lead, std::uint64_t per_item, bool recurrence) {
            return GaussianStream::bytes(static_cast<std::size_t>(draw_capacity(lead, per_item)),
                                         static_cast<std::size_t>(pair_capacity(lead, per_item))) +
                (recurrence ? static_cast<std::size_t>(3 * lead) * (3 * sizeof(double) + sizeof(Complex)) : 0);
        }
        Complex at(std::uint64_t item) const { return value[static_cast<std::size_t>(item % value.size())]; }
    };
    std::array<DrawChain, staged_count> staged;
    // One block's stage ranges of a staged chain, fixed before its jobs run.
    struct ChainRound {
        std::uint64_t accept_to = 0, finish_from = 0, finish_to = 0;
        std::uint64_t phase_from = 0, phase_to = 0, form_from = 0, form_to = 0;
        // Where the recurrence job stopped, and why when an item failed. The
        // reader records it in the chain after the jobs.
        std::uint64_t phased = 0, failure = UINT64_MAX;
        std::exception_ptr error;
        bool phase_ran = false;
    };
    // The work of a block that any of its pool jobs claims: its rows in
    // shrinking runs, then fixed chunks of the staged chains' formations.
    struct Pool {
        static constexpr std::size_t row_grain = 16, draw_grain = 512, item_grain = 256;
        struct Segment {
            Staged chain = receive_staged;
            bool values = false;
            std::uint64_t from = 0, to = 0;
            std::size_t first_chunk = 0;
        };
        std::atomic<std::size_t> next_row{0}, next_chunk{0};
        std::size_t rows = 0, jobs = 1, chunks = 0, segment_count = 0;
        std::array<Segment, 2 * staged_count> segments{};
        void add(Staged chain, bool values, std::uint64_t from, std::uint64_t to) {
            if (from >= to) return;
            const auto grain = values ? item_grain : draw_grain;
            segments[segment_count++] = {chain, values, from, to, chunks};
            chunks += static_cast<std::size_t>((to - from + grain - 1) / grain);
        }
        bool claim_rows(std::size_t& begin, std::size_t& end) {
            auto current = next_row.load(std::memory_order_relaxed);
            for (;;) {
                if (current >= rows) return false;
                const auto size = std::max(row_grain, (rows - current) / (2 * jobs));
                const auto stop = std::min(rows, current + size);
                if (next_row.compare_exchange_weak(current, stop, std::memory_order_relaxed)) {
                    begin = current;
                    end = stop;
                    return true;
                }
            }
        }
    };
    // One block's sequential pass, delivered at offset of a read's output.
    struct Pass {
        std::span<Complex> output;
        std::span<std::uint8_t> validity;
        std::size_t offset = 0, rows = 0, slot = 0;
        std::exception_ptr error;
        bool ran = false;
    };
    bool chained = false, selective_ahead = false;
    // The source history that the unbatched reader had generated at the cursor.
    std::uint64_t reader_source_end = 0;
    // A source failure met while generating ahead. The reader reports it at
    // the first row that needs the sample.
    std::exception_ptr ahead_failure;
    // A failed read ends a channel whose chains had already passed that row.
    std::exception_ptr ended;
    // With lanes, a transmit path that never reads a sample's value (no DAC
    // and no diagnostic I/Q noise) splits a source range into stages: one job
    // forms the TX oscillator factor of each sample, the source's readers
    // evaluate disjoint parts of the range on others, and the reader then
    // accounts the samples in order and multiplies value and factor. Without
    // readers the fused loop keeps the source callback beside the oscillator.
    struct Stage {
        std::uint64_t index = UINT64_MAX;
        std::exception_ptr error;
        bool ran = false;
    };
    bool split_transmit = false;
    std::shared_ptr<EmittedSourceReaders> source_readers;
    std::size_t source_reader_count = 0;
    std::vector<Stage> source_stages;
    // Whether every planned transmit rotation of the range being generated
    // was formed.
    bool transmit_formed = false;
    // Every TX calendar coordinate of the source converts to a dwell index,
    // so a channel without FHSS need not form them.
    bool tx_calendar_bounded = false;
    std::unique_ptr<background_noise::Generator> transmit_diagnostic_iq_noise;
    OscillatorState tx_osc, rx_osc;
    Rng tx_rng, rx_rng, thermal_rng;
    Rng tx_spectrum_rng, rx_spectrum_rng;
    Rng dac_rng, adc_rng, interference_rng;
    Rng path_rng;
    Complex direct_gain{1,0}, delayed_gain{1,0};
    double path_gain_decay = 1, path_gain_innovation = 0;
    std::unique_ptr<detail::GaussianRayFading> gaussian_path;
    std::array<std::unique_ptr<detail::FixedGaussianRay>,2> random_path;
    std::unique_ptr<detail::SmoothRandomDelay> random_delay;
    std::unique_ptr<detail::BoundedRandomSpread> bounded_spread;
    double bounded_direct_gain = 1, bounded_echo_gain = 0;
    std::unique_ptr<detail::BoundedRandomSpread> common_doppler_wander;
    std::unique_ptr<SpreadRay> spread;
    std::unique_ptr<DispersiveSelectivePerturbation> dispersive_perturbation;
    std::vector<long double> hop_prefix;
    std::int64_t previous_tx_dwell = INT64_MIN, previous_rx_dwell = INT64_MIN;
    double agc_power = 1, agc_alpha = 0;
    Complex frontend_filter_state{};
    double frontend_filter_alpha = 0, recovery_scale = 1, recovery_alpha = 0;
    double tone_initial_phase = 0;
    StreamChannelReport stats;

    Impl(PatternSource& src, const StreamChannelConfig& cfg)
        : Impl(pattern_descriptor(src), cfg, false) {}

    Impl(EmittedSource src, const StreamChannelConfig& cfg, bool explicit_source = true)
        : format(src.format), geometry(src.geometry), source_sample(std::move(src.sample_at)),
          config(checked_config(cfg, format)),
          effects(config.effects),
          // The staged chains draw the streams of rx_spectrum_rng,
          // tx_spectrum_rng and thermal_rng, which they then replace.
          staged{DrawChain(root_seed(cfg.oscillator_seed,cfg.effects.seed) ^ 0x9e37a202ULL),
                 DrawChain(root_seed(cfg.oscillator_seed,cfg.effects.seed) ^ 0x9e37a201ULL),
                 DrawChain(root_seed(cfg.noise_seed,cfg.effects.seed) ^ 0x3254494154415747ULL)},
          tx_rng(root_seed(cfg.oscillator_seed,cfg.effects.seed) ^ 0x3254494154545801ULL),
          rx_rng(root_seed(cfg.oscillator_seed,cfg.effects.seed) ^ 0x3254494154525802ULL),
          thermal_rng(root_seed(cfg.noise_seed,cfg.effects.seed) ^ 0x3254494154415747ULL),
          tx_spectrum_rng(root_seed(cfg.oscillator_seed,cfg.effects.seed) ^ 0x9e37a201ULL),
          rx_spectrum_rng(root_seed(cfg.oscillator_seed,cfg.effects.seed) ^ 0x9e37a202ULL),
          dac_rng(root_seed(cfg.frontend_seed,cfg.effects.seed) ^ 0x444143434f4e5631ULL),
          adc_rng(root_seed(cfg.frontend_seed,cfg.effects.seed) ^ 0x414443434f4e5631ULL),
          interference_rng(root_seed(cfg.frontend_seed,cfg.effects.seed) ^ 0x494e544552464531ULL),
          path_rng(root_seed(cfg.path_seed,cfg.effects.seed) ^ 0x504154484741494eULL) {
        if (explicit_source) {
            require(static_cast<bool>(source_sample), "emitted source needs a sample callback");
            require(geometry.source_samples > 0 && geometry.source_samples < UINT64_MAX - 16 &&
                    geometry.calendar_origin_sample < geometry.source_samples &&
                    std::isfinite(format.dwell_seconds) && format.dwell_seconds > 0 &&
                    std::isfinite(geometry.carrier_spacing_hz) && geometry.carrier_spacing_hz > 0 &&
                    std::isfinite(geometry.occupied_bandwidth_hz) && geometry.occupied_bandwidth_hz > 0,
                    "emitted source extent, calendar or DATA geometry is invalid");
            require(std::isfinite(src.minimum_frequency_hz) && std::isfinite(src.maximum_frequency_hz) &&
                    src.minimum_frequency_hz <= src.maximum_frequency_hz,
                    "emitted source needs finite ordered signed frequency bounds");
        }
        validate_effects(effects, format);
        if (cfg.diagnostic_iq_noise.mode != background_noise::Mode::Off) {
            auto noise_config = cfg.diagnostic_iq_noise;
            noise_config.full_scale = config.frontend.dac.full_scale;
            const auto level = background_noise::resolve(noise_config);
            transmit_diagnostic_iq_noise = std::make_unique<background_noise::Generator>(noise_config);
            stats.diagnostic_iq_noise_enabled = true;
            stats.diagnostic_iq_noise_sample_rate_hz = effects.sample_rate;
            stats.diagnostic_iq_noise_config = noise_config;
            stats.diagnostic_iq_noise_expected_variance = level.variance * config.signal_gain * config.signal_gain;
            stats.gaussian_projection_supported = false;
        }
        validate_spectrum(config.tx_spectrum);
        validate_spectrum(config.rx_spectrum);
        require(std::isfinite(config.signal_gain) && config.signal_gain >= 0 &&
                std::isfinite(config.complex_noise_variance) && config.complex_noise_variance >= 0,
                "streaming channel gain or absolute noise variance is invalid");
        require(config.maximum_chunk_samples > 0 &&
                config.maximum_chunk_samples <= 1'048'576,
                "streaming channel chunk limit is invalid");
        tx_scale = 1 + effects.tx.audio_ppm * 1e-6;
        rx_scale = 1 + effects.rx.audio_ppm * 1e-6;
        require(std::isfinite(tx_scale) && tx_scale > 0 &&
                std::isfinite(rx_scale) && rx_scale > 0,
                "streaming audio clock is not positive and finite");
        dt_tx = 1 / (effects.sample_rate * tx_scale);
        dt_rx = 1 / (effects.sample_rate * rx_scale);
        require(std::isfinite(dt_tx) && dt_tx > 0 && std::isfinite(dt_rx) && dt_rx > 0,
                "streaming sample interval is outside numeric range");
        hop_origin_seconds = static_cast<long double>(geometry.calendar_origin_sample) /
            effects.sample_rate;
        tx_clock = static_cast<long double>(effects.sample_rate) * tx_scale;
        rx_clock = static_cast<long double>(effects.sample_rate) * rx_scale;
        tx_per_rx = static_cast<long double>(tx_scale) / rx_scale;
        direct_origin = (static_cast<long double>(effects.capture_lead_seconds) + effects.timing_offset_seconds) *
            effects.sample_rate * tx_scale;
        require(config.lanes.count <= 1 || static_cast<bool>(config.lanes.run),
                "streaming channel lanes need a function that runs their jobs");
        // A block of 1024 rows gives sixteen jobs enough rows each; more
        // would only add copies of the fixed-time processes.
        lane_jobs = config.lanes.run ? std::clamp<std::size_t>(config.lanes.count, 1, 16) : 1;

        const long double maximum_delay_seconds = effects.spread_delay_ms * .001L +
            config.path_evolution.delay_excursion_seconds;
        const long double delay_real = maximum_delay_seconds *
            effects.sample_rate * tx_scale;
        require(std::isfinite(delay_real) && delay_real <=
            static_cast<long double>(kHistoryLimitBytes / sizeof(Complex) - 64),
            "streaming spread delay exceeds the bounded oscillator history");
        delay_samples = static_cast<std::uint64_t>(std::ceil(delay_real));
        const long double interpolation_tail = std::ceil(8.L * rx_scale / tx_scale) + 2;
        require(std::isfinite(interpolation_tail) && interpolation_tail < UINT64_MAX,
                "stream channel interpolation tail overflows sample coordinates");
        tx_interpolation_tail = static_cast<std::uint64_t>(interpolation_tail);
        const bool spread_enabled = effects.spread_delay_ms > 0 || effects.spread_doppler_hz > 0;
        const double selective_rate = config.selective_model.modulation_rate_hz.value_or(
            effects.spread_doppler_hz);
        const bool perturb_enabled = selective_rate > 0 &&
            (effects.perturbation_slow_depth > 0 || effects.perturbation_fast_depth > 0);
        const double selective_sideband = selective_rate * (effects.perturbation_fast_depth > 0 ? 1 :
            effects.perturbation_slow_depth > 0 ? .1 : 0);
        const bool random_selective = config.selective_model.coefficient_law == SelectiveCoefficientLaw::GaussianErf;
        // erf broadens a Gaussian spectrum. Ten B per edge is an explicit
        // engineering allowance, never a compact-support guarantee.
        const double selective_capture_allowance = random_selective ? 10*selective_sideband :
            config.selective_model.modulation_rate_hz ?
            selective_sideband : (perturb_enabled ? effects.spread_doppler_hz : 0);
        const double pulse_spacing = geometry.nominal_carrier_bandwidth_hz > 0 ?
            geometry.nominal_carrier_bandwidth_hz : geometry.carrier_spacing_hz;
        const double selective_spacing = config.selective_model.correlation_spacing_hz > 0 ?
            config.selective_model.correlation_spacing_hz : pulse_spacing;
        const auto actual_perturb_tail = perturb_enabled ?
            DispersiveSelectivePerturbation::tail_samples(effects.sample_rate * rx_scale, selective_spacing) : 0;
        const long double selective_floor_real = std::ceil(
            static_cast<long double>(config.selective_tail_floor_seconds) * effects.sample_rate * rx_scale);
        require(std::isfinite(selective_floor_real) && selective_floor_real >= 0 &&
                selective_floor_real < UINT64_MAX,
                "selective observation tail floor overflows sample coordinates");
        const auto perturb_tail = std::max(actual_perturb_tail,
            static_cast<std::uint64_t>(selective_floor_real));
        const long double delay_tail_real = maximum_delay_seconds *
            effects.sample_rate * rx_scale;
        require(perturb_tail <= UINT64_MAX - tx_interpolation_tail &&
                std::isfinite(delay_tail_real) && delay_tail_real <
                    static_cast<long double>(UINT64_MAX - perturb_tail - tx_interpolation_tail),
                "stream channel tail overflows sample coordinates");
        std::uint64_t frontend_tail = 0;
        if (config.frontend.lowpass_hz > 0) {
            frontend_filter_alpha = std::exp(-tau*config.frontend.lowpass_hz*dt_rx);
            const long double filter_tail = std::ceil(std::log(1e6L) /
                (tau*config.frontend.lowpass_hz*dt_rx));
            require(std::isfinite(filter_tail) && filter_tail <= 128'000'000,
                    "frontend filter tail exceeds its explicit sample bound");
            frontend_tail = static_cast<std::uint64_t>(filter_tail);
        }
        require(frontend_tail <= UINT64_MAX - perturb_tail - tx_interpolation_tail -
                static_cast<std::uint64_t>(std::ceil(delay_tail_real)),
                "frontend tail overflows sample coordinates");
        tail_samples = static_cast<std::uint64_t>(std::ceil(delay_tail_real)) +
            perturb_tail + tx_interpolation_tail + frontend_tail;
        const long double lead_samples = effects.capture_lead_seconds *
            effects.sample_rate * rx_scale;
        const long double end_seconds = std::max(0.0L,
            static_cast<long double>(geometry.source_samples) /
                (effects.sample_rate * tx_scale) +
            effects.timing_offset_seconds) + effects.capture_trail_seconds +
            static_cast<long double>(tail_samples) / (effects.sample_rate * rx_scale);
        const long double total_real = std::ceil(lead_samples + end_seconds *
            effects.sample_rate * rx_scale);
        require(std::isfinite(total_real) && total_real >= 0 && total_real < UINT64_MAX,
                "stream channel observation extent overflows sample coordinates");
        const auto natural_total = static_cast<std::uint64_t>(total_real);
        total = std::max(natural_total, config.minimum_capture_samples);
        stats.natural_complete_samples = natural_total;
        stats.observation_padding_samples = total - natural_total;
        const double maximum_hop = effects.fhss ? std::max(
            std::abs(*std::min_element(effects.hop_offsets_hz.begin(), effects.hop_offsets_hz.end())),
            std::abs(*std::max_element(effects.hop_offsets_hz.begin(), effects.hop_offsets_hz.end()))) : 0;
        const double delay_motion_allowance = detail::SmoothRandomDelay::maximum_slope(
            config.path_evolution.delay_excursion_seconds,config.path_evolution.delay_knot_seconds) *
            (effects.rf_hz + effects.nominal_conversion_if_hz + maximum_hop +
             (explicit_source ? std::max(std::abs(src.minimum_frequency_hz),std::abs(src.maximum_frequency_hz)) :
              effects.occupied_bandwidth_hz*.5));
        const double elapsed = static_cast<double>(total) * dt_rx;
        const double cfo_bound = (effects.rf_hz + maximum_hop) *
            (std::abs(effects.tx.rf_ppm) + std::abs(effects.rx.rf_ppm) +
             (std::abs(effects.tx.rf_drift_ppm_per_second) +
              std::abs(effects.rx.rf_drift_ppm_per_second)) * elapsed) * 1e-6 +
            effects.nominal_conversion_if_hz *
                (std::abs(effects.tx.audio_ppm) + std::abs(effects.rx.audio_ppm)) * 1e-6 +
            std::abs(effects.static_doppler_hz) +
            config.path_evolution.common_doppler_wander_hz +
            (spread_enabled ? effects.spread_doppler_hz : 0) +
            selective_capture_allowance + delay_motion_allowance + std::abs(config.tx_frequency_drift_hz_per_second) *
                (static_cast<double>(geometry.source_samples) * dt_tx) +
            config.path_evolution.diurnal_phase_amplitude_radians/config.path_evolution.diurnal_period_seconds;
        // Six sigma at each spectral edge: declared allowance for Gaussian
        // tails, not a proof that a stochastic finite-FIR spectrum is compact.
        const double gaussian_allowance = 6*(config.path_evolution.gaussian_doppler_width_hz +
            config.path_evolution.random_doppler_width_hz);
        require(std::isfinite(cfo_bound) &&
                effects.sample_rate * rx_scale > effects.occupied_bandwidth_hz * tx_scale + 2 * cfo_bound + gaussian_allowance,
                "stream channel sample clock cannot contain deterministic offsets and Gaussian-tail allowance");

        if (explicit_source) {
            const long double source_seconds = static_cast<long double>(geometry.source_samples) * dt_tx;
            const long double receive_start = -static_cast<long double>(effects.capture_lead_seconds);
            const long double receive_end = static_cast<long double>(total) * dt_rx + receive_start;
            const long double rx_time_bound = std::max(std::abs(receive_start), std::abs(receive_end));
            // Check the complete calendar before a read can convert it to a
            // signed dwell index. Include the TX and delayed-arrival calendars.
            const long double calendar_bound = std::max(source_seconds, rx_time_bound) +
                std::abs(static_cast<long double>(effects.timing_offset_seconds)) +
                std::abs(static_cast<long double>(effects.retune_offset_seconds)) +
                maximum_delay_seconds + hop_origin_seconds;
            require(std::isfinite(calendar_bound) && calendar_bound / effects.dwell_seconds < INT64_MAX - 1.L &&
                    calendar_bound / format.dwell_seconds < INT64_MAX - 1.L,
                    "emitted source calendar exceeds signed dwell coordinates");
            const long double rf_bound = effects.rf_hz + maximum_hop;
            const long double tx_offset = rf_bound * (std::abs(effects.tx.rf_ppm) +
                std::abs(effects.tx.rf_drift_ppm_per_second) * source_seconds) * 1e-6L +
                effects.nominal_conversion_if_hz * std::abs(tx_scale - 1.L) +
                std::abs(config.tx_frequency_drift_hz_per_second) * source_seconds;
            const long double rx_offset = rf_bound * (std::abs(effects.rx.rf_ppm) +
                std::abs(effects.rx.rf_drift_ppm_per_second) * rx_time_bound) * 1e-6L +
                effects.nominal_conversion_if_hz * std::abs(rx_scale - 1.L) +
                std::abs(effects.static_doppler_hz);
            const long double propagation_allowance = (spread_enabled ? effects.spread_doppler_hz : 0) +
                selective_capture_allowance + delay_motion_allowance + gaussian_allowance * .5L +
                config.path_evolution.common_doppler_wander_hz +
                config.path_evolution.diurnal_phase_amplitude_radians / config.path_evolution.diurnal_period_seconds;
            const long double low = static_cast<long double>(src.minimum_frequency_hz) * tx_scale;
            const long double high = static_cast<long double>(src.maximum_frequency_hz) * tx_scale;
            auto contained = [&](long double nyquist, long double allowance) {
                return std::isfinite(nyquist) && std::isfinite(allowance) &&
                    low - allowance > -nyquist && high + allowance < nyquist;
            };
            require(contained(effects.sample_rate * tx_scale * .5L, tx_offset),
                    "emitted source and TX oscillator exceed the TX sample clock");
            require(contained(effects.sample_rate * rx_scale * .5L, tx_offset + propagation_allowance),
                    "emitted source exceeds the pre-selective RX capture band");
            require(contained(effects.sample_rate * rx_scale * .5L, tx_offset + propagation_allowance + rx_offset),
                    "emitted source exceeds the final RX capture band");
        }

        if (config.path_evolution.gaussian_doppler_width_hz > 0) {
            gaussian_path = std::make_unique<detail::GaussianRayFading>(
                config.path_evolution.gaussian_doppler_width_hz, dt_rx,
                root_seed(config.path_seed,effects.seed));
            const auto coefficients = gaussian_path->coefficients();
            stats.path_gaussian_fir_coefficients.assign(coefficients.begin(), coefficients.end());
            stats.path_gaussian_sample_interval_seconds = dt_rx;
        }
        if (config.path_evolution.random_doppler_width_hz > 0)
            for(std::size_t ray=0;ray<random_path.size();++ray)
                random_path[ray]=std::make_unique<detail::FixedGaussianRay>(
                    config.path_evolution.random_doppler_width_hz,
                    root_seed(config.path_seed,effects.seed)^mix64(0x52414e4452415900ULL+ray));
        if (config.path_evolution.delay_excursion_seconds > 0)
            random_delay=std::make_unique<detail::SmoothRandomDelay>(effects.spread_delay_ms*.001,
                config.path_evolution.delay_excursion_seconds,config.path_evolution.delay_knot_seconds,
                root_seed(config.path_seed,effects.seed)^0x52414e4444454c41ULL);

        retained_history = delay_samples + 64;
        const auto prefix_count = effects.fhss ? effects.hop_offsets_hz.size() + 1 : 0;
        const auto spectrum_values = (config.tx_spectrum.flicker_fm_rms_hz > 0 ? config.tx_spectrum.flicker_bands : 0) +
            (config.rx_spectrum.flicker_fm_rms_hz > 0 ? config.rx_spectrum.flicker_bands : 0);
        // The first job reads the channel's own fixed-time processes; every
        // further job owns a copy of those the selected laws evaluate.
        const bool reads_rays = config.path_evolution.random_doppler_width_hz > 0;
        const bool reads_excursions = perturb_enabled && random_selective;
        const bool moving_delay = config.path_evolution.bounded_spread_knot_seconds == 0 &&
            config.path_evolution.delay_excursion_seconds > 0;
        // Only the DAC and the diagnostic I/Q noise read a source value
        // before the TX oscillator multiplies it.
        const bool value_free_transmit = !effects.signal_enabled || (!transmit_diagnostic_iq_noise &&
            !config.frontend.dac.nominal_bits && config.frontend.dac.gain == 1);
        const bool reads_source = explicit_source && src.readers && effects.signal_enabled;
        const auto size_block = [&](std::size_t jobs, bool with_readers) {
            lane_jobs = jobs;
            // Each job needs enough rows to repay its hand-off, and a read
            // of several blocks generates all but the first one's source
            // history beside the rows of the block before it.
            block_rows = std::min<std::size_t>(config.maximum_chunk_samples,
                jobs > 1 ? std::min<std::size_t>(1024, 128 * jobs) : 128);
            // The source advances tx_per_rx samples for each received row. A
            // block that would outrun this allowance is shortened when prepared.
            // Chains keep the next block's allowance beside the current one.
            block_lookahead = 2 * block_rows + 16;
            chained = jobs > 1;
            source_reader_count = chained && value_free_transmit && with_readers && reads_source ?
                std::min<std::size_t>(jobs, kSourceReaders) : 0;
            split_transmit = chained && value_free_transmit && (source_reader_count || !effects.signal_enabled);
            const auto chain_row = !chained ? 0 :
                (config.path_evolution.bounded_spread_knot_seconds > 0 ?
                    sizeof(detail::BoundedRandomSpread::State) + sizeof(Complex) : 0) +
                (config.path_evolution.common_doppler_wander_hz > 0 ? sizeof(double) + sizeof(Complex) : 0);
            const auto staged_bytes = !chained ? 0 :
                DrawChain::bytes(block_rows, staged_draws(config.rx_spectrum), true) +
                (effects.noise_enabled ? DrawChain::bytes(block_rows, 2, false) : 0) +
                (split_transmit ? DrawChain::bytes(block_lookahead, staged_draws(config.tx_spectrum), true) : 0);
            selective_ahead = chained && perturb_enabled &&
                DispersiveSelectivePerturbation::minimum_lag(effects.sample_rate * rx_scale, selective_spacing) >=
                    2 * block_rows;
            const auto ahead_row = !chained ? 0 : sizeof(long double) +
                (selective_ahead ? sizeof(std::array<Complex, 2>) : 0);
            return (chained ? 2 : 1) * block_lookahead * sizeof(Complex) + (split_transmit ? 2 : 1) * block_rows *
                    (2 * sizeof(Complex) + chain_row + ahead_row + (reads_rays ? sizeof(std::array<Complex, 2>) : 0) +
                     (reads_excursions ? sizeof(Excursions) : 0) + (moving_delay ? sizeof(long double) : 0)) +
                jobs * sizeof(Failure) + (jobs - 1) * (sizeof(Reader) +
                    (reads_rays ? 2 * sizeof(detail::FixedGaussianRay) : 0) +
                    (reads_excursions ? DispersiveSelectivePerturbation::maximum_random_working_bytes() : 0)) +
                staged_bytes + (split_transmit ? source_reader_count * sizeof(Stage) : 0);
        };
        // All owned arrays are fixed-size; no deque growth or duration-sized
        // history is hidden from this pre-allocation bound. Fixed allowance
        // covers allocator metadata for the bounded number of allocations.
        const auto fixed_bytes = sizeof(*this) + 4096 + static_cast<std::size_t>(retained_history) * sizeof(Complex) +
            effects.hop_offsets_hz.capacity() * sizeof(double) + prefix_count * sizeof(long double) +
            config.missed_receiver_dwell_indices.capacity() * sizeof(std::uint64_t) +
            spectrum_values * 4 * sizeof(double) +
            (gaussian_path ? gaussian_path->working_bytes() : 0) +
            (random_path[0] ? 2*sizeof(detail::FixedGaussianRay) : 0) +
            (random_delay ? sizeof(detail::SmoothRandomDelay) : 0) +
            (config.path_evolution.bounded_spread_knot_seconds > 0 ?
                sizeof(detail::BoundedRandomSpread) : 0) +
            (config.path_evolution.common_doppler_wander_hz > 0 ?
                sizeof(detail::BoundedRandomSpread) : 0) +
            stats.path_gaussian_fir_coefficients.capacity()*sizeof(double) +
            (spread_enabled && config.path_evolution.bounded_spread_knot_seconds == 0 ?
                sizeof(SpreadRay) : 0) +
            (perturb_enabled ? sizeof(DispersiveSelectivePerturbation) +
                DispersiveSelectivePerturbation::memory_samples(effects.sample_rate * rx_scale,
                                                               selective_spacing)*sizeof(Complex) +
                (random_selective ? DispersiveSelectivePerturbation::maximum_random_working_bytes() : 0) : 0);
        // Lanes never decide whether a channel fits: one that only fits
        // without their rows and process copies keeps its reader alone.
        constexpr auto workspace_limit = StreamingChannel::maximum_working_bytes;
        const auto source_bytes = explicit_source ? src.working_bytes : 0;
        const auto fits = [&](std::size_t block, std::size_t readers) {
            return fixed_bytes <= workspace_limit && block <= workspace_limit - fixed_bytes &&
                source_bytes <= workspace_limit - fixed_bytes - block &&
                readers <= workspace_limit - fixed_bytes - block - source_bytes;
        };
        // The source's readers are charged with the source. Without room for
        // them a channel keeps its lanes and the fused source loop.
        auto block_bytes = size_block(lane_jobs, true);
        std::size_t reader_bytes = source_reader_count ?
            src.readers->reader_bytes(source_reader_count, block_lookahead) : 0;
        if (lane_jobs > 1 && !fits(block_bytes, reader_bytes)) {
            block_bytes = size_block(lane_jobs, false);
            reader_bytes = 0;
            if (!fits(block_bytes, 0)) block_bytes = size_block(1, false);
        }
        stats.working_bytes = fixed_bytes + block_bytes;
        require(stats.working_bytes <= workspace_limit,
                "stream channel workspace exceeds its fixed bound");
        if (explicit_source) {
            require(src.working_bytes <= workspace_limit - stats.working_bytes,
                    "emitted source and channel workspace exceed the fixed bound");
            stats.working_bytes += src.working_bytes;
        }
        if (source_reader_count) {
            stats.working_bytes += reader_bytes;
            source_readers = src.readers;
            source_readers->open(source_reader_count, block_lookahead);
            source_stages.resize(source_reader_count);
        }
        if (chained) {
            open_staged(receive_staged, true, block_rows, staged_draws(config.rx_spectrum), total);
            if (effects.noise_enabled) open_staged(thermal_staged, false, block_rows, 2, total);
        }
        if (split_transmit)
            open_staged(transmit_staged, true, block_lookahead, staged_draws(config.tx_spectrum), geometry.source_samples);
        {
            const long double extent = static_cast<long double>(geometry.source_samples) / tx_clock +
                std::abs(hop_origin_seconds);
            tx_calendar_bounded = std::isfinite(extent) && extent / effects.dwell_seconds < 0x1p62L;
        }
        tx_history.resize(static_cast<std::size_t>(retained_history + (chained ? 2 : 1) * block_lookahead));
        for (std::size_t slot = 0; slot < (split_transmit ? 2 : 1); ++slot) {
            auto& block = blocks[slot];
            block.direct.resize(block_rows);
            block.delayed.resize(block_rows);
            if (reads_rays) block.path_gain.resize(block_rows);
            if (reads_excursions) block.excursion.resize(block_rows);
            if (moving_delay) block.delay_seconds.resize(block_rows);
            if (!chained) continue;
            block.physical_time.resize(block_rows);
            if (selective_ahead) block.branches.resize(block_rows);
            auto& chains = chain_sets[slot];
            if (config.path_evolution.bounded_spread_knot_seconds > 0) {
                chains.echo.resize(block_rows);
                chains.echo_rotation.resize(block_rows);
            }
            if (config.path_evolution.common_doppler_wander_hz > 0) {
                chains.common_doppler_hz.resize(block_rows);
                chains.common_rotation.resize(block_rows);
            }
        }
        job_failures.resize(lane_jobs);

        if (spread_enabled && config.path_evolution.bounded_spread_knot_seconds == 0)
            spread = std::make_unique<SpreadRay>(
            effects.sample_rate * rx_scale, effects.spread_doppler_hz,
            effects.seed ^ 0x325449415443484eULL);
        if (config.path_evolution.bounded_spread_knot_seconds > 0) {
            spread.reset();
            const auto seed = root_seed(config.path_seed,effects.seed)^0x424f554e44535052ULL;
            bounded_spread = std::make_unique<detail::BoundedRandomSpread>(
                effects.spread_delay_ms*.001, effects.spread_doppler_hz,
                config.path_evolution.bounded_spread_knot_seconds, seed);
            auto& diagnostic = stats.bounded_spread.emplace();
            diagnostic.excess_delay_seconds = bounded_spread->excess_delay_seconds();
            diagnostic.knot_seconds = config.path_evolution.bounded_spread_knot_seconds;
            diagnostic.doppler_bound_hz = std::abs(effects.spread_doppler_hz);
            diagnostic.seed = seed;
            if (const auto p = config.path_evolution.bounded_spread_direct_power_fraction) {
                bounded_direct_gain = std::sqrt(*p);
                bounded_echo_gain = std::sqrt(1-*p);
                diagnostic.fixed_direct_power_fraction = p;
                const auto ratio = *p > 0 ?
                    std::optional<double>(bounded_echo_gain/bounded_direct_gain) : std::nullopt;
                diagnostic.minimum_echo_ratio = diagnostic.maximum_echo_ratio = ratio;
            }
        }
        if (config.path_evolution.common_doppler_wander_hz > 0) {
            const auto seed = root_seed(config.path_seed,effects.seed)^0x434f4d4d4f4e4652ULL;
            common_doppler_wander = std::make_unique<detail::BoundedRandomSpread>(
                0, config.path_evolution.common_doppler_wander_hz,
                config.path_evolution.common_doppler_knot_seconds, seed);
            auto& diagnostic = stats.common_doppler_wander.emplace();
            diagnostic.bound_hz = config.path_evolution.common_doppler_wander_hz;
            diagnostic.knot_seconds = config.path_evolution.common_doppler_knot_seconds;
            diagnostic.seed = seed;
        }
        if (perturb_enabled) {
            const auto seed = effects.seed ^ 0x3254494154504552ULL;
            auto& diagnostic = stats.selective_perturbation.emplace();
            diagnostic.coefficient_law = config.selective_model.coefficient_law;
            diagnostic.sample_rate_hz = effects.sample_rate * rx_scale;
            diagnostic.component_depths = {effects.perturbation_slow_depth, effects.perturbation_fast_depth};
            dispersive_perturbation = std::make_unique<DispersiveSelectivePerturbation>(
                effects.sample_rate * rx_scale, config.selective_model.center_hz, selective_spacing,
                selective_rate, effects.perturbation_slow_depth,
                effects.perturbation_fast_depth, seed, random_selective);
            diagnostic.correlation_spacing_hz = selective_spacing;
            diagnostic.center_hz = config.selective_model.center_hz;
            diagnostic.response_period_hz = dispersive_perturbation->response_period_hz;
            diagnostic.branch_delays_seconds = dispersive_perturbation->branch_delays_seconds;
            diagnostic.memory_samples = DispersiveSelectivePerturbation::memory_samples(
                effects.sample_rate * rx_scale, selective_spacing);
            diagnostic.band_width_hz = dispersive_perturbation->band_width_hz;
            diagnostic.component_rates_hz = dispersive_perturbation->rates_hz;
            diagnostic.band_centers_hz = dispersive_perturbation->band_centers_hz;
            if(!random_selective) diagnostic.initial_phases_radians = dispersive_perturbation->initial_phases;
            diagnostic.seed = seed;
            diagnostic.decay_tail_samples = actual_perturb_tail;
        }
        // A staged oscillator takes its initial flicker draws from its stream.
        if (staged[transmit_staged].active)
            initialize_spectrum(tx_osc, config.tx_spectrum, [this] { return staged[transmit_staged].stream.next(); });
        else initialize_spectrum(tx_osc, config.tx_spectrum, [this] { return tx_spectrum_rng.gaussian(); });
        if (staged[receive_staged].active)
            initialize_spectrum(rx_osc, config.rx_spectrum, [this] { return staged[receive_staged].stream.next(); });
        else initialize_spectrum(rx_osc, config.rx_spectrum, [this] { return rx_spectrum_rng.gaussian(); });
        for (auto& chain : staged) chain.offset = chain.stream.taken();
        prepare_phase_step(tx_osc, effects.tx, config.tx_spectrum, effects.nominal_conversion_if_hz, tx_scale, dt_tx);
        prepare_phase_step(rx_osc, effects.rx, config.rx_spectrum, effects.nominal_conversion_if_hz, rx_scale, dt_rx);
        // Without retuning both rays keep the phase of equal hop calendars.
        still_hop_rotation = std::polar(1.0, tau * std::remainder(static_cast<double>(0.L - 0.L), 1.0));
        // The echo's carrier rotation follows its excess delay, which only
        // the moving-delay law changes from row to row.
        if (bounded_spread) {
            fixed_delay_seconds = static_cast<long double>(bounded_spread->excess_delay_seconds());
            fixed_delay_rotation = delay_rotation(fixed_delay_seconds);
        } else {
            // Keep legacy off-path multiplication ordering exactly unchanged.
            fixed_delay_seconds = static_cast<long double>(effects.spread_delay_ms)*.001L;
            fixed_delay_rotation = std::polar(1.0, -tau * std::remainder(effects.rf_hz *
                effects.spread_delay_ms * .001, 1.0));
        }
        fixed_delay_samples = fixed_delay_seconds * tx_scale * effects.sample_rate;
        for (std::size_t job = 1; job < lane_jobs; ++job) {
            auto& reader = readers.emplace_back();
            if (reads_rays) reader.rays = std::make_unique<std::array<detail::FixedGaussianRay, 2>>(
                std::array{*random_path[0], *random_path[1]});
            if (reads_excursions) reader.excursions.emplace(dispersive_perturbation->random_excursions());
        }
        if (config.path_evolution.ray_coherence_seconds > 0) {
            path_gain_decay = std::exp(-dt_rx/config.path_evolution.ray_coherence_seconds);
            path_gain_innovation = std::sqrt(-std::expm1(-2*dt_rx/config.path_evolution.ray_coherence_seconds)*.5);
            constexpr double scale = .7071067811865475244;
            const double direct_real = path_rng.gaussian(), direct_imag = path_rng.gaussian();
            const double delayed_real = path_rng.gaussian(), delayed_imag = path_rng.gaussian();
            direct_gain = scale*Complex{direct_real,direct_imag};
            delayed_gain = scale*Complex{delayed_real,delayed_imag};
        }
        if (config.path_evolution.diurnal_phase_amplitude_radians > 0)
            stats.path_diurnal_initial_phase_radians = keyed_phase(root_seed(config.path_seed,effects.seed), 0,
                                                                  0x5041544844495552ULL);
        hop_prefix.assign(prefix_count, 0);
        for (std::size_t i = 0; effects.fhss && i < effects.hop_offsets_hz.size(); ++i)
            hop_prefix[i + 1] = hop_prefix[i] + effects.hop_offsets_hz[i];
        agc_power = config.complex_noise_variance > 0 ? config.complex_noise_variance : 1;
        agc_alpha = std::exp(-dt_rx / .75);
        if (config.frontend.overload_recovery_seconds > 0)
            recovery_alpha = std::exp(-dt_rx/config.frontend.overload_recovery_seconds);
        tone_initial_phase = keyed_phase(root_seed(config.frontend_seed,effects.seed), 0, 0x544f4e4546524f4eULL);
        stats.complex_noise_variance = effects.noise_enabled ? config.complex_noise_variance : 0;
        stats.gaussian_projection_supported = !transmit_diagnostic_iq_noise && !config.frontend.active() && !effects.slow_agc;
        stats.source_samples = geometry.source_samples;
        stats.tail_samples = tail_samples;
    }

    void generate_tx_until(std::uint64_t last) {
        const auto max_end = geometry.source_samples;
        if (last >= max_end) last = max_end == 0 ? 0 : max_end - 1;
        if (geometry.source_samples == 0 || tx_next > last) return;
        while (tx_next <= last) {
            advance_transmit_phase(tx_next, [this] { return tx_spectrum_rng.gaussian(); });
            auto value = effects.signal_enabled ? source_sample(tx_next) : Complex{};
            const double white_pm = transmit_white_pm();
            if (effects.signal_enabled && transmit_diagnostic_iq_noise) {
                // Sample once in emitted-calendar order at the narrow
                // complex-I/Q diagnostic source. Quantized noise is added in code space so
                // sub-LSB requests remain visible; fixed TX gain then scales
                // the signal and noise together.
                if (config.frontend.dac.nominal_bits || config.frontend.dac.gain != 1)
                    value = convert(value, config.frontend.dac, dac_rng, stats.dac_clipped_components);
                Complex noise{};
                const auto& dac = config.frontend.dac;
                if (dac.nominal_bits) {
                    const double step = std::ldexp(dac.full_scale, 1-static_cast<int>(dac.nominal_bits));
                    const double component_variance = transmit_diagnostic_iq_noise->variance() * .5;
                    const auto max_codes = static_cast<std::int64_t>(std::ceil(
                        (std::sqrt(1 + 12*component_variance/(step*step)) - 1) * .5));
                    const double support = step * static_cast<double>(max_codes);
                    const double positive_limit = dac.full_scale-step;
                    require(value.real()+support <= positive_limit && value.real()-support >= -dac.full_scale &&
                            value.imag()+support <= positive_limit && value.imag()-support >= -dac.full_scale,
                            "cryptographic diagnostic-IQ noise exceeds DAC headroom; reduce its level or reserve fixed TX gain");
                    noise = {step*static_cast<double>(transmit_diagnostic_iq_noise->quantized_noise(step, component_variance)),
                             step*static_cast<double>(transmit_diagnostic_iq_noise->quantized_noise(step, component_variance))};
                } else {
                    noise = transmit_diagnostic_iq_noise->complex();
                }
                value += noise;
                stats.diagnostic_iq_noise_source_energy += std::norm(noise) *
                    config.signal_gain * config.signal_gain;
                ++stats.diagnostic_iq_noise_source_samples;
                value *= config.signal_gain * std::polar(1.0, tx_osc.phase + white_pm);
            } else {
                if (effects.signal_enabled && (config.frontend.dac.nominal_bits || config.frontend.dac.gain != 1))
                    value = convert(value, config.frontend.dac, dac_rng, stats.dac_clipped_components);
                value *= config.signal_gain * std::polar(1.0, tx_osc.phase + white_pm);
            }
            tx_history[static_cast<std::size_t>(tx_next % tx_history.size())] = value;
            ++tx_next;
        }
    }

    // The TX oscillator's phase at source sample n from its draws in order.
    // It advances the oscillator one sample; the fused loop then reads the
    // source, and both paths draw the white phase modulation of the sample.
    template<class Gaussian>
    void advance_transmit_phase(std::uint64_t n, Gaussian&& gaussian) {
        const double t = static_cast<double>(n) * dt_tx;
        double hop = 0;
        // Without FHSS the dwell index only checked a range that the bound
        // on the source's calendar already guarantees.
        if (effects.fhss || !tx_calendar_bounded) {
            const double calendar_time = static_cast<double>(static_cast<long double>(n) /
                tx_clock - hop_origin_seconds);
            hop = hop_at(effects, calendar_time);
            const auto current_dwell = dwell_index(calendar_time, effects.dwell_seconds);
            if (effects.fhss && current_dwell != previous_tx_dwell) {
                tx_osc.phase += keyed_phase(root_seed(config.oscillator_seed,effects.seed), current_dwell,
                                            0x5458484f50504841ULL);
                previous_tx_dwell = current_dwell;
            }
        }
        const double rf = effects.rf_hz + hop;
        tx_osc.phase += oscillator_phase_step(tx_osc, effects.tx, gaussian, rf, t, dt_tx);
        // Keep the historical positive-RF arithmetic unchanged when this
        // supplemental emission-time slope is off.
        if (config.tx_frequency_drift_hz_per_second != 0)
            tx_osc.phase += tau * config.tx_frequency_drift_hz_per_second * t * dt_tx;
        tx_osc.phase = std::remainder(tx_osc.phase, tau);
    }
    double transmit_white_pm() {
        return config.tx_spectrum.white_pm_rms_radians * tx_spectrum_rng.gaussian();
    }

    // A staged chain's items, rows or source samples, below items and
    // without overflowing its draw indices.
    void open_staged(Staged which, bool recurrence, std::uint64_t lead, std::uint64_t per_item, std::uint64_t items) {
        auto& chain = staged[which];
        chain.active = true;
        chain.recurrence = recurrence;
        chain.lead = lead;
        chain.per_item = per_item;
        chain.items = std::min(items, (UINT64_MAX / 4) / per_item);
        chain.stream.open(static_cast<std::size_t>(DrawChain::draw_capacity(lead, per_item)),
                          static_cast<std::size_t>(DrawChain::pair_capacity(lead, per_item)));
        if (!recurrence) return;
        chain.phase.resize(static_cast<std::size_t>(3 * lead));
        chain.white_pm.resize(chain.phase.size());
        chain.value.resize(chain.phase.size());
        if (which == receive_staged) chain.dwell.resize(chain.phase.size());
    }
    // The recurrence of a staged oscillator through its items [phase_from,
    // phase_to): the operations of the unbatched reader in its order, with
    // each item's draws taken from the stream in order. It stops at the first
    // item that fails and records it for the consumer.
    void recur(Staged which, ChainRound& round) noexcept {
        auto& chain = staged[which];
        round.phase_ran = true;
        auto n = round.phase_from;
        try {
            for (; n < round.phase_to; ++n) {
                auto j = chain.draw(n);
                const auto gaussian = [&chain, &j] { return chain.stream[j++]; };
                const auto slot = static_cast<std::size_t>(n % chain.phase.size());
                if (which == receive_staged) {
                    const auto physical_time = physical_time_at(n);
                    const double receive_calendar = static_cast<double>(physical_time +
                        effects.retune_offset_seconds - hop_origin_seconds);
                    const double rx_hop = effects.fhss ? hop_at(effects, receive_calendar) : 0;
                    const auto rx_dwell = dwell_index(receive_calendar, effects.dwell_seconds);
                    chain.dwell[slot] = rx_dwell;
                    chain.white_pm[slot] = advance_receive(static_cast<double>(physical_time), rx_hop, rx_dwell, gaussian);
                    chain.phase[slot] = rx_osc.phase;
                } else {
                    advance_transmit_phase(n, gaussian);
                    chain.white_pm[slot] = config.tx_spectrum.white_pm_rms_radians * gaussian();
                    chain.phase[slot] = tx_osc.phase;
                }
            }
        } catch (...) {
            round.failure = n;
            round.error = std::current_exception();
        }
        round.phased = n;
    }
    // The recurrence's end after its job, on the reader.
    static void record_recurrence(DrawChain& chain, const ChainRound& round) {
        chain.phased = round.phased;
        if (round.error) {
            chain.failure = round.failure;
            chain.error = round.error;
        }
    }
    // The rotations of a staged oscillator's items [from, to), formed from
    // its recurrence as the unbatched reader forms them.
    void form(Staged which, std::uint64_t from, std::uint64_t to) noexcept {
        auto& chain = staged[which];
        const auto size = chain.value.size();
        for (auto n = from; n < to; ++n) {
            const auto slot = static_cast<std::size_t>(n % size);
            chain.value[slot] = which == receive_staged ?
                std::polar(1.0, -chain.phase[slot] - chain.white_pm[slot]) :
                config.signal_gain * std::polar(1.0, chain.phase[slot] + chain.white_pm[slot]);
        }
    }
    // Brings a staged chain on the reader to where a block's jobs need it:
    // an oscillator's recurrence through item need, or the noise's draws
    // through those of item need. Its consumer reads from item oldest on.
    void catch_up(DrawChain& chain, Staged which, std::uint64_t need, std::uint64_t oldest) {
        require(need <= chain.items, "streaming channel staged chain index overflow");
        auto& stream = chain.stream;
        for (;;) {
            const bool ready = chain.recurrence ? chain.failure != UINT64_MAX || chain.phased >= need :
                stream.finished() >= chain.draw(need);
            if (ready) return;
            const auto before = std::tuple(stream.accepted(), stream.finished(), chain.phased);
            const auto first_read = chain.draw(chain.recurrence ? chain.phased : oldest);
            stream.accept(std::min(stream.pair_end(chain.draw(need)), stream.accept_limit(first_read)));
            const auto finish_to = std::min(stream.accepted(), stream.finish_limit(first_read));
            stream.finish(stream.finished(), finish_to);
            stream.finished_through(finish_to);
            if (chain.recurrence) {
                ChainRound round;
                round.phase_from = chain.phased;
                round.phase_to = std::max(chain.phased, std::min({need, chain.drawn(),
                    std::min(oldest, chain.formed) + chain.phase.size()}));
                recur(which, round);
                record_recurrence(chain, round);
            }
            require(before != std::tuple(stream.accepted(), stream.finished(), chain.phased),
                    "streaming channel staged chain cannot advance");
        }
    }
    // The ranges of a staged chain for one block's jobs. The jobs form the
    // rotations of items [formed, need), which the chain must have carried
    // through its recurrence, and its consumer reads values or draws from
    // item oldest on. The other stages run as far ahead as the chain's lead
    // and rings allow.
    ChainRound plan_staged(Staged which, std::uint64_t need, std::uint64_t oldest) {
        auto& chain = staged[which];
        catch_up(chain, which, need, oldest);
        auto& stream = chain.stream;
        const auto ahead = [&](std::uint64_t leads) {
            return std::min(chain.items, need + std::min(chain.items, leads * chain.lead));
        };
        ChainRound round;
        if (chain.recurrence) {
            round.form_from = chain.formed;
            round.form_to = std::max(round.form_from, std::min(need, chain.phased));
            require(round.form_to - std::min(oldest, round.form_from) <= chain.value.size(),
                    "streaming channel staged chain outran its ring");
            round.phase_from = round.phase_to = round.phased = chain.phased;
            // The pass still reads the receiver's dwell indices from oldest.
            if (chain.failure == UINT64_MAX)
                round.phase_to = std::max(chain.phased, std::min({ahead(1), chain.drawn(),
                    std::min(oldest, round.form_from) + chain.phase.size()}));
        }
        const auto first_read = chain.draw(chain.recurrence ? round.phase_from : oldest);
        round.finish_from = stream.finished();
        round.finish_to = std::max(round.finish_from, std::min({stream.accepted(), stream.finish_limit(first_read),
            stream.pair_end(chain.draw(ahead(2)))}));
        round.accept_to = std::max(stream.accepted(),
            std::min(stream.pair_end(chain.draw(ahead(3))), stream.accept_limit(first_read)));
        return round;
    }
    // After a block's jobs: the chain's stages reached the planned ends.
    void advance_staged(Staged which, const ChainRound& round) {
        auto& chain = staged[which];
        require(chain.stream.accepted() >= round.accept_to && (round.phase_to == round.phase_from || round.phase_ran),
                "streaming channel lanes did not run every job");
        chain.stream.finished_through(round.finish_to);
        if (!chain.recurrence) return;
        record_recurrence(chain, round);
        chain.formed = round.form_to;
    }
    // Part job of jobs of the range, evaluated on that reader into the ring.
    void run_source(std::size_t job, std::size_t jobs, std::uint64_t first, std::uint64_t end) noexcept {
        auto& stage = source_stages[job];
        stage.ran = true;
        const auto count = end - first;
        auto n = first + count * job / jobs;
        const auto stop = first + count * (job + 1) / jobs;
        const auto size = tx_history.size();
        try {
            for (; n < stop; ++n)
                tx_history[static_cast<std::size_t>(n % size)] = source_readers->evaluate(job, n);
        } catch (...) {
            stage.index = n;
            stage.error = std::current_exception();
        }
    }
    std::size_t source_jobs(std::uint64_t count) const {
        if (!effects.signal_enabled) return 0;
        return std::clamp<std::size_t>(static_cast<std::size_t>(count / 160), 1, source_reader_count);
    }
    void reset_stages(std::size_t jobs) {
        transmit_formed = false;
        for (std::size_t job = 0; job < jobs; ++job) source_stages[job] = {};
    }
    // After the stages of [first, end) ran: the source accounts the samples
    // in order up to the first that the fused loop could not have generated,
    // and the ring receives value times factor for each generated sample.
    // Returns the failure of the next sample, if any.
    std::exception_ptr finish_transmit(std::uint64_t first, std::uint64_t end, std::size_t jobs) {
        require(transmit_formed, "streaming channel lanes did not run every job");
        Stage source;
        for (std::size_t job = 0; job < jobs; ++job) {
            require(source_stages[job].ran, "streaming channel lanes did not run every job");
            if (source_stages[job].index < source.index) source = source_stages[job];
        }
        // The fused loop advances the oscillator before it reads the source.
        // A staged recurrence that failed beyond this range has not been met.
        const auto& oscillator = staged[transmit_staged];
        const auto oscillator_index = oscillator.failure >= first && oscillator.failure < end ?
            oscillator.failure : UINT64_MAX;
        const bool oscillator_first = oscillator_index <= source.index;
        auto good = std::min(end, oscillator_first ? oscillator_index : source.index);
        auto error = oscillator_first ? (oscillator_index == UINT64_MAX ? nullptr : oscillator.error) : source.error;
        if (jobs) {
            std::exception_ptr check;
            const auto accepted = source_readers->accept(good - first, !oscillator_first, check);
            if (check) {
                good = first + accepted;
                error = check;
            }
        }
        const auto size = tx_history.size();
        for (auto n = first; n < good; ++n) {
            auto& slot = tx_history[static_cast<std::size_t>(n % size)];
            auto value = effects.signal_enabled ? slot : Complex{};
            value *= oscillator.at(n);
            slot = value;
        }
        tx_next = good;
        return error;
    }
    // The last source sample that generating through needed produces, or
    // nothing when the history already holds it.
    std::optional<std::uint64_t> source_end(std::uint64_t needed) const {
        if (geometry.source_samples == 0) return std::nullopt;
        const auto last = std::min(needed, geometry.source_samples - 1);
        if (tx_next > last) return std::nullopt;
        return last + 1;
    }
    // The split counterpart of generate_tx_until on the reader: the stages
    // of each part of at most one lookahead run as jobs of their own.
    void generate_split(std::uint64_t needed) {
        const auto end = source_end(needed);
        if (!end) return;
        auto& oscillator = staged[transmit_staged];
        while (tx_next < *end) {
            const auto first = tx_next;
            const auto stop = std::min<std::uint64_t>(*end, first + block_lookahead);
            const auto jobs = source_jobs(stop - first);
            reset_stages(jobs);
            if (jobs) source_readers->plan(first, stop - first);
            catch_up(oscillator, transmit_staged, stop, first);
            const auto formed = std::min(stop, oscillator.phased);
            const auto run = [&](std::size_t job) {
                if (job == 0) {
                    form(transmit_staged, first, formed);
                    transmit_formed = true;
                } else run_source(job - 1, jobs, first, stop);
            };
            if (jobs == 0 || stop - first < 64) for (std::size_t job = 0; job <= jobs; ++job) run(job);
            else config.lanes.run(jobs + 1, run);
            if (auto error = finish_transmit(first, stop, jobs)) std::rethrow_exception(error);
            oscillator.formed = tx_next;
        }
    }

    Complex delay_rotation(long double delay_seconds) const {
        const auto cycles=std::remainder((static_cast<long double>(effects.rf_hz)+
            effects.nominal_conversion_if_hz)*delay_seconds,1.L);
        return std::polar(1.0,-tau*static_cast<double>(cycles));
    }

    struct Path {
        long double delay_seconds = 0, direct = 0, delayed = 0;
        // The last source sample that the row's two interpolations read.
        std::uint64_t needed = 0;
    };
    // The echo delay and both source coordinates of the row received at
    // physical_time. They depend on the row's index alone.
    Path path_at(std::uint64_t n, long double physical_time) const {
        Path path;
        const bool moving = !bounded_spread && random_delay;
        if (moving) path.delay_seconds = static_cast<long double>(random_delay->value(physical_time));
        else path.delay_seconds = fixed_delay_seconds;
        // Preserve the exact integer lattice when the clocks/offset agree;
        // avoid a lossy double sample -> seconds -> sample round trip.
        path.direct = static_cast<long double>(n) * tx_per_rx - direct_origin;
        path.delayed = path.direct - (moving ? path.delay_seconds * tx_scale * effects.sample_rate :
                                               fixed_delay_samples);
        auto needed_last = [&](long double position) {
            require(std::isfinite(position), "stream channel source coordinate overflow");
            const auto last = geometry.source_samples - 1;
            if (position < 0) return std::uint64_t{0};
            const auto needed = floor_coordinate(position) + 8;
            return needed >= static_cast<long double>(last) ? last : unsigned_index(needed);
        };
        const auto direct_last = needed_last(path.direct);
        const auto delayed_last = needed_last(path.delayed);
        path.needed = std::max(direct_last, delayed_last);
        return path;
    }
    long double physical_time_at(std::uint64_t n) const {
        return static_cast<long double>(n) / rx_clock - effects.capture_lead_seconds;
    }
    // Whether the generated source history already covers a row's needs.
    bool source_ready(const Path& path) const {
        return geometry.source_samples == 0 || path.needed < tx_next;
    }

    // The receiver oscillator's phase at the next row from its draws in
    // order; returns the row's white phase modulation.
    template<class Gaussian>
    double advance_receive(double t, double rx_hop, std::int64_t rx_dwell, Gaussian&& gaussian) {
        if (effects.fhss && rx_dwell != previous_rx_dwell) {
            rx_osc.phase += keyed_phase(root_seed(config.oscillator_seed,effects.seed) ^ 0x5258484f50504841ULL,
                                        rx_dwell, 0x5258504841534531ULL);
            previous_rx_dwell = rx_dwell;
        }
        const double rf_error = (effects.rf_hz + rx_hop) *
            (effects.rx.rf_ppm + effects.rx.rf_drift_ppm_per_second * t) * 1e-6 -
            effects.static_doppler_hz;
        rx_osc.phase += oscillator_phase_step(rx_osc, effects.rx, gaussian, 0, t, dt_rx) +
            tau * rf_error * dt_rx;
        rx_osc.phase = std::remainder(rx_osc.phase, tau);
        return config.rx_spectrum.white_pm_rms_radians * gaussian();
    }
    // The receiver oscillator's rotation of the next row.
    Complex receive_rotation(double t, double rx_hop, std::int64_t rx_dwell) {
        const double white_pm = advance_receive(t, rx_hop, rx_dwell, [this] { return rx_spectrum_rng.gaussian(); });
        return std::polar(1.0, -rx_osc.phase - white_pm);
    }
    double noise_sigma() const { return std::sqrt(config.complex_noise_variance * .5); }
    Complex thermal_noise() {
        const double sigma = noise_sigma();
        return Complex{sigma * thermal_rng.gaussian(), sigma * thermal_rng.gaussian()};
    }
    // The staged thermal noise of row n, from the same draws.
    Complex staged_thermal(std::uint64_t n) const {
        const auto& chain = staged[thermal_staged];
        const double sigma = noise_sigma();
        const auto j = chain.draw(n);
        return Complex{sigma * chain.stream[j], sigma * chain.stream[j + 1]};
    }

    // One sequential state through rows [0, rows) of the block, or the source
    // history of the block after it. Each touches its own state alone.
    void run_chain(Chain chain, std::size_t rows, std::uint64_t ahead_needed) noexcept {
        auto& block = blocks[prepare_slot];
        auto& chains = chain_sets[prepare_slot];
        auto& failure = chains.failures[chain];
        failure.ran = true;
        std::size_t row = 0;
        try {
            if (chain == ahead_chain) {
                generate_tx_until(ahead_needed);
                return;
            }
            for (; row < rows; ++row) {
                const auto physical_time = physical_time_at(block.first + row);
                if (chain == echo_chain) {
                    chains.echo[row] = bounded_spread->at(physical_time);
                    chains.echo_rotation[row] = std::polar(1., chains.echo[row].phase_radians);
                } else {
                    const auto state = common_doppler_wander->at(physical_time);
                    chains.common_doppler_hz[row] = state.doppler_hz;
                    chains.common_rotation[row] = std::polar(1., state.phase_radians);
                }
            }
        } catch (...) {
            failure = {row, false, true, std::current_exception()};
        }
    }
    static void chain_reached(const Chains& chains, Chain chain, std::size_t row) {
        if (row == chains.failures[chain].row) std::rethrow_exception(chains.failures[chain].error);
    }

    // Rows [begin, end) of the block on one pool job. A row reads the
    // history its reader retained when the unbatched reader reached it, then
    // the fixed-time values of the selected laws at its physical time. A job
    // keeps the first row of each kind of failure among the runs it claims;
    // runs are claimed in order, so every earlier row is prepared.
    void prepare_rows(std::size_t job, std::size_t begin, std::size_t end) noexcept {
        auto& block = blocks[prepare_slot];
        auto& failure = job_failures[job];
        auto* reader = job ? &readers[job - 1] : nullptr;
        // Only the bounded and the two-ray laws read the delayed ray.
        const bool delayed_read = spread || bounded_spread;
        std::size_t row = begin;
        bool in_path = false;
        try {
            for (; row < end; ++row) {
                const auto n = block.first + row;
                const auto physical_time = physical_time_at(n);
                if (!block.physical_time.empty()) block.physical_time[row] = physical_time;
                in_path = false;
                // The sequential pass repeats this row's coordinates and
                // reports their failure itself, before it reads the row.
                Path path;
                try { path = path_at(n, physical_time); } catch (...) {
                    failure.path_row = std::min(failure.path_row, row);
                    return;
                }
                if (!block.delay_seconds.empty()) block.delay_seconds[row] = path.delay_seconds;
                const auto stop = geometry.source_samples ? std::max(block.source_begin, path.needed + 1) : 0;
                const auto first = stop > retained_history ? stop - retained_history : 0;
                block.direct[row] = interpolate(tx_history, first, stop, path.direct);
                block.delayed[row] = delayed_read ? interpolate(tx_history, first, stop, path.delayed) : Complex{};
                if (!block.path_gain.empty()) {
                    in_path = true;
                    auto& direct = reader ? (*reader->rays)[0] : *random_path[0];
                    auto& delayed = reader ? (*reader->rays)[1] : *random_path[1];
                    const auto direct_gain = direct.value(physical_time);
                    block.path_gain[row] = {direct_gain, delayed.value(physical_time)};
                    in_path = false;
                }
                if (!block.excursion.empty())
                    block.excursion[row] = reader ? (*reader->excursions)(physical_time) :
                        dispersive_perturbation->random_excursions_at(physical_time);
                if (!block.branches.empty()) block.branches[row] = dispersive_perturbation->delayed_branches(n);
            }
        } catch (...) {
            if (row < failure.row) {
                failure.row = row;
                failure.in_path = in_path;
                failure.error = std::current_exception();
            }
        }
    }
    // One pool job: runs of rows while any remain, then the chunks of the
    // staged chains' formations.
    void run_pool(Pool& pool, std::size_t job) noexcept {
        job_failures[job].ran = true;
        for (std::size_t begin = 0, end = 0; pool.claim_rows(begin, end);) prepare_rows(job, begin, end);
        for (;;) {
            const auto chunk = pool.next_chunk.fetch_add(1, std::memory_order_relaxed);
            if (chunk >= pool.chunks) return;
            std::size_t segment = 0;
            while (segment + 1 < pool.segment_count && pool.segments[segment + 1].first_chunk <= chunk) ++segment;
            const auto& part = pool.segments[segment];
            const auto grain = part.values ? Pool::item_grain : Pool::draw_grain;
            const auto from = part.from + (chunk - part.first_chunk) * grain;
            const auto to = std::min(part.to, from + grain);
            if (part.values) form(part.chain, from, to);
            else staged[part.chain].stream.finish(from, to);
        }
    }

    // Generates the source history of the rows from first and evaluates
    // their index-pure terms. following is the size of the block this read
    // holds after it. A pending pass over the block before runs as one of
    // its jobs. Returns the number of rows the block holds.
    std::size_t prepare(std::uint64_t first, std::size_t wanted, std::size_t following, Pass* pass) {
        auto& block = blocks[prepare_slot];
        auto& chains = chain_sets[prepare_slot];
        block.first = first;
        block.source_begin = reader_source_end;
        block.source_failure_row = SIZE_MAX;
        block.source_failure = nullptr;
        block.failure = {};
        block.carried = 0;
        // The last row needs the most source history. Shorten a block whose
        // advance would overwrite samples that its first rows still read.
        auto rows = wanted;
        std::optional<Path> last;
        for (;; rows = (rows + 1) / 2) {
            try {
                last = path_at(first + rows - 1, physical_time_at(first + rows - 1));
            } catch (...) {
                // A single row that cannot be placed reports that when the
                // sequential pass reaches it; nothing is prepared for it.
                if (rows == 1) {
                    // Its echo state precedes its coordinates, as in a row
                    // that the reader evaluates alone.
                    for (auto& failure : chains.failures) failure = {};
                    if (chained && bounded_spread) run_chain(echo_chain, 1, 0);
                    return 1;
                }
                continue;
            }
            if (rows == 1 || last->needed < reader_source_end ||
                last->needed - reader_source_end < block_lookahead) break;
        }
        auto ready = rows;
        // Generating the source accounts it. The pass before then runs
        // first, as a failure there would have stopped the unbatched reader.
        if (pass && !pass->ran && !ahead_failure && source_end(last->needed)) {
            run_pass(*pass);
            if (pass->error) std::rethrow_exception(pass->error);
        }
        try {
            if (!ahead_failure) {
                if (split_transmit) generate_split(last->needed);
                else generate_tx_until(last->needed);
            } else if (!source_ready(*last)) std::rethrow_exception(std::exchange(ahead_failure, nullptr));
        } catch (...) {
            // The reader met this failure at the first row that needs the
            // sample. Earlier rows remain complete and are delivered first.
            block.source_failure = std::current_exception();
            for (ready = 0; ready < rows; ++ready) {
                try {
                    if (!source_ready(path_at(first + ready, physical_time_at(first + ready)))) break;
                } catch (...) { break; }
            }
            if (ready < rows) block.source_failure_row = ready;
            rows = std::min(rows, ready + 1);
        }
        reader_source_end = std::max(reader_source_end, std::min(last->needed + 1, tx_next));

        // The jobs of this block: the sequential states that run ahead, then
        // the pool of its rows and the staged chains' formations. The
        // longest come first so that they start first.
        enum class Kind : std::uint8_t { pass, chain, recurrence, accept, source, pool };
        struct Job { Kind kind; std::size_t index; };
        std::array<Job, 1 + chain_count + 2 * staged_count + kSourceReaders + 16> sequence{};
        std::size_t jobs = 0, source_count = 0;
        if (pass && !pass->ran) sequence[jobs++] = {Kind::pass, 0};
        std::uint64_t ahead_needed = 0, ahead_first = 0, ahead_end = 0;
        bool split_ahead = false;
        for (auto& failure : chains.failures) failure = {};
        std::array<ChainRound, staged_count> rounds{};
        Pool pool;
        if (chained) {
            if (following && !block.source_failure && !ahead_failure) {
                try {
                    const auto n = first + rows + following - 1;
                    ahead_needed = path_at(n, physical_time_at(n)).needed;
                    if (ahead_needed >= tx_next && ahead_needed - reader_source_end < block_lookahead) {
                        const auto end = split_transmit ? source_end(ahead_needed) : std::nullopt;
                        if (!split_transmit) sequence[jobs++] = {Kind::chain, ahead_chain};
                        else if (end && *end - tx_next <= block_lookahead) {
                            split_ahead = true;
                            ahead_first = tx_next;
                            ahead_end = *end;
                        }
                    }
                } catch (...) {}
            }
            if (split_ahead) {
                source_count = source_jobs(ahead_end - ahead_first);
                reset_stages(source_count);
                if (source_count) source_readers->plan(ahead_first, ahead_end - ahead_first);
            }
            if (bounded_spread) sequence[jobs++] = {Kind::chain, echo_chain};
            if (common_doppler_wander) sequence[jobs++] = {Kind::chain, common_chain};
            // The rows' chains form this block's values while its pass
            // waits; the transmit oscillator forms the range generated ahead.
            for (std::size_t which = 0; which < staged_count; ++which) {
                auto& chain = staged[which];
                if (!chain.active) continue;
                const bool transmit = which == transmit_staged;
                if (transmit) chain.formed = tx_next;
                auto& round = rounds[which] = plan_staged(static_cast<Staged>(which),
                    transmit ? (split_ahead ? ahead_end : tx_next) : first + rows, transmit ? tx_next : cursor);
                if (round.phase_to > round.phase_from) sequence[jobs++] = {Kind::recurrence, which};
                if (round.accept_to > chain.stream.accepted()) sequence[jobs++] = {Kind::accept, which};
                pool.add(static_cast<Staged>(which), false, round.finish_from, round.finish_to);
                if (chain.recurrence) pool.add(static_cast<Staged>(which), true, round.form_from, round.form_to);
            }
            for (std::size_t job = 0; job < source_count; ++job) sequence[jobs++] = {Kind::source, job};
        }
        pool.rows = ready;
        pool.jobs = std::clamp<std::size_t>(ready / 64 + pool.chunks / 2, 1, lane_jobs);
        for (std::size_t job = 0; job < pool.jobs; ++job) {
            job_failures[job] = {};
            sequence[jobs++] = {Kind::pool, job};
        }
        const auto run = [&](std::size_t job) {
            const auto [kind, index] = sequence[job];
            switch (kind) {
            case Kind::pass: run_pass(*pass); break;
            case Kind::chain: run_chain(static_cast<Chain>(index), rows, ahead_needed); break;
            case Kind::recurrence: recur(static_cast<Staged>(index), rounds[index]); break;
            case Kind::accept: staged[index].stream.accept(rounds[index].accept_to); break;
            case Kind::source: run_source(index, source_count, ahead_first, ahead_end); break;
            case Kind::pool: run_pool(pool, index); break;
            }
        };
        // A short block does not repay handing its jobs to other workers.
        try {
            if (jobs == 1 || rows < 64) for (std::size_t job = 0; job < jobs; ++job) run(job);
            else config.lanes.run(jobs, run);
        } catch (...) {
            if (pass && pass->error) std::rethrow_exception(pass->error);
            throw;
        }
        // A failed pass ends the read before this block's source is accounted.
        if (pass) {
            require(pass->ran, "streaming channel lanes did not run every job");
            if (pass->error) std::rethrow_exception(pass->error);
        }
        block.carried = ready;
        for (std::size_t job = 0; job < jobs; ++job) {
            const auto [kind, index] = sequence[job];
            if (kind == Kind::chain) require(chains.failures[index].ran, "streaming channel lanes did not run every job");
        }
        for (std::size_t job = 0; job < pool.jobs; ++job) {
            require(job_failures[job].ran, "streaming channel lanes did not run every job");
            if (job_failures[job].error && job_failures[job].row < block.failure.row)
                block.failure = job_failures[job];
            block.carried = std::min(block.carried, job_failures[job].path_row);
        }
        for (std::size_t which = 0; which < staged_count; ++which)
            if (staged[which].active) advance_staged(static_cast<Staged>(which), rounds[which]);
        if (split_ahead) {
            transmit_formed = true;
            if (auto failure = finish_transmit(ahead_first, ahead_end, source_count)) ahead_failure = failure;
            staged[transmit_staged].formed = tx_next;
        } else if (auto failure = std::exchange(chains.failures[ahead_chain].error, nullptr)) ahead_failure = failure;
        return rows;
    }

    Complex process_one(std::uint64_t n, std::size_t row, bool& erased, std::size_t slot) {
        const auto& block = blocks[slot];
        const auto& chains = chain_sets[slot];
        // A prepared row carries its physical time.
        const long double physical_time = row < block.carried && !block.physical_time.empty() ?
            block.physical_time[row] : physical_time_at(n);
        const double t = static_cast<double>(physical_time);
        if (chained && bounded_spread) chain_reached(chains, echo_chain, row);
        const auto bounded_state = !bounded_spread ? std::nullopt :
            std::optional<detail::BoundedRandomSpread::State>(chained ? chains.echo[row] :
                                                              bounded_spread->at(physical_time));
        // A prepared row carries its echo delay; another evaluates its
        // coordinates here, which reports their failure at this row.
        const long double delay_seconds = row < block.carried ?
            (block.delay_seconds.empty() ? fixed_delay_seconds : block.delay_seconds[row]) :
            path_at(n, physical_time).delay_seconds;
        if (row == block.source_failure_row) std::rethrow_exception(block.source_failure);
        Complex direct = block.direct[row];
        Complex delayed = block.delayed[row];

        // A shared public nominal origin fixes padding alignment without
        // recentering the receiver from simulated TX arrival/clock truth.
        const double receive_calendar = static_cast<double>(physical_time +
            effects.retune_offset_seconds - hop_origin_seconds);
        double rx_hop = 0;
        if (effects.fhss) {
            const double direct_calendar = static_cast<double>(physical_time -
                effects.timing_offset_seconds - hop_origin_seconds);
            const double delayed_calendar = static_cast<double>(physical_time -
                effects.timing_offset_seconds - delay_seconds - hop_origin_seconds);
            rx_hop = hop_at(effects, receive_calendar);
            const double direct_hop = hop_at(effects, direct_calendar);
            const double delayed_hop = hop_at(effects, delayed_calendar);
            const double limit = effects.occupied_bandwidth_hz * .5;
            bool out_of_band = false;
            if (std::abs(direct_hop - rx_hop) > limit) { direct = {}; out_of_band = true; }
            if (std::abs(delayed_hop - rx_hop) > limit) { delayed = {}; out_of_band = true; }
            if (out_of_band) ++stats.out_of_passband_samples;
            const auto receive_cycles = hop_cycles(effects, receive_calendar, hop_prefix);
            const auto direct_phase = tau * std::remainder(static_cast<double>(
                hop_cycles(effects, direct_calendar, hop_prefix) - receive_cycles), 1.0);
            const auto delayed_phase = tau * std::remainder(static_cast<double>(
                hop_cycles(effects, delayed_calendar, hop_prefix) - receive_cycles), 1.0);
            direct *= std::polar(1.0, direct_phase);
            delayed *= std::polar(1.0, delayed_phase);
        } else {
            // Every hop is zero: nothing leaves the passband and both rays
            // take the rotation of two equal hop calendars.
            direct *= still_hop_rotation;
            delayed *= still_hop_rotation;
        }
        delayed *= !bounded_state && random_delay ? delay_rotation(delay_seconds) : fixed_delay_rotation;
        if (config.path_evolution.ray_coherence_seconds > 0) {
            direct *= direct_gain;
            delayed *= delayed_gain;
            // Use stationary initial gains on the first row, then advance by
            // the exact OU endpoint transition for the physical RX sample dt.
            // Independent draws and updates persist even through source silence.
            const double direct_real = path_rng.gaussian(), direct_imag = path_rng.gaussian();
            const double delayed_real = path_rng.gaussian(), delayed_imag = path_rng.gaussian();
            direct_gain = path_gain_decay*direct_gain + path_gain_innovation*Complex{direct_real,direct_imag};
            delayed_gain = path_gain_decay*delayed_gain + path_gain_innovation*Complex{delayed_real,delayed_imag};
        }
        if (gaussian_path) {
            // Every physical RX row advances both stationary histories, even
            // source silence, hop guards, outages and disabled signal controls.
            const auto gains = gaussian_path->next();
            direct *= gains[0];
            delayed *= gains[1];
        }
        if(random_path[0]) {
            if (row == block.failure.row && block.failure.in_path) std::rethrow_exception(block.failure.error);
            direct*=block.path_gain[row][0];
            delayed*=block.path_gain[row][1];
        }
        Complex propagated;
        if (bounded_state) {
            if (config.path_evolution.bounded_spread_direct_power_fraction) {
                direct *= bounded_direct_gain;
                delayed *= bounded_echo_gain;
            } else {
                // Preserve the original default multiplication order exactly.
                direct *= bounded_state->direct_gain;
                delayed *= bounded_state->echo_gain;
            }
            delayed *= chained ? chains.echo_rotation[row] : std::polar(1., bounded_state->phase_radians);
            propagated = direct + delayed;
            auto& diagnostic = *stats.bounded_spread;
            if (!diagnostic.fixed_direct_power_fraction) {
                diagnostic.minimum_echo_ratio = std::min(*diagnostic.minimum_echo_ratio,bounded_state->echo_ratio);
                diagnostic.maximum_echo_ratio = std::max(*diagnostic.maximum_echo_ratio,bounded_state->echo_ratio);
            }
            if (diagnostic.processed_rows == 0)
                diagnostic.minimum_doppler_hz = diagnostic.maximum_doppler_hz = bounded_state->doppler_hz;
            else {
                diagnostic.minimum_doppler_hz = std::min(diagnostic.minimum_doppler_hz,bounded_state->doppler_hz);
                diagnostic.maximum_doppler_hz = std::max(diagnostic.maximum_doppler_hz,bounded_state->doppler_hz);
            }
            ++diagnostic.processed_rows;
        } else {
            propagated = spread ? spread->process(direct, delayed) : direct;
        }
        double common_doppler_hz = 0;
        if (common_doppler_wander) {
            if (chained) {
                chain_reached(chains, common_chain, row);
                common_doppler_hz = chains.common_doppler_hz[row];
                propagated *= chains.common_rotation[row];
            } else {
                const auto state = common_doppler_wander->at(physical_time);
                common_doppler_hz = state.doppler_hz;
                propagated *= std::polar(1., state.phase_radians);
            }
            auto& diagnostic = *stats.common_doppler_wander;
            if (diagnostic.processed_rows == 0)
                diagnostic.minimum_hz = diagnostic.maximum_hz = common_doppler_hz;
            else {
                diagnostic.minimum_hz = std::min(diagnostic.minimum_hz,common_doppler_hz);
                diagnostic.maximum_hz = std::max(diagnostic.maximum_hz,common_doppler_hz);
            }
            ++diagnostic.processed_rows;
        }
        if (bounded_state) {
            // Both states already advanced at this exact physical sample.
            // This is a simultaneous sum, not a sum of separate extrema.
            const auto combined = bounded_state->doppler_hz + common_doppler_hz;
            auto& diagnostic = *stats.bounded_spread;
            if (diagnostic.processed_rows == 1)
                diagnostic.minimum_combined_physical_doppler_hz = diagnostic.maximum_combined_physical_doppler_hz = combined;
            else {
                diagnostic.minimum_combined_physical_doppler_hz = std::min(diagnostic.minimum_combined_physical_doppler_hz,combined);
                diagnostic.maximum_combined_physical_doppler_hz = std::max(diagnostic.maximum_combined_physical_doppler_hz,combined);
            }
        }
        const auto& path = config.path_evolution;
        if (path.diurnal_phase_amplitude_radians > 0) {
            const auto cycles = std::remainder(physical_time/path.diurnal_period_seconds, 1.L);
            const double angle = path.diurnal_phase_amplitude_radians*std::sin(
                tau*static_cast<double>(cycles)+stats.path_diurnal_initial_phase_radians);
            propagated *= std::polar(1., angle);
        }
        if (path.outage_start_seconds >= 0) propagated *= outage_gain(path, physical_time);
        if (dispersive_perturbation) {
            auto& diagnostic = *stats.selective_perturbation;
            diagnostic.pre_selective_energy_samples += std::norm(propagated);
            if (!block.excursion.empty() && row == block.failure.row) std::rethrow_exception(block.failure.error);
            if (!block.branches.empty() && row < block.carried)
                propagated = dispersive_perturbation->process(propagated, block.branches[row],
                    block.excursion.empty() ? nullptr : &block.excursion[row]);
            else if (block.excursion.empty()) propagated = dispersive_perturbation->process(propagated,physical_time);
            else propagated = dispersive_perturbation->process(propagated,block.excursion[row]);
            diagnostic.post_selective_energy_samples += std::norm(propagated);
            ++diagnostic.processed_rows;
        }

        const auto& receiver = staged[receive_staged];
        if (chained && n == receiver.failure) std::rethrow_exception(receiver.error);
        // The staged receiver evaluated this row's dwell index, and would have
        // failed above where it overflows.
        const auto rx_dwell = chained ? receiver.dwell[static_cast<std::size_t>(n % receiver.dwell.size())] :
            dwell_index(receive_calendar, effects.dwell_seconds);
        propagated *= chained ? receiver.at(n) : receive_rotation(t, rx_hop, rx_dwell);
        stats.signal_energy += std::norm(propagated);
        if (effects.noise_enabled) propagated += chained ? staged_thermal(n) : thermal_noise();
        const auto& front = config.frontend;
        if (front.tone_amplitude > 0) {
            const auto d = dwell_index(t, effects.dwell_seconds);
            if (d >= 0 && static_cast<std::uint64_t>(d) % front.tone_every_n_dwells == 0) {
                const auto cycles = std::remainder(physical_time*front.tone_frequency_hz, 1.L);
                propagated += std::polar(front.tone_amplitude,
                    tone_initial_phase + tau*static_cast<double>(cycles));
            }
        }
        if (front.impulse_amplitude > 0 && front.impulse_rate_hz > 0 &&
                interference_rng.uniform() < -std::expm1(-front.impulse_rate_hz*dt_rx)) {
            propagated += std::polar(front.impulse_amplitude, tau*interference_rng.uniform());
            ++stats.impulse_events;
        }
        if (front.lowpass_hz > 0) {
            frontend_filter_state = frontend_filter_alpha*frontend_filter_state +
                (1-frontend_filter_alpha)*propagated;
            propagated = frontend_filter_state;
        }
        if (effects.slow_agc) {
            agc_power = agc_alpha * agc_power + (1 - agc_alpha) * std::norm(propagated);
            propagated *= std::clamp(1 / std::sqrt(std::max(agc_power, 1e-12)), .02, 50.0);
        }
        if (front.active()) {
            if (front.gain_step_seconds >= 0 && t >= front.gain_step_seconds)
                propagated *= front.gain_step_factor;
            if (front.overload_recovery_seconds > 0) {
                recovery_scale = 1-(1-recovery_scale)*recovery_alpha;
                propagated *= recovery_scale;
            }
            const double peak = front.adc.gain*std::max(std::abs(propagated.real()), std::abs(propagated.imag()));
            stats.maximum_adc_input_component = std::max(stats.maximum_adc_input_component, peak);
            if (front.adc.nominal_bits && peak >= front.adc.full_scale) {
                ++stats.overload_samples;
                if (front.overload_recovery_seconds > 0)
                    recovery_scale *= front.adc.full_scale/peak;
            }
            if (front.adc.nominal_bits || front.adc.gain != 1)
                propagated = convert(propagated, front.adc, adc_rng, stats.adc_clipped_components);
        }
        // Preserve the established envelope-only experiment coordinates;
        // actual retuning uses windows measured from the RF dwell calendar.
        const bool missed_dwell = rx_dwell >= 0 &&
            std::binary_search(config.missed_receiver_dwell_indices.begin(),
                               config.missed_receiver_dwell_indices.end(),
                               static_cast<std::uint64_t>(rx_dwell));
        erased = dropout_at(effects, effects.fhss ?
            static_cast<double>(physical_time - hop_origin_seconds) : t) || missed_dwell;
        if (erased) { propagated = {}; ++stats.erased_samples; }
        stats.observed_energy += std::norm(propagated);
        ++stats.samples;
        return propagated;
    }

    void run_pass(Pass& pass) noexcept {
        pass.ran = true;
        try {
            for (std::size_t row = 0; row < pass.rows; ++row) {
                bool erased = false;
                pass.output[pass.offset + row] = process_one(cursor++, row, erased, pass.slot);
                if (!pass.validity.empty()) pass.validity[pass.offset + row] = erased ? 0 : 1;
            }
        } catch (...) {
            pass.error = std::current_exception();
        }
    }
    // Whether the preparation recorded no failure that the pass would meet.
    bool clean(const Pass& pass) const {
        const auto& block = blocks[pass.slot];
        const auto receiver_failure = staged[receive_staged].failure;
        return !block.source_failure && !block.failure.error && block.carried >= pass.rows &&
            std::ranges::none_of(chain_sets[pass.slot].failures, [](const Failure& f) { return static_cast<bool>(f.error); }) &&
            (receiver_failure < block.first || receiver_failure - block.first >= pass.rows);
    }
    void finish(Pass& pass) {
        if (!pass.ran) run_pass(pass);
        if (pass.error) std::rethrow_exception(pass.error);
    }
    // Each block's pass runs as a job of the next block's preparation in
    // the same read; the last block's pass runs alone.
    void read_overlapped(std::span<Complex> output, std::span<std::uint8_t> validity, std::size_t count) {
        std::optional<Pass> pending;
        auto next = cursor;
        for (std::size_t i = 0; i < count;) {
            const auto wanted = std::min(block_rows, count - i);
            if (pending && !clean(*pending)) {
                finish(*pending);
                pending.reset();
            }
            prepare_slot = pending ? 1 - pending->slot : prepare_slot;
            const auto rows = prepare(next, wanted, std::min(block_rows, count - i - wanted),
                                      pending ? &*pending : nullptr);
            if (pending) finish(*pending);
            pending = Pass{output, validity, i, rows, prepare_slot, nullptr, false};
            next += rows;
            i += rows;
        }
        if (pending) finish(*pending);
    }

    std::size_t read(std::span<Complex> output, std::span<std::uint8_t> validity) {
        require(validity.empty() || validity.size() == output.size(),
                "stream channel validity size differs from output");
        require(output.size() <= config.maximum_chunk_samples,
                "stream channel read exceeds its configured chunk bound");
        if (ended) std::rethrow_exception(ended);
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(output.size(), total - cursor));
        try {
            if (split_transmit) read_overlapped(output, validity, count);
            else for (std::size_t i = 0; i < count;) {
                const auto wanted = std::min(block_rows, count - i);
                const auto rows = prepare(cursor, wanted, std::min(block_rows, count - i - wanted), nullptr);
                for (std::size_t row = 0; row < rows; ++row, ++i) {
                    bool erased = false;
                    output[i] = process_one(cursor++, row, erased, 0);
                    if (!validity.empty()) validity[i] = erased ? 0 : 1;
                }
            }
        } catch (...) {
            if (chained) ended = std::current_exception();
            throw;
        }
        if (!validity.empty()) std::fill(validity.begin() + static_cast<std::ptrdiff_t>(count), validity.end(), 0);
        return count;
    }
};

StreamingChannel::StreamingChannel(PatternSource& source, const StreamChannelConfig& cfg)
    : impl_(std::make_unique<Impl>(source, cfg)) {}
StreamingChannel::StreamingChannel(EmittedSource source, const StreamChannelConfig& cfg)
    : impl_(std::make_unique<Impl>(std::move(source), cfg)) {}
StreamingChannel::~StreamingChannel() = default;
StreamingChannel::StreamingChannel(StreamingChannel&&) noexcept = default;
StreamingChannel& StreamingChannel::operator=(StreamingChannel&&) noexcept = default;
std::uint64_t StreamingChannel::total_samples() const { return impl_->total; }
std::uint64_t StreamingChannel::position() const { return impl_->cursor; }
std::size_t StreamingChannel::read(std::span<Complex> output,
                                   std::span<std::uint8_t> validity) {
    return impl_->read(output, validity);
}
const StreamChannelReport& StreamingChannel::report() const { return impl_->stats; }
double StreamingChannel::path_gaussian_covariance(std::uint64_t lag_samples) const {
    return impl_->gaussian_path ? impl_->gaussian_path->covariance(lag_samples) : 0;
}

} // namespace datapump::triad::v3
