#include "datapump/fast/simulation.hpp"
#include "datapump/fast/modem.hpp"
#include "datapump/resampler.hpp"
#include "../channel_perturbation.hpp"
#include "../channel_spread.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <numbers>
#include <random>
#include <sstream>
#include <utility>
#include <vector>

namespace datapump::fast {
namespace {

using Complex = std::complex<double>;
using modem::detail::DispersiveSelectivePerturbation;
using modem::detail::SpreadRay;
using triad::v3::detail::FixedGaussianRay;
constexpr double tau = 2 * std::numbers::pi;
constexpr std::size_t chunk_samples = 1024;

bool in_range(double value, double lo, double hi) {
    return std::isfinite(value) && value >= lo && value <= hi;
}

double relative_shift_ppm(const ChannelAssumptions& settings) {
    if (settings.shift_hz == 0) return 0;
    return settings.shared_reference ? settings.baseband_ppm : settings.shift_ppm;
}

double selective_rate(const ChannelAssumptions& settings) {
    if (settings.selective_slow_depth == 0 && settings.selective_fast_depth == 0) return 0;
    return settings.doppler_hz > 0 ? settings.doppler_hz * settings.margin : .1;
}

double propagation_frequency_allowance(const SimulationSettings& settings) {
    const double spread_factor = settings.spread_evolution == SpreadEvolution::gaussian ? 3. : 1.;
    return spread_factor * settings.doppler_hz * settings.margin + selective_rate(channel_assumptions(settings));
}

double relative_clock_bound(const ChannelAssumptions& settings) {
    return settings.baseband_ppm / (1 - settings.baseband_ppm * .5e-6);
}

std::uint64_t random_seed() {
    std::random_device source;
    const auto high = static_cast<std::uint64_t>(source()) & 0xffffffffULL;
    const auto low = static_cast<std::uint64_t>(source()) & 0xffffffffULL;
    return (high << 32) | low;
}

} // namespace

ChannelAssumptions channel_assumptions(const SimulationSettings& settings) noexcept {
    return {settings.baseband_ppm, settings.baseband_phase_rad_sqrt_s, settings.shift_ppm,
        settings.shift_phase_rad_sqrt_s, settings.shared_reference, settings.shift_hz, settings.delay_ms,
        settings.doppler_hz, settings.selective_slow_depth, settings.selective_fast_depth, settings.margin};
}

void validate(const ChannelAssumptions& settings) {
    // The shared adjustable resampler supports at most 1000 ppm of relative
    // rate error. The per-end ratio remains within that bound at 999 ppm.
    if (!in_range(settings.baseband_ppm, 0, 999) ||
        !in_range(settings.shift_ppm, 0, 1000) ||
        !in_range(settings.baseband_phase_rad_sqrt_s, 0, 100) ||
        !in_range(settings.shift_phase_rad_sqrt_s, 0, 100) ||
        !in_range(settings.shift_hz, 0, 100e9) ||
        !in_range(settings.delay_ms, 0, 1000) ||
        !in_range(settings.doppler_hz, 0, 1000) ||
        !in_range(settings.selective_slow_depth, 0, 1) ||
        !in_range(settings.selective_fast_depth, 0, 1) ||
        !in_range(settings.margin, 1, 3))
        throw Error("Fast channel assumptions are outside the supported ranges");
}

void validate(const Profile& profile, const SimulationSettings& settings, ChannelPurpose purpose) {
    validate(profile);
    const auto assumptions=channel_assumptions(settings);
    validate(assumptions);
    if (settings.spread_evolution != SpreadEvolution::fixed && settings.spread_evolution != SpreadEvolution::gaussian)
        throw Error("Unknown Fast spread evolution model");
    if (purpose == ChannelPurpose::receiver_simulation &&
        (!in_range(settings.transmit_watts, 1e-9, 1e9) ||
         !in_range(settings.path_loss_db, 0, 300) ||
         (settings.noise_dbm_hz && !in_range(*settings.noise_dbm_hz, -250, 0))))
        throw Error("Fast simulated link budget is outside the supported ranges");

    const double clock = relative_clock_bound(assumptions) * 1e-6;
    const double shift = settings.shift_hz * relative_shift_ppm(assumptions) * 1e-6 /
        (1 - settings.baseband_ppm * .5e-6);
    const double spread = propagation_frequency_allowance(settings);
    const auto low = occupied_lower_hz(profile) * (1 - clock);
    const auto high = occupied_upper_hz(profile) * (1 + clock);
    if (low <= shift + spread || high + shift + spread >= profile.sample_rate / 2.) {
        std::ostringstream message;
        message.imbue(std::locale::classic());
        message << std::fixed << std::setprecision(1)
            << "Fast simulation bounds need " << low - shift - spread << ".." << high + shift + spread
            << " Hz; sampled audio allows 0.." << profile.sample_rate / 2.
            << " Hz (RF oscillator +/-" << shift << " Hz; channel allowance +/-" << spread << " Hz)";
        throw Error(message.str());
    }
}

struct SimulationChannel::Impl {
    Source source;
    SimulationReport report;
    std::array<std::complex<float>, chunk_samples> analytic{};
    std::array<float, chunk_samples> in_real{}, in_imag{}, out_real{}, out_imag{};
    std::optional<audio::Resampler> real_resampler, imag_resampler;
    std::size_t in_position = 0, in_count = 0, wave_position = 0, wave_count = 0;
    std::size_t resampler_padding = 0;
    bool source_ended = false;

    std::optional<SpreadRay> ray;
    std::optional<FixedGaussianRay> gaussian_direct, gaussian_delayed;
    double physical_sample_rate = 1;
    std::optional<DispersiveSelectivePerturbation> selective;
    std::vector<Complex> delay_history;
    std::array<std::size_t, 4> delay_lags{};
    std::array<double, 4> delay_coefficients{};
    std::size_t delay_taps = 0, delay_cursor = 0;
    std::uint64_t tail_remaining = 0, quiet_remaining = 0;

    std::mt19937_64 phase_rng, noise_rng;
    std::normal_distribution<double> phase_normal, noise_normal;
    Complex rotation{1, 0}, rotation_step{1, 0};
    double phase_sigma = 0, noise_sigma = 0;
    bool rotate = false;

    Impl(const Profile& profile, const SimulationSettings& settings, Source read, ChannelPurpose purpose)
        : source(std::move(read)) {
        validate(profile, settings, purpose);
        const auto assumptions=channel_assumptions(settings);
        if (!source) throw Error("Fast simulation requires an emitted source");

        const auto sample_rate = profile.sample_rate;
        report.link_budget_applied = purpose == ChannelPurpose::receiver_simulation;
        report.seed = settings.seed ? *settings.seed : random_seed();
        report.spread_evolution = settings.spread_evolution;
        report.propagation_frequency_allowance_hz = propagation_frequency_allowance(settings);
        // These are the emitted PCM power references for unit-energy mapper
        // symbols. Receiver noise is normalized to this reference; link loss
        // and TX watts determine the receiver C/N0, not a pre-playback PCM gain.
        // Short source/marker mixes can have different actual RMS.
        report.nominal_signal_power = profile.acoustic_ofdm
            ? std::pow(profile.amplitude / 4.5, 2)
            : .5 * profile.amplitude * profile.amplitude;
        if (report.link_budget_applied) {
            report.received_power_dbm = 30 + 10 * std::log10(settings.transmit_watts) - settings.path_loss_db;
        }
        if (report.link_budget_applied && settings.noise_dbm_hz) {
            report.cn0_db_hz = report.received_power_dbm - *settings.noise_dbm_hz;
            report.noise_variance = report.nominal_signal_power * (.5 * sample_rate) *
                std::pow(10., -*report.cn0_db_hz / 10);
            noise_sigma = std::sqrt(report.noise_variance);
        }

        std::mt19937_64 clock_rng(report.seed ^ 0x23797a38a4b61f5dULL);
        std::mt19937_64 shift_rng(report.seed ^ 0x5c129ad6978b43f2ULL);
        phase_rng.seed(report.seed ^ 0x7913d1cf8d88cf5bULL);
        noise_rng.seed(report.seed ^ 0xc1c765c241487c89ULL);
        const auto endpoint = [](double ppm, std::mt19937_64& rng) {
            return ppm == 0 ? 0. : std::uniform_real_distribution<double>(-ppm * .5, ppm * .5)(rng);
        };
        const auto tx_clock = endpoint(settings.baseband_ppm, clock_rng);
        const auto rx_clock = endpoint(settings.baseband_ppm, clock_rng);
        const auto rx_scale = 1 + rx_clock * 1e-6;
        const auto clock = (tx_clock - rx_clock) / rx_scale;
        report.sample_clock_error_ppm = clock;
        report.clock_bound_ppm = relative_clock_bound(assumptions) * settings.margin;
        if (clock != 0) {
            real_resampler.emplace(sample_rate, sample_rate, true);
            imag_resampler.emplace(sample_rate, sample_rate, true);
            real_resampler->initialize_timing(0, clock * 1e-6);
            imag_resampler->initialize_timing(0, clock * 1e-6);
            resampler_padding = static_cast<std::size_t>(
                std::ceil(real_resampler->source_lookahead_seconds() * sample_rate));
        }

        const double shift_ppm = relative_shift_ppm(assumptions);
        if (settings.shift_hz != 0) {
            double relative = tx_clock - rx_clock;
            if (!settings.shared_reference) {
                const auto tx_shift = endpoint(shift_ppm, shift_rng);
                const auto rx_shift = endpoint(shift_ppm, shift_rng);
                relative = tx_shift - rx_shift;
            }
            report.shift_error_hz = settings.shift_hz * relative * 1e-6 / rx_scale;
        }
        const double audio_center = (occupied_lower_hz(profile) + occupied_upper_hz(profile)) * .5;
        report.frequency_bound_hz = (audio_center * settings.baseband_ppm + settings.shift_hz * shift_ppm) *
            1e-6 * settings.margin / (1 - settings.baseband_ppm * .5e-6);
        const auto shift_diffusion = settings.shift_hz == 0 || settings.shared_reference
            ? 0. : settings.shift_phase_rad_sqrt_s;
        const auto phase_diffusion = std::hypot(settings.baseband_phase_rad_sqrt_s, shift_diffusion);
        physical_sample_rate = sample_rate * rx_scale;
        phase_sigma = phase_diffusion / std::sqrt(physical_sample_rate);
        rotate = phase_sigma != 0 || report.shift_error_hz != 0;
        rotation_step = std::polar(1., tau * report.shift_error_hz / sample_rate);

        report.spread_delay_ms = settings.delay_ms * settings.margin;
        report.spread_doppler_hz = settings.doppler_hz * settings.margin;
        if (report.spread_delay_ms > 0 || report.spread_doppler_hz > 0) {
            if (settings.spread_evolution == SpreadEvolution::gaussian && report.spread_doppler_hz > 0) {
                report.gaussian_doppler_width_hz = report.spread_doppler_hz;
                gaussian_direct.emplace(report.gaussian_doppler_width_hz, report.seed ^ 0x4741555353444952ULL);
                gaussian_delayed.emplace(report.gaussian_doppler_width_hz, report.seed ^ 0x474155535344454cULL);
            } else {
                ray.emplace(physical_sample_rate, report.spread_doppler_hz, report.seed ^ 0x325449415443484eULL);
            }
            const auto samples = report.spread_delay_ms * .001 * physical_sample_rate;
            const auto integral = static_cast<std::size_t>(std::floor(samples));
            if (samples == std::round(samples)) {
                delay_lags[0] = integral;
                delay_coefficients[0] = 1;
                delay_taps = 1;
            } else {
                // Causal cubic Lagrange approximation on four adjacent samples.
                // Near zero delay use the four earliest available lags.
                const auto first = integral > 0 ? integral - 1 : 0;
                delay_taps = delay_lags.size();
                for (std::size_t tap = 0; tap < delay_taps; ++tap) {
                    const auto lag = first + tap;
                    delay_lags[tap] = lag;
                    double coefficient = 1;
                    for (std::size_t other = 0; other < delay_taps; ++other) {
                        if (other == tap) continue;
                        const auto neighbor = static_cast<double>(first + other);
                        coefficient *= (samples - neighbor) / (static_cast<double>(lag) - neighbor);
                    }
                    delay_coefficients[tap] = coefficient;
                }
            }
            const auto last_lag = delay_lags[delay_taps - 1];
            delay_history.resize(last_lag + 1);
            tail_remaining = last_lag;
        }

        const double am_rate = selective_rate(assumptions);
        if (am_rate > 0) {
            // Keep the same synthetic frequency correlation when selecting a
            // narrower symbol rate or constellation inside this channel.
            report.selective_spacing_hz = occupied_bandwidth_hz(capacity_profile(profile.channel)) / 3;
            selective.emplace(physical_sample_rate, audio_center, report.selective_spacing_hz, am_rate,
                settings.selective_slow_depth, settings.selective_fast_depth, report.seed ^ 0x3254494154504552ULL);
            tail_remaining += DispersiveSelectivePerturbation::tail_samples(physical_sample_rate, report.selective_spacing_hz);
        }
        report.propagation_tail_samples = tail_remaining;
        quiet_remaining = end_silence_samples(profile);
    }

    void load_source() {
        in_position = 0;
        in_count = 0;
        if (source_ended) return;
        in_count = source(analytic);
        if (in_count > analytic.size() || report.source_samples > std::numeric_limits<std::uint64_t>::max() - in_count)
            throw Error("Fast simulation source returned an invalid sample extent");
        report.source_samples += in_count;
        for (const auto& sample : std::span(analytic).first(in_count)) {
            if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag()))
                throw Error("Fast simulation source returned a nonfinite sample");
        }
        if (in_count == 0) {
            source_ended = true;
            // Flush the actual resampling FIR before the propagation drain.
            in_count = resampler_padding;
            std::fill_n(analytic.begin(), in_count, std::complex<float>{});
        }
        if (real_resampler) {
            for (std::size_t i = 0; i < in_count; ++i) {
                in_real[i] = analytic[i].real();
                in_imag[i] = analytic[i].imag();
            }
        }
    }

    bool load_wave() {
        wave_position = 0;
        if (!real_resampler) {
            load_source();
            wave_count = in_count;
            return wave_count != 0;
        }
        while (!real_resampler->finished()) {
            if (in_position == in_count && !source_ended) load_source();
            const auto r = real_resampler->process(std::span(in_real).first(in_count).subspan(in_position),
                out_real, source_ended);
            const auto i = imag_resampler->process(std::span(in_imag).first(in_count).subspan(in_position),
                out_imag, source_ended);
            if (r.consumed != i.consumed || r.produced != i.produced)
                throw Error("Fast simulation resampler channels lost sample alignment");
            in_position += r.consumed;
            wave_count = r.produced;
            if (wave_count != 0) return true;
            if (!r.consumed && !real_resampler->finished())
                throw Error("Fast simulation resampler stopped progressing");
        }
        wave_count = 0;
        return false;
    }

    Complex propagate(Complex value) {
        if (ray || gaussian_direct) {
            delay_history[delay_cursor] = value;
            Complex delayed{};
            for (std::size_t tap = 0; tap < delay_taps; ++tap) {
                const auto lag = delay_lags[tap];
                const auto index = delay_cursor >= lag ? delay_cursor - lag : delay_history.size() - (lag - delay_cursor);
                delayed += delay_coefficients[tap] * delay_history[index];
            }
            if (++delay_cursor == delay_history.size()) delay_cursor = 0;
            if (gaussian_direct) {
                // Physical-time random fields remain continuous across caller
                // chunks and delayed/quiet tails. No window-wise gain or SNR
                // normalization removes their actual fades.
                const auto seconds = static_cast<long double>(report.output_samples) / physical_sample_rate;
                constexpr double gain = .7071067811865475244;
                value = gain * (gaussian_direct->value(seconds) * value + gaussian_delayed->value(seconds) * delayed);
            } else value = ray->process(value, delayed);
        }
        if (selective) value = selective->process(value);
        if (rotate) {
            value *= rotation;
            rotation *= rotation_step;
            if (phase_sigma != 0) rotation *= std::polar(1., phase_sigma * phase_normal(phase_rng));
            if ((report.output_samples & 4095) == 4095) rotation /= std::abs(rotation);
        }
        return value;
    }

    std::size_t read(std::span<float> output) {
        std::size_t written = 0;
        for (auto& sample : output) {
            Complex value{};
            if (wave_position != wave_count || load_wave()) {
                value = real_resampler ? Complex{out_real[wave_position], out_imag[wave_position]}
                                       : static_cast<Complex>(analytic[wave_position]);
                ++wave_position;
            } else if (tail_remaining != 0) {
                --tail_remaining;
            } else if (quiet_remaining != 0) {
                if (!report.quiet_start_sample) report.quiet_start_sample = report.output_samples;
                --quiet_remaining;
            } else {
                break;
            }
            const double pcm = propagate(value).real() + (noise_sigma == 0 ? 0. : noise_sigma * noise_normal(noise_rng));
            if (!in_range(pcm, -std::numeric_limits<float>::max(), std::numeric_limits<float>::max()))
                throw Error("Fast simulation produced nonfinite PCM");
            sample = static_cast<float>(pcm);
            if (report.output_samples == std::numeric_limits<std::uint64_t>::max())
                throw Error("Fast simulation output exceeds the sample counter range");
            ++report.output_samples;
            ++written;
        }
        return written;
    }

    std::size_t workspace_bytes() const {
        return sizeof(*this) + delay_history.capacity() * sizeof(Complex) +
            (selective ? selective->working_bytes() - sizeof(*selective) : 0) +
            (real_resampler ? real_resampler->workspace_bytes() + imag_resampler->workspace_bytes() -
                2 * sizeof(audio::Resampler) : 0);
    }
};

SimulationChannel::SimulationChannel(const Profile& profile, const SimulationSettings& settings, Source source,
                                     ChannelPurpose purpose)
    : impl_(std::make_unique<Impl>(profile, settings, std::move(source), purpose)) {}
SimulationChannel::~SimulationChannel() = default;
SimulationChannel::SimulationChannel(SimulationChannel&&) noexcept = default;
SimulationChannel& SimulationChannel::operator=(SimulationChannel&&) noexcept = default;
std::size_t SimulationChannel::read(std::span<float> output) { return impl_->read(output); }
const SimulationReport& SimulationChannel::report() const { return impl_->report; }
std::size_t SimulationChannel::workspace_bytes() const { return impl_->workspace_bytes(); }

} // namespace datapump::fast
