#include "datapump/triad_modem_v3.hpp"

#include "channel_perturbation.hpp"
#include "channel_spread.hpp"
#include "triad_gaussian_fading.hpp"
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
using modem::detail::FrequencySelectivePerturbation;
using modem::detail::DispersiveSelectivePerturbation;
using modem::detail::SpreadRay;
using Complex = std::complex<double>;
constexpr double tau = 2 * std::numbers::pi;
constexpr std::size_t kHistoryLimitBytes = 64ULL * 1024 * 1024;

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
    require(finite_nonnegative(p.ray_coherence_seconds) &&
            finite_nonnegative(p.gaussian_doppler_width_hz) &&
            finite_nonnegative(p.random_doppler_width_hz) &&
            finite_nonnegative(p.delay_excursion_seconds) &&
            std::isfinite(p.delay_knot_seconds) && p.delay_knot_seconds > 0 &&
            detail::SmoothRandomDelay::maximum_slope(p.delay_excursion_seconds,p.delay_knot_seconds) < 1 &&
            finite_nonnegative(p.diurnal_phase_amplitude_radians) &&
            std::isfinite(p.diurnal_period_seconds) && p.diurnal_period_seconds > 0 &&
            std::isfinite(p.outage_start_seconds) && std::isfinite(p.outage_end_seconds) &&
            std::isfinite(p.outage_gain) && p.outage_gain >= 0 && p.outage_gain <= 1 &&
            finite_nonnegative(p.outage_ramp_seconds), "invalid physical-path evolution");
    require(static_cast<int>(p.ray_coherence_seconds > 0) +
            static_cast<int>(p.gaussian_doppler_width_hz > 0) +
            static_cast<int>(p.random_doppler_width_hz > 0) <= 1,
            "OU, sampled Gaussian and fixed-time Gaussian ray evolution are mutually exclusive");
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
    const long double q = std::floor(static_cast<long double>(t) / dwell);
    require(std::isfinite(q) && q > static_cast<long double>(INT64_MIN) &&
            q < static_cast<long double>(INT64_MAX), "stream channel dwell coordinate overflow");
    return static_cast<std::int64_t>(q);
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

Complex interpolate(const std::vector<Complex>& history, std::uint64_t first,
                    std::uint64_t end,
                    long double position) {
    if (!std::isfinite(position) || position < -8 || position >=
        static_cast<long double>(end) + 8) return {};
    auto sample = [&](std::uint64_t index) {
        return index >= first && index < end ? history[static_cast<std::size_t>(index % history.size())] : Complex{};
    };
    if (position == std::floor(position) && position >= 0) {
        if (position >= static_cast<long double>(UINT64_MAX)) return {};
        const auto index = static_cast<std::uint64_t>(position);
        return sample(index);
    }
    const auto center_ld = std::floor(position);
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
        if (tap >= 0 && tap < static_cast<long double>(UINT64_MAX)) {
            const auto index = static_cast<std::uint64_t>(tap);
            value += coefficient * sample(index);
        }
        norm += coefficient;
    }
    return std::abs(norm) > 1e-12 ? value / norm : Complex{};
}

struct OscillatorState {
    double phase = 0, rw_frequency = 0;
    std::vector<double> flicker;
    std::vector<double> taus;
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

void initialize_spectrum(OscillatorState& state, const OscillatorSpectrum& s, Rng& rng) {
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
        state.flicker[i] = component_rms * rng.gaussian();
    }
}

double oscillator_phase_step(OscillatorState& state, const Oscillator& base,
                             const OscillatorSpectrum& spectrum, Rng& rng,
                             double rf_hz, double nominal_if_hz,
                             double audio_scale, double t, double dt) {
    const double white_sigma = spectrum.white_fm_hz_sqrt_seconds / std::sqrt(dt);
    const double white_frequency = white_sigma * rng.gaussian();
    state.rw_frequency += spectrum.random_walk_fm_hz_per_sqrt_second *
        std::sqrt(dt) * rng.gaussian();
    double flicker_frequency = 0;
    for (std::size_t i = 0; i < state.flicker.size(); ++i) {
        const double alpha = std::exp(-dt / state.taus[i]);
        state.flicker[i] = alpha * state.flicker[i] +
            (spectrum.flicker_fm_rms_hz / std::sqrt(static_cast<double>(state.flicker.size()))) *
            std::sqrt(1 - alpha * alpha) * rng.gaussian();
        flicker_frequency += state.flicker[i];
    }
    const double offset = rf_hz * (base.rf_ppm + base.rf_drift_ppm_per_second * t) * 1e-6 +
        nominal_if_hz * (audio_scale - 1) + white_frequency + state.rw_frequency +
        flicker_frequency;
    const double wiener = base.phase_noise_rad_per_sqrt_second * std::sqrt(dt) * rng.gaussian();
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
    require((cfg.selective_model.model == SelectiveModel::LegacyAllpass ||
             cfg.selective_model.model == SelectiveModel::DispersiveDft) &&
            (!cfg.selective_model.modulation_rate_hz ||
             finite_nonnegative(*cfg.selective_model.modulation_rate_hz)) &&
            finite_nonnegative(cfg.selective_model.correlation_spacing_hz) &&
            std::isfinite(cfg.selective_model.center_hz) &&
            finite_nonnegative(cfg.selective_tail_floor_seconds),
            "invalid versioned selective modulation model");
    require(cfg.selective_model.coefficient_law == SelectiveCoefficientLaw::Sinusoidal ||
            (cfg.selective_model.coefficient_law == SelectiveCoefficientLaw::GaussianErf &&
             cfg.selective_model.model == SelectiveModel::DispersiveDft),
            "random selective coefficients require the isolated dispersive DFT model");
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
    std::uint64_t total = 0, cursor = 0, tx_next = 0, history_first = 0;
    std::uint64_t delay_samples = 0, tx_interpolation_tail = 0, tail_samples = 0;
    std::vector<Complex> tx_history;
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
    std::unique_ptr<SpreadRay> spread;
    std::unique_ptr<FrequencySelectivePerturbation> perturbation;
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
          effects(config.effects), tx_rng(root_seed(cfg.oscillator_seed,cfg.effects.seed) ^ 0x3254494154545801ULL),
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
        const bool dispersive = config.selective_model.model == SelectiveModel::DispersiveDft;
        const double selective_spacing = config.selective_model.correlation_spacing_hz > 0 ?
            config.selective_model.correlation_spacing_hz : geometry.carrier_spacing_hz;
        const auto actual_perturb_tail = perturb_enabled ? (dispersive ?
            DispersiveSelectivePerturbation::tail_samples(effects.sample_rate * rx_scale, selective_spacing) :
            FrequencySelectivePerturbation::decay_samples(effects.sample_rate * rx_scale,
                                                        effects.occupied_bandwidth_hz)) : 0;
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

        const auto history_count = static_cast<std::size_t>(delay_samples + 64);
        const auto prefix_count = effects.fhss ? effects.hop_offsets_hz.size() + 1 : 0;
        const auto spectrum_values = (config.tx_spectrum.flicker_fm_rms_hz > 0 ? config.tx_spectrum.flicker_bands : 0) +
            (config.rx_spectrum.flicker_fm_rms_hz > 0 ? config.rx_spectrum.flicker_bands : 0);
        // All owned arrays are fixed-size; no deque growth or duration-sized
        // history is hidden from this pre-allocation bound. Fixed allowance
        // covers allocator metadata for the bounded number of allocations.
        stats.working_bytes = sizeof(*this) + 4096 + history_count * sizeof(Complex) +
            effects.hop_offsets_hz.capacity() * sizeof(double) + prefix_count * sizeof(long double) +
            config.missed_receiver_dwell_indices.capacity() * sizeof(std::uint64_t) +
            spectrum_values * 2 * sizeof(double) +
            (gaussian_path ? gaussian_path->working_bytes() : 0) +
            (random_path[0] ? 2*sizeof(detail::FixedGaussianRay) : 0) +
            (random_delay ? sizeof(detail::SmoothRandomDelay) : 0) +
            stats.path_gaussian_fir_coefficients.capacity()*sizeof(double) +
            (spread_enabled ? sizeof(SpreadRay) : 0) +
            (perturb_enabled ? (dispersive ? sizeof(DispersiveSelectivePerturbation) +
                DispersiveSelectivePerturbation::memory_samples(effects.sample_rate * rx_scale,
                                                               selective_spacing)*sizeof(Complex) +
                (random_selective ? DispersiveSelectivePerturbation::maximum_random_working_bytes() : 0) :
                sizeof(FrequencySelectivePerturbation)) : 0);
        require(stats.working_bytes <= kHistoryLimitBytes + 1024 * 1024,
                "stream channel workspace exceeds its fixed bound");
        if (explicit_source) {
            require(src.working_bytes <= kHistoryLimitBytes + 1024 * 1024 - stats.working_bytes,
                    "emitted source and channel workspace exceed the fixed bound");
            stats.working_bytes += src.working_bytes;
        }
        tx_history.resize(history_count);

        if (spread_enabled) spread = std::make_unique<SpreadRay>(
            effects.sample_rate * rx_scale, effects.spread_doppler_hz,
            effects.seed ^ 0x325449415443484eULL);
        if (perturb_enabled) {
            const auto seed = effects.seed ^ 0x3254494154504552ULL;
            auto& diagnostic = stats.selective_perturbation.emplace();
            diagnostic.model = config.selective_model.model;
            diagnostic.coefficient_law = config.selective_model.coefficient_law;
            diagnostic.sample_rate_hz = effects.sample_rate * rx_scale;
            diagnostic.component_depths = {effects.perturbation_slow_depth, effects.perturbation_fast_depth};
            if (dispersive) {
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
            } else {
                // Exact historical operator, including zero-centered geometry,
                // initial phases and RNG domains. No ordinary path changes.
                perturbation = std::make_unique<FrequencySelectivePerturbation>(
                    effects.sample_rate * rx_scale, 0, effects.occupied_bandwidth_hz,
                    selective_rate, effects.perturbation_slow_depth,
                    effects.perturbation_fast_depth, seed);
                diagnostic.band_width_hz = perturbation->band_width_hz;
                diagnostic.component_rates_hz = perturbation->rates_hz;
                diagnostic.band_centers_hz = perturbation->band_centers_hz;
                diagnostic.initial_phases_radians = perturbation->initial_phases;
            }
            diagnostic.seed = seed;
            diagnostic.decay_tail_samples = actual_perturb_tail;
        }
        initialize_spectrum(tx_osc, config.tx_spectrum, tx_spectrum_rng);
        initialize_spectrum(rx_osc, config.rx_spectrum, rx_spectrum_rng);
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
            const double t = static_cast<double>(tx_next) * dt_tx;
            const double calendar_time = static_cast<double>(static_cast<long double>(tx_next) /
                (static_cast<long double>(effects.sample_rate) * tx_scale) - hop_origin_seconds);
            const double hop = hop_at(effects, calendar_time);
            const auto current_dwell = dwell_index(calendar_time, effects.dwell_seconds);
            if (effects.fhss && current_dwell != previous_tx_dwell) {
                tx_osc.phase += keyed_phase(root_seed(config.oscillator_seed,effects.seed), current_dwell,
                                            0x5458484f50504841ULL);
                previous_tx_dwell = current_dwell;
            }
            const double rf = effects.rf_hz + hop;
            tx_osc.phase += oscillator_phase_step(tx_osc, effects.tx, config.tx_spectrum,
                tx_spectrum_rng, rf, effects.nominal_conversion_if_hz, tx_scale, t, dt_tx);
            // Keep the historical positive-RF arithmetic unchanged when this
            // supplemental emission-time slope is off.
            if (config.tx_frequency_drift_hz_per_second != 0)
                tx_osc.phase += tau * config.tx_frequency_drift_hz_per_second * t * dt_tx;
            tx_osc.phase = std::remainder(tx_osc.phase, tau);
            auto value = effects.signal_enabled ? source_sample(tx_next) : Complex{};
            const double white_pm = config.tx_spectrum.white_pm_rms_radians * tx_spectrum_rng.gaussian();
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
            history_first = tx_next > tx_history.size() ? tx_next - tx_history.size() : 0;
        }
    }

    Complex process_one(std::uint64_t n, bool& erased) {
        const long double physical_time = static_cast<long double>(n) /
            (static_cast<long double>(effects.sample_rate) * rx_scale) - effects.capture_lead_seconds;
        const double t = static_cast<double>(physical_time);
        const double excess_delay_seconds = random_delay ? random_delay->value(physical_time) :
            effects.spread_delay_ms*.001;
        // Preserve the exact integer lattice when the clocks/offset agree;
        // avoid a lossy double sample -> seconds -> sample round trip.
        const long double direct_pos = static_cast<long double>(n) *
            (static_cast<long double>(tx_scale) / rx_scale) -
            (static_cast<long double>(effects.capture_lead_seconds) + effects.timing_offset_seconds) *
                effects.sample_rate * tx_scale;
        // Keep legacy off-path multiplication ordering exactly unchanged.
        const long double delay_seconds = random_delay ? static_cast<long double>(excess_delay_seconds) :
            static_cast<long double>(effects.spread_delay_ms)*.001L;
        const long double delayed_pos = direct_pos - delay_seconds * tx_scale * effects.sample_rate;
        auto needed_last = [&](long double position) {
            require(std::isfinite(position), "stream channel source coordinate overflow");
            const auto last = geometry.source_samples - 1;
            if (position < 0) return std::uint64_t{0};
            const auto needed = std::floor(position) + 8;
            return needed >= static_cast<long double>(last) ? last : static_cast<std::uint64_t>(needed);
        };
        const auto direct_last = needed_last(direct_pos);
        const auto delayed_last = needed_last(delayed_pos);
        generate_tx_until(std::max(direct_last, delayed_last));
        Complex direct = interpolate(tx_history, history_first, tx_next, direct_pos);
        Complex delayed = interpolate(tx_history, history_first, tx_next, delayed_pos);

        // A shared public nominal origin fixes padding alignment without
        // recentering the receiver from simulated TX arrival/clock truth.
        const double receive_calendar = static_cast<double>(physical_time +
            effects.retune_offset_seconds - hop_origin_seconds);
        const double direct_calendar = static_cast<double>(physical_time -
            effects.timing_offset_seconds - hop_origin_seconds);
        const double delayed_calendar = static_cast<double>(physical_time -
            effects.timing_offset_seconds - delay_seconds - hop_origin_seconds);
        const double rx_hop = hop_at(effects, receive_calendar);
        const double direct_hop = hop_at(effects, direct_calendar);
        const double delayed_hop = hop_at(effects, delayed_calendar);
        const double limit = effects.occupied_bandwidth_hz * .5;
        bool out_of_band = false;
        if (std::abs(direct_hop - rx_hop) > limit) { direct = {}; out_of_band = true; }
        if (std::abs(delayed_hop - rx_hop) > limit) { delayed = {}; out_of_band = true; }
        if (out_of_band) ++stats.out_of_passband_samples;
        const auto direct_phase = tau * std::remainder(static_cast<double>(
            hop_cycles(effects, direct_calendar, hop_prefix) - hop_cycles(effects, receive_calendar, hop_prefix)), 1.0);
        const auto delayed_phase = tau * std::remainder(static_cast<double>(
            hop_cycles(effects, delayed_calendar, hop_prefix) - hop_cycles(effects, receive_calendar, hop_prefix)), 1.0);
        direct *= std::polar(1.0, direct_phase);
        delayed *= std::polar(1.0, delayed_phase);
        if(random_delay) {
            const auto cycles=std::remainder((static_cast<long double>(effects.rf_hz)+
                effects.nominal_conversion_if_hz)*delay_seconds,1.L);
            delayed*=std::polar(1.0,-tau*static_cast<double>(cycles));
        } else delayed *= std::polar(1.0, -tau * std::remainder(effects.rf_hz *
            effects.spread_delay_ms * .001, 1.0));
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
            direct*=random_path[0]->value(physical_time);
            delayed*=random_path[1]->value(physical_time);
        }
        Complex propagated = spread ? spread->process(direct, delayed) : direct;
        const auto& path = config.path_evolution;
        if (path.diurnal_phase_amplitude_radians > 0) {
            const auto cycles = std::remainder(physical_time/path.diurnal_period_seconds, 1.L);
            const double angle = path.diurnal_phase_amplitude_radians*std::sin(
                tau*static_cast<double>(cycles)+stats.path_diurnal_initial_phase_radians);
            propagated *= std::polar(1., angle);
        }
        if (path.outage_start_seconds >= 0) propagated *= outage_gain(path, physical_time);
        if (perturbation || dispersive_perturbation) {
            auto& diagnostic = *stats.selective_perturbation;
            diagnostic.pre_selective_energy_samples += std::norm(propagated);
            propagated = dispersive_perturbation ? dispersive_perturbation->process(propagated,physical_time) :
                perturbation->process(propagated);
            diagnostic.post_selective_energy_samples += std::norm(propagated);
            ++diagnostic.processed_rows;
        }

        const auto rx_dwell = dwell_index(receive_calendar, effects.dwell_seconds);
        if (effects.fhss && rx_dwell != previous_rx_dwell) {
            rx_osc.phase += keyed_phase(root_seed(config.oscillator_seed,effects.seed) ^ 0x5258484f50504841ULL,
                                        rx_dwell, 0x5258504841534531ULL);
            previous_rx_dwell = rx_dwell;
        }
        const double rf_error = (effects.rf_hz + rx_hop) *
            (effects.rx.rf_ppm + effects.rx.rf_drift_ppm_per_second * t) * 1e-6 -
            effects.static_doppler_hz;
        rx_osc.phase += oscillator_phase_step(rx_osc, effects.rx, config.rx_spectrum,
            rx_spectrum_rng, 0, effects.nominal_conversion_if_hz,
            rx_scale, t, dt_rx) + tau * rf_error * dt_rx;
        rx_osc.phase = std::remainder(rx_osc.phase, tau);
        const double white_pm = config.rx_spectrum.white_pm_rms_radians * rx_spectrum_rng.gaussian();
        propagated *= std::polar(1.0, -rx_osc.phase - white_pm);
        stats.signal_energy += std::norm(propagated);
        const double sigma = std::sqrt(config.complex_noise_variance * .5);
        if (effects.noise_enabled)
            propagated += Complex{sigma * thermal_rng.gaussian(), sigma * thermal_rng.gaussian()};
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
        const auto receiver_dwell = dwell_index(receive_calendar, effects.dwell_seconds);
        const bool missed_dwell = receiver_dwell >= 0 &&
            std::binary_search(config.missed_receiver_dwell_indices.begin(),
                               config.missed_receiver_dwell_indices.end(),
                               static_cast<std::uint64_t>(receiver_dwell));
        erased = dropout_at(effects, effects.fhss ?
            static_cast<double>(physical_time - hop_origin_seconds) : t) || missed_dwell;
        if (erased) { propagated = {}; ++stats.erased_samples; }
        stats.observed_energy += std::norm(propagated);
        ++stats.samples;
        return propagated;
    }

    std::size_t read(std::span<Complex> output, std::span<std::uint8_t> validity) {
        require(validity.empty() || validity.size() == output.size(),
                "stream channel validity size differs from output");
        require(output.size() <= config.maximum_chunk_samples,
                "stream channel read exceeds its configured chunk bound");
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(output.size(), total - cursor));
        for (std::size_t i = 0; i < count; ++i) {
            bool erased = false;
            output[i] = process_one(cursor++, erased);
            if (!validity.empty()) validity[i] = erased ? 0 : 1;
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
