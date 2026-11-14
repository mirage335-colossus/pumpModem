#include "appraisal/channel.hpp"
#include "appraisal/hf_replay.hpp"
#include "lunar_spectral_internal.hpp"
#include "triad_random_process.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <random>
#include <stdexcept>
#include <utility>

namespace appraisal {
namespace {
constexpr double pi = std::numbers::pi;
constexpr std::size_t fractional_intervals = 2048;
constexpr double tail_relative_amplitude = 1e-6;
using GaussianRay = datapump::triad::v3::detail::FixedGaussianRay;

void require(bool condition, const char* message) {
    if (!condition) throw std::invalid_argument(message);
}
bool finite(double x) { return std::isfinite(x); }
bool finite(Complex x) { return finite(x.real()) && finite(x.imag()); }
void nonnegative(double x, const char* message) { require(finite(x) && x >= 0, message); }
std::size_t checked_add(std::size_t a, std::size_t b) {
    if (b > std::numeric_limits<std::size_t>::max() - a)
        throw std::invalid_argument("channel workspace size overflow");
    return a + b;
}
std::size_t checked_multiply(std::size_t a, std::size_t b) {
    if (a && b > std::numeric_limits<std::size_t>::max() / a)
        throw std::invalid_argument("channel workspace size overflow");
    return a * b;
}
std::size_t count_from_long_double(long double n) {
    require(std::isfinite(n) && n >= 0 && n < static_cast<long double>(
        std::numeric_limits<std::size_t>::max()), "channel sample count overflow");
    return static_cast<std::size_t>(n);
}

// This explicit transform is reproducible across standard-library versions.
// Upper 52-bit midpoint uniforms are strictly within (0,1), including after
// rounding; using 53 bits plus one half can round the largest value to 1.
struct Random {
    std::mt19937_64 engine;
    explicit Random(std::uint64_t root, std::uint64_t domain)
        : engine(root + 104729ULL * domain) {}
    double uniform() {
        return (static_cast<double>(engine() >> 12) + .5) / 4503599627370496.;
    }
    Complex proper() {
        const double radius = std::sqrt(-std::log(uniform()));
        return std::polar(radius, 2 * pi * uniform());
    }
};

void validate_resonator(const Resonator& r) {
    require(finite(r.detuning_hz) && finite(r.detuning_rate_hz_s),
            "resonator detuning must be finite");
    if (!r.enabled) return;
    require(finite(r.inductance_h) && r.inductance_h > 0 &&
            finite(r.capacitance_f) && r.capacitance_f > 0 &&
            finite(r.resistance_ohm) && r.resistance_ohm > 0,
            "resonator requires positive finite L, C and R");
    require(finite(r.resistance_ohm / (2 * r.inductance_h)) &&
            r.resistance_ohm / (2 * r.inductance_h) > 0 &&
            finite(1 / std::sqrt(r.inductance_h * r.capacitance_f)),
            "resonator coefficients overflow");
}

double maximum_delay(const ChannelSpec& spec, double horizon) {
    double maximum = 0;
    if (spec.lunar) {
        const auto& lunar = *spec.lunar;
        require(spec.paths.empty(), "lunar spectral propagation and explicit paths are mutually exclusive");
        require(finite(lunar.bulk_delay_seconds) && lunar.bulk_delay_seconds > 0 && finite(lunar.limb_width_hz) &&
                lunar.limb_width_hz >= 0 && finite(spec.rf_hz) && spec.rf_hz > 0, "invalid lunar delay coordinates");
        maximum = lunar.bulk_delay_seconds + 2 * detail::lunar_radius_m / detail::lunar_light_speed +
            (.5 * lunar.limb_width_hz + .001) * horizon / spec.rf_hz;
    }
    for (const auto& path : spec.paths) {
        require(finite(path.delay_seconds) && finite(path.delay_rate), "path delay must be finite");
        const double last = path.delay_seconds + path.delay_rate * horizon;
        double earliest_motion = 0, latest_motion = 0, maximum_rate = std::abs(path.delay_rate);
        if (path.hf) {
            detail::validate_hf_mode(*path.hf, path);
            const auto& motion = path.hf->group_delay_motion;
            const auto bounds = detail::hf_group_delay_motion_bounds(*path.hf, spec.rf_hz);
            earliest_motion = bounds.front();
            latest_motion = bounds.back();
            maximum_rate += 2 * pi * motion.amplitude_seconds / motion.period_seconds;
        }
        require(finite(last) && std::min(path.delay_seconds, last) + earliest_motion >= 0 && maximum_rate < 1,
                "moving path delay must remain nonnegative with slope below one over the horizon");
        maximum = std::max(maximum, std::max(path.delay_seconds, last) + latest_motion);
    }
    return maximum;
}

std::vector<double> interpolation_weights(unsigned half, double fraction) {
    std::vector<double> result(2 * half);
    detail::fractional_weights(half, fraction, result);
    return result;
}

double fade_gain(const Fade& f, double t) {
    if (f.first_seconds < 0 || t < f.first_seconds || t >= f.stop_seconds) return 1;
    double depth = 1;
    if (f.ramp_seconds > 0) {
        const double edge = std::min(t - f.first_seconds, f.stop_seconds - t);
        if (edge < f.ramp_seconds) depth = .5 * (1 - std::cos(pi * edge / f.ramp_seconds));
    }
    return 1 - depth * (1 - f.voltage_floor);
}
}

namespace detail {
void fractional_weights(unsigned half, double fraction, std::span<double> weights,
                        std::span<double> derivatives) {
    require(weights.size() == 2 * half && (derivatives.empty() || derivatives.size() == weights.size()),
            "fractional-delay stencil size mismatch");
    double sum = 0, slope_sum = 0;
    for (int k = 1 - static_cast<int>(half); k <= static_cast<int>(half); ++k) {
        const auto index = static_cast<std::size_t>(k + static_cast<int>(half) - 1);
        const double d = fraction - k, arg = pi * d, edge = arg / half;
        const double sinc = d == 0 ? 1 : std::sin(arg) / arg;
        const double window = std::abs(d) >= half ? 0 : .42 + .5 * std::cos(edge) + .08 * std::cos(2 * edge);
        weights[index] = sinc * window;
        sum += weights[index];
        if (!derivatives.empty()) {
            const double dsinc = std::abs(d) < 1e-4 ?
                -pi * arg * (1. / 3 - arg * arg / 30) : (std::cos(arg) - sinc) / d;
            const double dwindow = std::abs(d) >= half ? 0 :
                -(pi / half) * (.5 * std::sin(edge) + .16 * std::sin(2 * edge));
            derivatives[index] = dsinc * window + sinc * dwindow;
            slope_sum += derivatives[index];
        }
    }
    require(finite(sum) && std::abs(sum) > .5, "fractional-delay interpolation normalization failed");
    for (std::size_t j = 0; j < weights.size(); ++j) {
        weights[j] /= sum;
        if (!derivatives.empty()) derivatives[j] = (derivatives[j] - weights[j] * slope_sum) / sum;
    }
}
}

double resonator_tail_seconds(const Resonator& r, double relative_amplitude) {
    validate_resonator(r);
    require(finite(relative_amplitude) && relative_amplitude > 0 && relative_amplitude < 1,
            "resonator tail amplitude must be within (0,1)");
    return r.enabled ? -std::log(relative_amplitude) * 2 * r.inductance_h / r.resistance_ohm : 0;
}

Complex resonator_transfer(const Resonator& r, double offset_hz) {
    validate_resonator(r);
    require(finite(offset_hz), "resonator offset must be finite");
    if (!r.enabled) return {1, 0};
    const double alpha = r.resistance_ohm / (2 * r.inductance_h);
    // Continuous, narrowband envelope approximation. The implemented ZOH state
    // update has its separately testable finite-sample hold error. This is not
    // the exact wideband RF RLC response and cannot propagate distant Helper.
    return alpha / Complex{alpha, 2 * pi * (offset_hz + r.detuning_hz)};
}

namespace {
double channel_analog_tail_seconds(const ChannelSpec& spec) {
    nonnegative(spec.frontend.lowpass_hz, "frontend lowpass must be nonnegative and finite");
    double tail = resonator_tail_seconds(spec.transmitter);
    if (spec.frontend.lowpass_hz > 0)
        tail += -std::log(tail_relative_amplitude) / (2 * pi * spec.frontend.lowpass_hz);
    return tail;
}
}

double channel_filter_tail_seconds(const ChannelSpec& spec, double fs) {
    return channel_analog_tail_seconds(spec) + channel_interpolation_tail_seconds(spec, fs);
}

double channel_tail_seconds(const ChannelSpec& spec, double horizon_seconds) {
    nonnegative(horizon_seconds, "channel horizon must be nonnegative and finite");
    const double tail = maximum_delay(spec, horizon_seconds) + channel_analog_tail_seconds(spec);
    require(finite(tail), "channel tail overflow");
    // Finite interpolation/dispersion memory is added separately, at the
    // caller's actual native sample rate.
    // Random interference and AGC release are not deterministic signal support.
    return tail;
}

struct Channel::Impl {
    struct RuntimePath {
        const Path* spec = nullptr;
        Complex fixed{1, 0}, phase{1, 0}, phase_step{1, 0}, step_change{1, 0};
        Complex coherent{};
        double diffuse_scale = 1;
        HfEikonal eikonal_initial{};
        std::unique_ptr<GaussianRay> gaussian;
        std::vector<double> static_weights;
        bool integer_delay = false, moving = false;
        std::size_t delay_group = std::numeric_limits<std::size_t>::max();
    };
    struct EndpointCache {
        std::uint64_t generation = UINT64_MAX;
        std::int64_t base = INT64_MIN;
        Complex value{};
    };
    struct DelayGroup { std::vector<EndpointCache> endpoints; };

    Source source;
    ChannelSpec spec;
    ChannelReport report;
    double fs = 0, dt = 0;
    std::uint64_t horizon = 0, next_source = 0;
    std::vector<Complex> history;
    std::vector<RuntimePath> paths;
    std::vector<double> interpolation_table;
    std::vector<DelayGroup> delay_groups;
    std::unique_ptr<detail::LunarSpectral> lunar;
    Random white, colored, events, marks;
    Complex tx_state{}, colored_state{}, impulse_state{}, lowpass_state{};
    double colored_decay = 0, colored_innovation = 0, impulse_decay = 0;
    double next_event = std::numeric_limits<double>::infinity();
    double lowpass_decay = 0, gain = 1, attack_decay = 0, release_decay = 0;
    Complex stationary_tx_decay{1, 0}, stationary_tx_drive{};

    Impl(Source input, ChannelSpec config, std::uint64_t seed, std::uint64_t count, std::stop_token stop)
        : source(std::move(input)), spec(std::move(config)), horizon(count),
          white(seed, 2), colored(seed, 6), events(seed, 3), marks(seed, 4) {
        require(source.format.sample_rate > 0 && source.format.sample_rate_denominator > 0,
                "channel source sample rate must be positive");
        fs = static_cast<double>(source.format.sample_rate) / source.format.sample_rate_denominator;
        dt = 1 / fs;
        require(finite(fs) && fs > 0, "channel sample rate overflow");
        require(horizon <= static_cast<std::uint64_t>(INT64_MAX / 4), "channel horizon exceeds signed coordinate range");
        require(source.geometry.source_samples == 0 || static_cast<bool>(source.sample_at), "channel source callback is missing");
        require(finite(spec.rf_hz) && spec.rf_hz > 0, "RF carrier must be positive and finite");
        require(spec.interpolation_half_width >= 4 && spec.interpolation_half_width <= 64,
                "fractional-delay half width must be in [4,64]");
        require(spec.lunar ? spec.paths.empty() : (!spec.paths.empty() && spec.paths.size() <= 65536),
                "channel requires lunar spectral propagation or 1..65536 explicit paths");
        require(finite(source.minimum_frequency_hz) && finite(source.maximum_frequency_hz) &&
                source.minimum_frequency_hz <= source.maximum_frequency_hz, "invalid source spectral guard");
        const double seconds = static_cast<double>(horizon) / fs;
        report.maximum_delay_seconds = maximum_delay(spec, seconds);
        report.tail_seconds = report.maximum_delay_seconds + channel_filter_tail_seconds(spec, fs);
        if (spec.transmitter.enabled) {
            require(std::max(std::abs(spec.transmitter.detuning_hz), std::abs(spec.transmitter.detuning_hz +
                    spec.transmitter.detuning_rate_hz_s * seconds)) < fs / 2,
                    "resonator detuning trajectory reaches Nyquist");
            require(2 * pi * std::abs(spec.transmitter.detuning_rate_hz_s) / (fs * fs) <= .01,
                    "resonator detuning changes too rapidly for midpoint exponential steps");
        }
        const double source_band = std::max(std::abs(source.minimum_frequency_hz), std::abs(source.maximum_frequency_hz));
        require(source_band < fs / 2, "source spectral allowance reaches Nyquist");
        std::optional<LunarSpectralPlan> lunar_plan;
        if (spec.lunar) {
            lunar_plan = lunar_spectral_plan(*spec.lunar, spec.rf_hz, fs, spec.interpolation_half_width, horizon);
            require(source_band <= 362.5, "source spectral allowance exceeds the qualified lunar envelope band");
        }
        bool any_fractional = spec.lunar.has_value(), any_moving = false;
        std::size_t fading_count = 0;
        for (const auto& path : spec.paths) {
            nonnegative(path.power, "path power must be finite and nonnegative");
            require(finite(path.phase_radians) && finite(path.residual_doppler_hz) && finite(path.residual_drift_hz_s),
                    "path phase and residual Doppler must be finite");
            nonnegative(path.phase_wander_amplitude_radians, "phase-wander amplitude must be finite and nonnegative");
            require(finite(path.phase_wander_period_seconds) && path.phase_wander_period_seconds > 0,
                    "phase-wander period must be finite and positive");
            const double wander_guard = path.phase_wander_amplitude_radians == 0 ? 0 :
                (path.phase_wander_amplitude_radians + 1) / path.phase_wander_period_seconds;
            nonnegative(path.gaussian_width_2sigma_hz, "Gaussian Doppler width must be finite and nonnegative");
            require(!(path.frozen_gaussian && path.gaussian_width_2sigma_hz > 0),
                    "frozen and time-varying Gaussian path laws are mutually exclusive");
            const double shift0 = path.residual_doppler_hz - spec.rf_hz * path.delay_rate;
            const double shift1 = shift0 + path.residual_drift_hz_s * seconds;
            // Gaussian spectra are not compact: three 2-sigma widths on each
            // side is a declared six-sigma engineering allowance, not a theorem.
            // Phase modulation adds maximum frequency excursion plus its
            // modulation frequency (Carson-style allowance); its Bessel
            // sidebands likewise do not have strictly compact support.
            const double hf_margin = path.hf ? detail::hf_mode_spectral_margin_hz(*path.hf, spec.rf_hz, source_band) : 0;
            if (path.hf && (path.hf->group_delay_slope_seconds_per_hz != 0 || path.hf->parabolic_layer)) {
                const auto bounds = detail::hf_group_delay_motion_bounds(*path.hf, spec.rf_hz);
                const double earliest = std::min(path.delay_seconds, path.delay_seconds + path.delay_rate * seconds) -
                    std::abs(bounds.front());
                require(earliest >= static_cast<double>(detail::hf_dispersion_half_width) / fs,
                        "HF dispersion stencil must fit inside the minimum physical delay");
                double maximum_slope = std::abs(path.hf->group_delay_slope_seconds_per_hz);
                if (path.hf->parabolic_layer) {
                    const auto& layer = *path.hf->parabolic_layer;
                    const double minimum_critical = layer.critical_frequency_hz - layer.critical_amplitude_hz;
                    require(spec.rf_hz + source_band < .99 * minimum_critical,
                            "HF source band approaches the parabolic turning-frequency cutoff");
                    maximum_slope = hf_parabolic_eikonal(layer, spec.rf_hz,
                        ( .75L - layer.critical_phase_radians / (2 * std::numbers::pi_v<long double>)) *
                        layer.critical_period_seconds).group_delay_slope_seconds_per_hz;
                }
                require(source_band <= .20 * fs &&
                        std::abs(hf_residual_dispersion_phase_radians(maximum_slope, source_band)) <= .002,
                        "HF dispersion approximation exceeds its admitted phase or sample-band bound");
            }
            if (path.hf && path.hf->measured_envelope) {
                validate_hf_replay_trace(*path.hf->measured_envelope);
                const auto available = hf_replay_support_seconds(*path.hf->measured_envelope) - path.hf->measured_offset_seconds;
                require(available >= 0 && (horizon == 0 || static_cast<double>(horizon - 1) / fs <= available),
                        "HF render horizon exceeds its measured envelope support");
            }
            const double guard = source_band * (1 + std::abs(path.delay_rate)) +
                std::max(std::abs(shift0), std::abs(shift1)) + 3 * path.gaussian_width_2sigma_hz + wander_guard + hf_margin;
            require(finite(guard) && guard < fs / 2, "path spectral allowance reaches Nyquist");
            const double delay_samples = path.delay_seconds * fs;
            const bool moving = path.delay_rate != 0 || (path.hf && (path.hf->group_delay_motion.amplitude_seconds != 0 ||
                (path.hf->parabolic_layer && path.hf->parabolic_layer->critical_amplitude_hz != 0)));
            const bool fractional = moving || delay_samples != std::floor(delay_samples);
            any_fractional |= fractional;
            any_moving |= moving;
            if (path.gaussian_width_2sigma_hz > 0) ++fading_count;
        }
        if (any_fractional) {
            // Leave a finite transition band for the declared Blackman-windowed
            // sinc. Tests quantify gain/phase within this guard; no brick-wall
            // support is asserted for a finite native pulse.
            require(source_band <= fs * (.5 - 1.5 / spec.interpolation_half_width),
                    "source guard is too wide for the selected fractional-delay interpolator");
        }

        const auto& noise = spec.noise;
        nonnegative(noise.white_variance, "white noise variance must be finite and nonnegative");
        nonnegative(noise.colored_variance, "colored noise variance must be finite and nonnegative");
        require(finite(noise.colored_corner_hz) && noise.colored_corner_hz > 0 &&
                (noise.colored_variance == 0 || noise.colored_corner_hz < fs / 2), "invalid colored-noise corner");
        nonnegative(noise.impulse_rate_hz, "impulse rate must be finite and nonnegative");
        nonnegative(noise.impulse_rms, "impulse RMS mark must be finite and nonnegative");
        require(finite(noise.impulse_decay_seconds) && noise.impulse_decay_seconds > 0,
                "impulse decay must be finite and positive");
        // These are explicit local work bounds, never silent clipping of events.
        require(noise.impulse_rate_hz / fs <= 32 && noise.impulse_rate_hz * noise.impulse_decay_seconds <= 10000,
                "impulse process exceeds per-sample or stationary-start work bound");
        nonnegative(noise.tone_amplitude, "blocker amplitude must be finite and nonnegative");
        require(finite(noise.tone_hz) && finite(noise.tone_sweep_hz_s), "blocker frequency must be finite");
        require(noise.tone_amplitude == 0 || std::max(std::abs(noise.tone_hz),
                std::abs(noise.tone_hz + noise.tone_sweep_hz_s * seconds)) < fs / 2,
                "blocker sweep reaches Nyquist");

        const auto& frontend = spec.frontend;
        require(frontend.lowpass_hz == 0 || (frontend.lowpass_hz > 0 && frontend.lowpass_hz < fs / 2),
                "frontend lowpass must be zero or below Nyquist");
        nonnegative(frontend.clip_magnitude, "clip magnitude must be finite and nonnegative");
        nonnegative(frontend.agc_target, "AGC target must be finite and nonnegative");
        require(finite(frontend.agc_attack_seconds) && frontend.agc_attack_seconds > 0 &&
                finite(frontend.agc_release_seconds) && frontend.agc_release_seconds > 0,
                "AGC attack and release must be finite and positive");
        require(frontend.adc_bits <= 24 && (frontend.adc_bits == 0 || frontend.clip_magnitude > 0),
                "ADC needs 1..24 bits and a positive full-scale magnitude");
        const auto& fade = spec.fade;
        require(finite(fade.first_seconds) && finite(fade.stop_seconds) && finite(fade.ramp_seconds) &&
                finite(fade.voltage_floor) && fade.voltage_floor >= 0 && fade.voltage_floor <= 1,
                "invalid fade parameters");
        require((fade.first_seconds == -1 && fade.stop_seconds == -1) ||
                (fade.first_seconds >= 0 && fade.stop_seconds > fade.first_seconds && fade.ramp_seconds >= 0 &&
                 fade.ramp_seconds <= .5 * (fade.stop_seconds - fade.first_seconds)),
                "fade must be disabled or have ordered bounds and nonoverlapping ramps");

        const auto history_count = count_from_long_double(std::ceil(static_cast<long double>(report.maximum_delay_seconds) * fs) +
            2 * spec.interpolation_half_width + detail::hf_dispersion_halo_samples(spec) + 4);
        std::size_t bytes = checked_add(sizeof(Impl), source.working_bytes);
        if (lunar_plan) bytes = checked_add(bytes, lunar_plan->workspace_bytes);
        bytes = checked_add(bytes, checked_multiply(history_count, sizeof(Complex)));
        bytes = checked_add(bytes, checked_multiply(spec.paths.capacity(), sizeof(Path)));
        for (const auto& path : spec.paths)
            if (path.hf) {
                bytes = checked_add(bytes, checked_multiply(path.hf->voltage_transitions.capacity(), sizeof(HfVoltageTransition)));
                if (path.hf->measured_envelope)
                    bytes = checked_add(bytes, checked_multiply(path.hf->measured_envelope->samples.capacity(), sizeof(Complex)));
            }
        bytes = checked_add(bytes, checked_multiply(spec.paths.size(), sizeof(RuntimePath)));
        bytes = checked_add(bytes, checked_multiply(fading_count, sizeof(GaussianRay)));
        bytes = checked_add(bytes, checked_multiply(checked_multiply(spec.paths.size(), 2 * spec.interpolation_half_width), sizeof(double)));
        const auto table_count = any_moving ? checked_multiply(fractional_intervals + 1, 2 * spec.interpolation_half_width) : 0;
        bytes = checked_add(bytes, checked_multiply(table_count, sizeof(double)));
        // Temporary ordering and membership arrays used to discover shared
        // delay rings are part of the construction peak. This cache shares
        // exact interpolation endpoint dot products; every cell retains its
        // full moving envelope delay and carrier phase.
        const auto planning_bytes = checked_multiply(spec.paths.size(), 2 * sizeof(std::size_t));
        bytes = checked_add(bytes, planning_bytes);
        if (bytes > spec.maximum_workspace_bytes) throw ChannelWorkspaceError("channel exceeds explicit workspace limit");
        std::vector<std::size_t> order(spec.paths.size()), membership(spec.paths.size(), std::numeric_limits<std::size_t>::max());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            return spec.paths[a].delay_seconds < spec.paths[b].delay_seconds;
        });
        std::size_t groups = 0;
        const auto group_bytes = checked_add(sizeof(DelayGroup),
            checked_multiply(fractional_intervals + 1, sizeof(EndpointCache)));
        for (std::size_t first = 0; first < order.size();) {
            std::size_t stop = first + 1, moving_count = spec.paths[order[first]].delay_rate != 0 ? 1 : 0;
            while (stop < order.size() && spec.paths[order[stop]].delay_seconds == spec.paths[order[first]].delay_seconds) {
                if (spec.paths[order[stop]].delay_rate != 0) ++moving_count;
                ++stop;
            }
            // A cache is worthwhile for repeated moving delays. If it cannot
            // fit, retain the admitted direct interpolator, not a smaller
            // physical channel or a looser numerical approximation.
            if (moving_count >= 8 && group_bytes <= spec.maximum_workspace_bytes - bytes) {
                bytes += group_bytes;
                for (std::size_t i = first; i < stop; ++i)
                    if (spec.paths[order[i]].delay_rate != 0) membership[order[i]] = groups;
                ++groups;
            }
            first = stop;
        }
        report.workspace_bytes = bytes;
        delay_groups.resize(groups);
        for (auto& group : delay_groups) group.endpoints.resize(fractional_intervals + 1);
        history.resize(history_count);
        if (lunar_plan)
            lunar = std::make_unique<detail::LunarSpectral>(*spec.lunar, spec.rf_hz, fs,
                spec.interpolation_half_width, *lunar_plan, seed, stop);
        if (any_moving) {
            interpolation_table.reserve(table_count);
            for (std::size_t i = 0; i <= fractional_intervals; ++i) {
                const auto weights = interpolation_weights(spec.interpolation_half_width,
                    static_cast<double>(i) / fractional_intervals);
                interpolation_table.insert(interpolation_table.end(), weights.begin(), weights.end());
            }
        }
        paths.reserve(spec.paths.size());
        Random path_random(seed, 1);
        for (std::size_t i = 0; i < spec.paths.size(); ++i) {
            RuntimePath runtime;
            runtime.spec = &spec.paths[i];
            runtime.delay_group = membership[i];
            const auto& path = *runtime.spec;
            runtime.fixed = std::sqrt(path.power) * (path.frozen_gaussian ? path_random.proper() : Complex{1, 0});
            runtime.phase = std::polar(1., path.phase_radians);
            const long double f0 = path.residual_doppler_hz - static_cast<long double>(spec.rf_hz) * path.delay_rate;
            runtime.phase_step = std::polar(1., static_cast<double>(2 * std::numbers::pi_v<long double> *
                std::remainder(f0 / fs + .5L * path.residual_drift_hz_s / (fs * fs), 1.L)));
            runtime.step_change = std::polar(1., 2 * pi * path.residual_drift_hz_s / (fs * fs));
            if (path.gaussian_width_2sigma_hz > 0)
                runtime.gaussian = std::make_unique<GaussianRay>(path.gaussian_width_2sigma_hz,
                    seed + 104729ULL * (17 + i));
            if (path.hf && path.hf->coherent_to_diffuse_power > 0) {
                const auto k = path.hf->coherent_to_diffuse_power;
                Random coherent_random(seed, 1002 + 32 * i);
                runtime.coherent = std::polar(std::sqrt(k / (1 + k)), 2 * pi * coherent_random.uniform());
                runtime.diffuse_scale = 1 / std::sqrt(1 + k);
            }
            const double lag = path.delay_seconds * fs;
            runtime.moving = path.delay_rate != 0 || (path.hf && (path.hf->group_delay_motion.amplitude_seconds != 0 ||
                (path.hf->parabolic_layer && path.hf->parabolic_layer->critical_amplitude_hz != 0)));
            if (path.hf && path.hf->parabolic_layer)
                runtime.eikonal_initial = hf_parabolic_eikonal(*path.hf->parabolic_layer, spec.rf_hz, 0);
            runtime.integer_delay = !runtime.moving && lag == std::floor(lag);
            if (!runtime.integer_delay && !runtime.moving)
                runtime.static_weights = interpolation_weights(spec.interpolation_half_width, std::ceil(lag) - lag);
            paths.push_back(std::move(runtime));
        }
        colored_decay = std::exp(-2 * pi * noise.colored_corner_hz / fs);
        colored_innovation = std::sqrt(noise.colored_variance * -std::expm1(-4 * pi * noise.colored_corner_hz / fs));
        colored_state = std::sqrt(noise.colored_variance) * colored.proper();
        impulse_decay = std::exp(-dt / noise.impulse_decay_seconds);
        if (noise.impulse_rate_hz > 0 && noise.impulse_rms > 0) {
            // Stationary shot-noise start, truncated at 1e-6 amplitude. Marks
            // have zero mean, so omitted mean-square contribution is 1e-12 of
            // stationary variance. Initialization events are not counted as
            // events in the rendered observation.
            next_event = std::log(tail_relative_amplitude) * noise.impulse_decay_seconds;
            next_event += -std::log(events.uniform()) / noise.impulse_rate_hz;
            std::size_t initial_events = 0;
            while (next_event < 0) {
                require(++initial_events <= 1000000, "impulse stationary initialization exceeded work bound");
                impulse_state += noise.impulse_rms * marks.proper() * std::exp(next_event / noise.impulse_decay_seconds);
                next_event += -std::log(events.uniform()) / noise.impulse_rate_hz;
            }
        }
        lowpass_decay = frontend.lowpass_hz > 0 ? std::exp(-2 * pi * frontend.lowpass_hz / fs) : 0;
        attack_decay = std::exp(-dt / frontend.agc_attack_seconds);
        release_decay = std::exp(-dt / frontend.agc_release_seconds);
        if (spec.transmitter.enabled) {
            const auto& r = spec.transmitter;
            const double alpha = r.resistance_ohm / (2 * r.inductance_h);
            const Complex pole{alpha, 2 * pi * r.detuning_hz};
            stationary_tx_decay = std::exp(-pole * dt);
            stationary_tx_drive = alpha / pole * (Complex{1, 0} - stationary_tx_decay);
        }
    }

    void ensure_source(std::uint64_t last) {
        while (next_source <= last) {
            Complex input{};
            if (next_source < source.geometry.source_samples) {
                input = source.sample_at(next_source);
                require(finite(input), "source emitted a nonfinite sample");
                ++report.source_samples;
                report.input_energy += static_cast<long double>(std::norm(input));
            }
            Complex filtered = input;
            if (spec.transmitter.enabled) {
                // State is at the LEFT boundary n/Fs. x[n] drives [n,n+1).
                // Returning the previous state preserves a causal exact ZOH
                // convention rather than silently advancing one sample.
                filtered = tx_state;
                if (spec.transmitter.detuning_rate_hz_s == 0) {
                    tx_state = stationary_tx_decay * tx_state + stationary_tx_drive * input;
                } else {
                    const double alpha = spec.transmitter.resistance_ohm / (2 * spec.transmitter.inductance_h);
                    const double t_mid = (static_cast<double>(next_source) + .5) / fs;
                    const Complex pole{alpha, 2 * pi * (spec.transmitter.detuning_hz +
                        spec.transmitter.detuning_rate_hz_s * t_mid)};
                    const auto decay = std::exp(-pole * dt);
                    // Midpoint-frozen coefficient: exact per-step exponential,
                    // second-order approximation for declared slow detuning.
                    tx_state = decay * tx_state + alpha / pole * (Complex{1, 0} - decay) * input;
                }
                require(finite(tx_state), "resonator state became nonfinite");
            }
            history[next_source % history.size()] = filtered;
            ++next_source;
        }
    }

    Complex saved(std::int64_t index) const {
        if (index < 0) return {};
        const auto positive = static_cast<std::uint64_t>(index);
        if (positive >= next_source || next_source - positive > history.size())
            throw std::logic_error("fractional-delay source history invariant violated");
        return history[positive % history.size()];
    }

    Complex endpoint(std::size_t group, std::size_t phase, std::int64_t base, std::uint64_t generation) {
        auto& cached = delay_groups[group].endpoints[phase];
        if (cached.generation != generation || cached.base != base) {
            const std::size_t width = 2 * spec.interpolation_half_width;
            const auto* weights = interpolation_table.data() + phase * width;
            Complex value{};
            for (std::size_t k = 0; k < width; ++k)
                value += weights[k] * saved(base + static_cast<std::int64_t>(k) + 1 - spec.interpolation_half_width);
            cached = {generation, base, value};
        }
        return cached.value;
    }

    Complex delayed(RuntimePath& path, std::uint64_t n, double delay_motion_seconds = 0, std::int64_t source_offset = 0) {
        // The static integer fast path and its address must use the same
        // rounded product; otherwise e.g. .2*2000 can be 400 in binary64 yet
        // slightly larger in extended precision, moving floor() back one row.
        const auto& spec_path = *path.spec;
        const long double delay_samples = spec_path.delay_rate == 0 ?
            static_cast<long double>(spec_path.delay_seconds * fs) :
            static_cast<long double>(spec_path.delay_seconds) * fs;
        const long double coordinate = static_cast<long double>(n) * (1.L - spec_path.delay_rate) - delay_samples -
            static_cast<long double>(delay_motion_seconds) * fs + source_offset;
        const auto floor = std::floor(coordinate);
        const auto base = static_cast<std::int64_t>(floor);
        const double fraction = static_cast<double>(coordinate - floor);
        const auto half = static_cast<std::int64_t>(spec.interpolation_half_width);
        // The complete 2*half stencil is [base+1-half, base+half]. Before
        // sample zero every source/filter value is exactly zero. After the
        // finite input, this also holds when no transmitter filter can retain
        // signal energy. Equality at either boundary must still be rendered.
        // Carrier/fading advancement and receiver noise remain in render().
        if (base + half < 0) return {};
        const auto first = base + 1 - half;
        if (!spec.transmitter.enabled && first >= 0 &&
            static_cast<std::uint64_t>(first) >= source.geometry.source_samples) return {};
        if (path.integer_delay || fraction == 0) return saved(base);
        const std::size_t width = 2 * spec.interpolation_half_width;
        const double* first_weights = path.static_weights.data();
        const double* second = first_weights;
        double blend = 0;
        if (path.moving) {
            const double u = fraction * fractional_intervals;
            const auto i = std::min(static_cast<std::size_t>(u), fractional_intervals - 1);
            blend = u - static_cast<double>(i);
            if (path.delay_group != std::numeric_limits<std::size_t>::max()) {
                const auto lower = endpoint(path.delay_group, i, base, static_cast<std::uint64_t>(n));
                const auto upper = endpoint(path.delay_group, i + 1, base, static_cast<std::uint64_t>(n));
                // Same two table rows and the same fraction as the direct
                // weighted sum below. Only floating-point summation order
                // differs, verified by a many-cell direct comparison.
                return lower + blend * (upper - lower);
            }
            first_weights = interpolation_table.data() + i * width;
            second = first_weights + width;
        }
        Complex value{};
        for (std::size_t k = 0; k < width; ++k) {
            const double weight = first_weights[k] + blend * (second[k] - first_weights[k]);
            value += weight * saved(base + static_cast<std::int64_t>(k) + 1 - spec.interpolation_half_width);
        }
        return value;
    }

    Complex hf_delayed(RuntimePath& path, std::uint64_t n, double motion, double slope) {
        const auto center = delayed(path, n, motion);
        if (slope == 0) return center;
        // The symmetric stencil differentiates the source coordinate at the
        // current physical path delay, with the output-time motion held fixed.
        // The calibrated transfer is 1 + i D/(4π) * d²/dt², i.e. the local
        // first-order approximation to exp(-iπ D f²).
        constexpr auto& weights = detail::hf_second_derivative_weights;
        Complex curvature = weights.front() * center;
        for (std::size_t offset = 1; offset < weights.size(); ++offset) {
            const auto sample_offset = static_cast<std::int64_t>(offset);
            curvature += weights[offset] * (delayed(path, n, motion, sample_offset) +
                                             delayed(path, n, motion, -sample_offset));
        }
        return center + Complex{0, slope * fs * fs / (4 * pi)} * curvature;
    }

    Complex render() {
        const auto n = report.samples;
        const long double time = static_cast<long double>(n) / fs;
        ensure_source(n + spec.interpolation_half_width);
        Complex wanted = lunar ? lunar->wanted(n, history, source.geometry.source_samples, spec.transmitter.enabled) : Complex{};
        for (auto& path : paths) {
            const auto& spec_path = *path.spec;
            if ((n & 4095) == 0) {
                const long double cycles = spec_path.phase_radians / (2 * std::numbers::pi_v<long double>) +
                    time * (spec_path.residual_doppler_hz - static_cast<long double>(spec.rf_hz) * spec_path.delay_rate) +
                    .5L * spec_path.residual_drift_hz_s * time * time;
                path.phase = std::polar(1., static_cast<double>(2 * std::numbers::pi_v<long double> * std::remainder(cycles, 1.L)));
                const long double next_cycles = (spec_path.residual_doppler_hz - static_cast<long double>(spec.rf_hz) * spec_path.delay_rate +
                    spec_path.residual_drift_hz_s * time) / fs + .5L * spec_path.residual_drift_hz_s / (fs * fs);
                path.phase_step = std::polar(1., static_cast<double>(2 * std::numbers::pi_v<long double> * std::remainder(next_cycles, 1.L)));
            }
            if (spec_path.power > 0) {
                const Complex fading = path.gaussian ? path.gaussian->value(time) : Complex{1, 0};
                if (spec_path.hf) {
                    const auto& hf = *spec_path.hf;
                    const auto& movement = hf.group_delay_motion;
                    double motion = hf_group_delay_motion_seconds(movement, time);
                    double phase = hf_geometric_phase_radians(movement, spec.rf_hz, time);
                    double dispersion = hf.group_delay_slope_seconds_per_hz;
                    if (hf.parabolic_layer) {
                        const auto next = hf.parabolic_layer->critical_amplitude_hz == 0 ? path.eikonal_initial :
                            hf_parabolic_eikonal(*hf.parabolic_layer, spec.rf_hz, time);
                        motion += next.group_delay_seconds - path.eikonal_initial.group_delay_seconds;
                        phase -= 2 * pi * std::remainder(spec.rf_hz *
                            (next.phase_delay_seconds - path.eikonal_initial.phase_delay_seconds), 1.);
                        dispersion = next.group_delay_slope_seconds_per_hz;
                    }
                    const double residual = spec_path.phase_wander_amplitude_radians > 0 ?
                        spec_path.phase_wander_amplitude_radians * static_cast<double>(std::sin(2 * std::numbers::pi_v<long double> *
                            std::remainder(time / spec_path.phase_wander_period_seconds, 1.L))) : 0;
                    const auto port = hf.measured_envelope ? hf_replay_gain(*hf.measured_envelope, time + hf.measured_offset_seconds) :
                        hf.polarization ? hf_port_projection(*hf.polarization, time) : Complex{1, 0};
                    wanted += path.fixed * path.phase * (path.coherent + path.diffuse_scale * fading) *
                        hf_mode_voltage(hf, time) * port * std::polar(1., phase + residual) *
                        hf_delayed(path, n, motion, dispersion);
                } else if (spec_path.phase_wander_amplitude_radians > 0) {
                    const long double angle = 2 * std::numbers::pi_v<long double> *
                        std::remainder(time / spec_path.phase_wander_period_seconds, 1.L);
                    const double phase = spec_path.phase_wander_amplitude_radians * static_cast<double>(std::sin(angle));
                    // Evaluate the separate residual phase at physical time.
                    // Do not alter group delay or add a second Doppler law.
                    wanted += path.fixed * path.phase * fading * std::polar(1., phase) * delayed(path, n);
                } else {
                    // Preserve the established OFF arithmetic exactly.
                    wanted += path.fixed * path.phase * fading * delayed(path, n);
                }
            }
            path.phase *= path.phase_step;
            path.phase_step *= path.step_change;
        }
        wanted *= fade_gain(spec.fade, static_cast<double>(time));
        report.signal_energy += static_cast<long double>(std::norm(wanted));
        const auto& noise = spec.noise;
        Complex additive{};
        if (noise.white_variance > 0) additive += std::sqrt(noise.white_variance) * white.proper();
        if (noise.colored_variance > 0) {
            additive += colored_state;
            colored_state = colored_decay * colored_state + colored_innovation * colored.proper();
        }
        if (noise.impulse_rate_hz > 0 && noise.impulse_rms > 0) {
            // Samples are at n/Fs; include arrivals since the previous sample.
            if (n != 0) impulse_state *= impulse_decay;
            while (next_event <= static_cast<double>(time)) {
                impulse_state += noise.impulse_rms * marks.proper() *
                    std::exp(-(static_cast<double>(time) - next_event) / noise.impulse_decay_seconds);
                ++report.impulse_events;
                next_event += -std::log(events.uniform()) / noise.impulse_rate_hz;
            }
            additive += impulse_state;
        }
        if (noise.tone_amplitude > 0) {
            const long double cycles = noise.tone_hz * time + .5L * noise.tone_sweep_hz_s * time * time;
            additive += std::polar(noise.tone_amplitude,
                static_cast<double>(2 * std::numbers::pi_v<long double> * std::remainder(cycles, 1.L)));
        }
        report.additive_noise_energy += static_cast<long double>(std::norm(additive));
        Complex observed = wanted + additive;
        if (spec.frontend.lowpass_hz > 0) {
            lowpass_state = lowpass_decay * lowpass_state + (1 - lowpass_decay) * observed;
            observed = lowpass_state;
        }
        if (spec.frontend.agc_target > 0) {
            // Explicit synthetic attenuation-only, instantaneous-envelope AGC.
            // It acts on signal + noise + blockers and retains release state.
            const double desired = spec.frontend.agc_target / std::max(spec.frontend.agc_target, std::abs(observed));
            const double decay = desired < gain ? attack_decay : release_decay;
            gain = decay * gain + (1 - decay) * desired;
            report.minimum_agc_gain = std::min(report.minimum_agc_gain, gain);
            observed *= gain;
        }
        if (spec.frontend.clip_magnitude > 0 && std::abs(observed) > spec.frontend.clip_magnitude) {
            observed *= spec.frontend.clip_magnitude / std::abs(observed);
            ++report.clipped_samples;
        }
        if (spec.frontend.adc_bits > 0) {
            const double full = spec.frontend.clip_magnitude;
            const double step = 2 * full / std::ldexp(1., static_cast<int>(spec.frontend.adc_bits));
            const auto quantize = [full, step](double x) {
                return std::clamp(std::round(x / step) * step, -full, full - step);
            };
            observed = {quantize(observed.real()), quantize(observed.imag())};
        }
        require(finite(observed) && std::isfinite(std::norm(observed)), "channel output or sample energy overflow");
        report.output_energy += static_cast<long double>(std::norm(observed));
        ++report.samples;
        return observed;
    }
};

Channel::Channel(Source source, ChannelSpec spec, std::uint64_t seed, std::uint64_t horizon_samples, std::stop_token stop)
    : impl_(std::make_unique<Impl>(std::move(source), std::move(spec), seed, horizon_samples, stop)) {}
Channel::~Channel() = default;
Channel::Channel(Channel&&) noexcept = default;
Channel& Channel::operator=(Channel&&) noexcept = default;
std::size_t Channel::read(std::span<Complex> output, std::stop_token stop) {
    if (!impl_) throw std::logic_error("cannot read a moved-from channel");
    const auto check_cancelled = [&] {
        if (stop.stop_requested()) throw std::runtime_error("channel rendering cancelled");
    };
    check_cancelled();
    const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(output.size(), impl_->horizon - impl_->report.samples));
    for (std::size_t i = 0; i < count; ++i) {
        if ((i & 63) == 0) check_cancelled();
        output[i] = impl_->render();
    }
    check_cancelled();
    return count;
}
const ChannelReport& Channel::report() const {
    if (!impl_) throw std::logic_error("cannot report a moved-from channel");
    return impl_->report;
}
}
