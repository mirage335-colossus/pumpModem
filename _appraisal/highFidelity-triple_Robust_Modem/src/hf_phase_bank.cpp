#include "appraisal/hf_phase_bank.hpp"
#include "hf_phase_bank_internal.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace appraisal {
namespace {
using Complex = datapump::triad::v3::Complex;
using WideComplex = std::complex<long double>;
using Clock = std::chrono::steady_clock;
constexpr std::array periods{90., 120., 180.};
constexpr std::array amplitudes{4., 8., 12., 16.};
constexpr std::size_t phase_count = 8;
constexpr std::size_t track_count = periods.size() * phase_count;
constexpr std::size_t dictionary_size = 1 + track_count * amplitudes.size();
constexpr long double two_pi = 2 * std::numbers::pi_v<long double>;
constexpr unsigned reset_interval = 1024;
constexpr std::uint64_t macs_per_active_row = 4 * dictionary_size;

constexpr std::array<HfPhaseBankModel, dictionary_size> dictionary() {
    std::array<HfPhaseBankModel, dictionary_size> result{};
    std::size_t index = 1;
    for (const auto period : periods)
        for (std::size_t phase = 0; phase < phase_count; ++phase)
            for (const auto amplitude : amplitudes)
                result[index++] = {amplitude, static_cast<double>(two_pi * phase / phase_count), period};
    return result;
}
constexpr auto models = dictionary();

void require(bool condition, const char* message) {
    if (!condition) throw std::invalid_argument(message);
}

void check_cancelled(std::stop_token stop) {
    if (stop.stop_requested()) throw std::runtime_error("HF phase bank cancelled");
}

void validate(std::span<const Complex> samples, std::stop_token stop) {
    for (std::size_t i = 0; i < samples.size(); ++i) {
        if ((i % reset_interval) == 0) check_cancelled(stop);
        require(std::isfinite(samples[i].real()) && std::isfinite(samples[i].imag()),
                "HF phase bank samples must be finite");
    }
}

WideComplex wide(Complex value) { return {value.real(), value.imag()}; }

struct AccumulateElapsed {
    double& seconds;
    const Clock::time_point begin = Clock::now();
    ~AccumulateElapsed() { seconds += std::chrono::duration<double>(Clock::now() - begin).count(); }
};

struct Dots {
    std::array<WideComplex, 2> noisy{}, noise_only{};
};

HfPhaseBankHypothesis report_hypothesis(const std::array<WideComplex, dictionary_size>& dots,
                                       long double energy, long double noise_variance) {
    std::array<long double, dictionary_size> scores{};
    const auto penalty = -std::log1p(energy / noise_variance);
    const auto scale = 1 / noise_variance / (noise_variance + energy);
    long double sum = 0;
    for (std::size_t i = 0; i < scores.size(); ++i) {
        scores[i] = penalty + scale * std::norm(dots[i]);
        require(std::isfinite(scores[i]) && std::abs(scores[i]) <= std::numeric_limits<double>::max(),
                "HF phase bank likelihood is outside the report range");
        sum += scores[i];
    }
    const auto peak = std::max_element(scores.begin(), scores.end());
    const auto mixture = detail::hf_phase_log_mean_exp(scores);
    const auto log_normalization = std::log(static_cast<long double>(dictionary_size));
    return {static_cast<double>(energy), static_cast<double>(scores[0]), static_cast<double>(mixture),
        static_cast<double>(*peak), static_cast<double>(sum / dictionary_size),
        static_cast<double>(std::exp(*peak - mixture - log_normalization)),
        models[static_cast<std::size_t>(peak - scores.begin())]};
}

HfPhaseBankObservation report_observation(const std::array<Dots, dictionary_size>& dots,
                                         const std::array<long double, 2>& energy,
                                         long double noise_variance, bool null) {
    HfPhaseBankObservation report;
    for (std::size_t bit = 0; bit < report.hypotheses.size(); ++bit) {
        std::array<WideComplex, dictionary_size> observations{};
        for (std::size_t i = 0; i < observations.size(); ++i)
            observations[i] = null ? dots[i].noise_only[bit] : dots[i].noisy[bit];
        report.hypotheses[bit] = report_hypothesis(observations, energy[bit], noise_variance);
    }
    const std::array<long double, 2> zero{report.hypotheses[0].zero_phase_log_likelihood_ratio,
        report.hypotheses[1].zero_phase_log_likelihood_ratio};
    const std::array<long double, 2> mixture{report.hypotheses[0].mixture_log_likelihood_ratio,
        report.hypotheses[1].mixture_log_likelihood_ratio};
    report.zero_phase_log_likelihood_ratio = static_cast<double>(detail::hf_phase_log_mean_exp(zero));
    const auto average = detail::hf_phase_log_mean_exp(mixture);
    report.mixture_log_likelihood_ratio = static_cast<double>(average);
    const auto difference = mixture[1] - mixture[0];
    const auto ratio = std::exp(-std::abs(difference));
    report.bit_one_probability_given_signal = static_cast<double>(difference >= 0 ? 1 / (1 + ratio) : ratio / (1 + ratio));
    return report;
}
}

long double detail::hf_phase_log_mean_exp(std::span<const long double> scores) {
    require(!scores.empty(), "cannot mix an empty phase family");
    const auto peak = *std::max_element(scores.begin(), scores.end());
    long double sum = 0;
    for (const auto score : scores) sum += std::exp(score - peak);
    return peak + std::log(sum / static_cast<long double>(scores.size()));
}

std::span<const HfPhaseBankModel> hf_phase_bank_models() { return models; }

struct HfPhaseBankDiagnostic::Impl {
    explicit Impl(HfPhaseBankConfig settings) : config(settings), next(settings.first_sample) {
        require(std::isfinite(config.sample_rate_hz) && config.sample_rate_hz > 0,
                "HF phase bank sample rate must be finite and positive");
        require(config.stop_sample > config.first_sample &&
                config.stop_sample - config.first_sample <= std::numeric_limits<std::uint64_t>::max() / macs_per_active_row,
                "HF phase bank aperture is empty or exceeds its work-count range");
        require(std::isfinite(config.white_noise_variance) && config.white_noise_variance > 0,
                "HF phase bank requires positive white-noise variance");
        for (std::size_t i = 0; i < periods.size(); ++i) {
            const long double cycles = 1 / (periods[i] * static_cast<long double>(config.sample_rate_hz));
            require(std::isfinite(cycles), "HF phase bank sample clock is outside the oscillator range");
            steps[i] = std::polar(1., static_cast<double>(two_pi * std::remainder(cycles, 1.L)));
            set_clock(i);
        }
        for (std::size_t phase = 0; phase < phase_count; ++phase)
            angles[phase] = std::polar(1., static_cast<double>(two_pi * phase / phase_count));
    }

    void set_clock(std::size_t i) {
        const long double cycle = std::remainder(static_cast<long double>(next) /
            (static_cast<long double>(config.sample_rate_hz) * periods[i]), 1.L);
        oscillators[i] = std::polar(1., static_cast<double>(two_pi * cycle));
    }

    void accumulate_row(Complex reference_zero, Complex reference_one, Complex noisy, Complex noise_only) {
        if (next % reset_interval == 0)
            for (std::size_t i = 0; i < periods.size(); ++i) set_clock(i);
        if (reference_zero != Complex{} || reference_one != Complex{}) {
            const std::array<WideComplex, 2> references{wide(reference_zero), wide(reference_one)};
            const auto wanted = wide(noisy);
            const auto null = wide(noise_only);
            Dots base;
            for (std::size_t bit = 0; bit < references.size(); ++bit) {
                energy[bit] += std::norm(references[bit]);
                const auto conjugate = std::conj(references[bit]);
                base.noisy[bit] = conjugate * wanted;
                base.noise_only[bit] = conjugate * null;
                dots.front().noisy[bit] += base.noisy[bit];
                dots.front().noise_only[bit] += base.noise_only[bit];
            }
            std::size_t model = 1;
            for (const auto oscillator : oscillators) {
                for (const auto theta : angles) {
                    const double sinusoid = (oscillator * theta).imag();
                    const auto unit = std::polar(1., -amplitudes.front() * sinusoid);
                    auto rotation = unit;
                    for (std::size_t amplitude = 0; amplitude < amplitudes.size(); ++amplitude) {
                        const auto wide_rotation = wide(rotation);
                        for (std::size_t bit = 0; bit < references.size(); ++bit) {
                            dots[model].noisy[bit] += base.noisy[bit] * wide_rotation;
                            dots[model].noise_only[bit] += base.noise_only[bit] * wide_rotation;
                        }
                        rotation *= unit;
                        ++model;
                    }
                }
            }
            ++active_rows;
        }
        for (std::size_t i = 0; i < periods.size(); ++i) oscillators[i] *= steps[i];
        ++next;
    }

    HfPhaseBankConfig config;
    std::uint64_t next, active_rows = 0;
    std::array<long double, 2> energy{};
    std::array<Dots, dictionary_size> dots{};
    std::array<Complex, periods.size()> oscillators{}, steps{};
    std::array<Complex, phase_count> angles{};
    HfPhaseBankReport report;
    double elapsed_seconds = 0;
    bool finished = false;
};

std::size_t hf_phase_bank_workspace_bytes() {
    return sizeof(HfPhaseBankDiagnostic::Impl) + dictionary_size * (sizeof(WideComplex) + sizeof(long double));
}

HfPhaseBankDiagnostic::HfPhaseBankDiagnostic(HfPhaseBankConfig config) : impl_(std::make_unique<Impl>(config)) {}
HfPhaseBankDiagnostic::~HfPhaseBankDiagnostic() = default;
HfPhaseBankDiagnostic::HfPhaseBankDiagnostic(HfPhaseBankDiagnostic&&) noexcept = default;
HfPhaseBankDiagnostic& HfPhaseBankDiagnostic::operator=(HfPhaseBankDiagnostic&&) noexcept = default;

void HfPhaseBankDiagnostic::append(std::uint64_t first_sample, std::span<const Complex> reference_zero,
                                   std::span<const Complex> reference_one, std::span<const Complex> noisy,
                                   std::span<const Complex> noise_only, std::stop_token stop) {
    require(impl_ && !impl_->finished, "HF phase bank accumulator is unavailable or sealed");
    auto& state = *impl_;
    require(first_sample == state.next && reference_zero.size() <= state.config.stop_sample - state.next,
            "HF phase bank input must be contiguous inside the public aperture");
    require(reference_zero.size() == reference_one.size() && reference_zero.size() == noisy.size() &&
            reference_zero.size() == noise_only.size(), "HF phase bank input spans must have identical extents");
    const AccumulateElapsed timer{state.elapsed_seconds};
    validate(reference_zero, stop); validate(reference_one, stop); validate(noisy, stop); validate(noise_only, stop);
    for (std::size_t i = 0; i < reference_zero.size(); ++i) {
        if ((i & 63) == 0) check_cancelled(stop);
        state.accumulate_row(reference_zero[i], reference_one[i], noisy[i], noise_only[i]);
    }
    check_cancelled(stop);
}

std::uint64_t HfPhaseBankDiagnostic::next_sample() const {
    require(impl_ != nullptr, "HF phase bank accumulator was moved from");
    return impl_->next;
}

HfPhaseBankReport HfPhaseBankDiagnostic::finish() {
    require(impl_ != nullptr, "HF phase bank accumulator was moved from");
    auto& state = *impl_;
    if (state.finished) return state.report;
    require(state.next == state.config.stop_sample, "HF phase bank requires the complete public aperture");
    const auto begin = Clock::now();
    auto& report = state.report;
    report.config = state.config;
    report.samples = state.next - state.config.first_sample;
    report.active_rows = state.active_rows;
    report.complex_multiply_accumulates = state.active_rows * macs_per_active_row;
    report.workspace_bytes = hf_phase_bank_workspace_bytes();
    report.model_count = dictionary_size;
    report.wanted_observation = report_observation(state.dots, state.energy, state.config.white_noise_variance, false);
    report.noise_only_observation = report_observation(state.dots, state.energy, state.config.white_noise_variance, true);
    report.evaluator_wall_seconds = state.elapsed_seconds + std::chrono::duration<double>(Clock::now() - begin).count();
    state.finished = true;
    return report;
}
}
