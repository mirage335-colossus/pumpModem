#include "appraisal/hf_history.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <limits>
#include <stdexcept>
#include <utility>

namespace appraisal {
namespace {
using Complex = datapump::triad::v3::Complex;
using WideComplex = std::complex<long double>;
using Clock = std::chrono::steady_clock;

void require(bool condition, const char* message) {
    if (!condition) throw std::invalid_argument(message);
}
WideComplex wide(Complex value) { return {value.real(), value.imag()}; }
void finite(std::span<const Complex> values) {
    for (const auto value : values)
        require(std::isfinite(value.real()) && std::isfinite(value.imag()),
                "history diagnostic requires finite samples");
}
struct Projection {
    long double reference_energy = 0;
    WideComplex signal_dot{}, noisy_dot{}, noise_dot{};
};
struct Totals {
    std::uint64_t rank = 0;
    long double reference_energy = 0, signal_projection = 0;
    long double noisy_projection = 0, noise_projection = 0;
    long double noisy_log_lr = 0, noise_log_lr = 0;
};
struct Partition {
    std::uint64_t samples_per_block = 0, in_block = 0, block_count = 0;
    std::array<Projection, 2> current;
    std::array<Totals, 2> totals;
};
long double likelihood(long double projected_energy, long double reference_energy,
                       long double noise_variance) {
    // rho=1 is predeclared. Expressing the quadratic term through projected
    // energy avoids squaring tiny or large noise variances in the denominator.
    const auto ratio = reference_energy / noise_variance;
    const auto shrinkage = reference_energy / (noise_variance + reference_energy);
    return -std::log1p(ratio) + shrinkage * (projected_energy / noise_variance);
}
void flush(Partition& partition, const HfHistoryConfig& config) {
    if (!partition.in_block) return;
    ++partition.block_count;
    for (std::size_t bit = 0; bit < 2; ++bit) {
        auto& sum = partition.totals[bit];
        const auto& block = partition.current[bit];
        sum.reference_energy += block.reference_energy;
        if (block.reference_energy > 0) {
            ++sum.rank;
            sum.signal_projection += std::norm(block.signal_dot) / block.reference_energy;
            if (config.include_noisy) {
                const auto projection = std::norm(block.noisy_dot) / block.reference_energy;
                sum.noisy_projection += projection;
                sum.noisy_log_lr += likelihood(projection, block.reference_energy,
                                               config.white_noise_variance);
            }
            if (config.include_noise_only) {
                const auto projection = std::norm(block.noise_dot) / block.reference_energy;
                sum.noise_projection += projection;
                sum.noise_log_lr += likelihood(projection, block.reference_energy,
                                               config.white_noise_variance);
            }
        }
    }
    partition.current = {};
    partition.in_block = 0;
}
} // namespace

struct HfHistoryDiagnostic::Impl {
    explicit Impl(HfHistoryConfig settings) : config(settings), next(settings.first_sample) {
        require(std::isfinite(config.sample_rate_hz) && config.sample_rate_hz > 0,
                "history diagnostic sample rate must be finite and positive");
        require(config.stop_sample > config.first_sample,
                "history diagnostic requires a nonempty aperture");
        require(config.wanted_bit < 2, "history diagnostic wanted bit must be zero or one");
        require(std::isfinite(config.white_noise_variance) && config.white_noise_variance >= 0,
                "history diagnostic noise variance must be finite and nonnegative");
        require(!(config.include_noisy || config.include_noise_only) || config.white_noise_variance > 0,
                "history diagnostic likelihood needs positive white-noise variance");
        const std::array<double, 4> lengths{.25, 1, 4, 16};
        for (std::size_t i = 0; i < lengths.size(); ++i) {
            const auto samples = std::round(static_cast<long double>(lengths[i]) * config.sample_rate_hz);
            require(samples < static_cast<long double>(std::numeric_limits<std::uint64_t>::max()),
                    "history diagnostic block sample count exceeds integer range");
            partitions[i].samples_per_block = static_cast<std::uint64_t>(std::max(1.L, samples));
            report.blocks[i].requested_block_seconds = lengths[i];
        }
        partitions.back().samples_per_block = config.stop_sample - config.first_sample;
        report.config = config;
    }
    HfHistoryConfig config;
    std::uint64_t next;
    std::array<Partition, 5> partitions;
    long double signal_energy = 0, noisy_energy = 0, noise_energy = 0;
    double elapsed_seconds = 0;
    HfHistoryReport report;
    bool finished = false;
};

HfHistoryDiagnostic::HfHistoryDiagnostic(HfHistoryConfig config)
    : impl_(std::make_unique<Impl>(config)) {}
HfHistoryDiagnostic::~HfHistoryDiagnostic() = default;
HfHistoryDiagnostic::HfHistoryDiagnostic(HfHistoryDiagnostic&&) noexcept = default;
HfHistoryDiagnostic& HfHistoryDiagnostic::operator=(HfHistoryDiagnostic&&) noexcept = default;

void HfHistoryDiagnostic::append(std::uint64_t first_sample,
                                 std::span<const Complex> reference_zero,
                                 std::span<const Complex> reference_one,
                                 std::span<const Complex> noise_free,
                                 std::span<const Complex> noisy,
                                 std::span<const Complex> noise_only) {
    require(impl_ != nullptr, "history diagnostic was moved from");
    auto& state = *impl_;
    const auto begin = Clock::now();
    require(!state.finished, "history diagnostic is already finished");
    require(first_sample == state.next, "history diagnostic input is not contiguous");
    require(reference_zero.size() == reference_one.size() && reference_zero.size() == noise_free.size(),
            "history diagnostic template and signal spans have different lengths");
    require(noise_free.size() <= state.config.stop_sample - state.next,
            "history diagnostic input exceeds the public aperture");
    require(state.config.include_noisy ? noisy.size() == noise_free.size() : noisy.empty(),
            "history diagnostic noisy span does not match its configuration");
    require(state.config.include_noise_only ? noise_only.size() == noise_free.size() : noise_only.empty(),
            "history diagnostic noise-only span does not match its configuration");
    // Validation before state changes makes a rejected call safe to correct.
    finite(reference_zero); finite(reference_one); finite(noise_free); finite(noisy); finite(noise_only);
    for (std::size_t i = 0; i < noise_free.size(); ++i) {
        const std::array<WideComplex, 2> templates{wide(reference_zero[i]), wide(reference_one[i])};
        const auto signal = wide(noise_free[i]);
        const auto observed = state.config.include_noisy ? wide(noisy[i]) : WideComplex{};
        const auto noise = state.config.include_noise_only ? wide(noise_only[i]) : WideComplex{};
        state.signal_energy += std::norm(signal);
        if (state.config.include_noisy) state.noisy_energy += std::norm(observed);
        if (state.config.include_noise_only) state.noise_energy += std::norm(noise);
        for (auto& partition : state.partitions) {
            for (std::size_t bit = 0; bit < 2; ++bit) {
                auto& sum = partition.current[bit];
                sum.reference_energy += std::norm(templates[bit]);
                const auto conjugate = std::conj(templates[bit]);
                sum.signal_dot += conjugate * signal;
                if (state.config.include_noisy) sum.noisy_dot += conjugate * observed;
                if (state.config.include_noise_only) sum.noise_dot += conjugate * noise;
            }
            if (++partition.in_block == partition.samples_per_block) flush(partition, state.config);
        }
    }
    state.next += noise_free.size();
    state.elapsed_seconds += std::chrono::duration<double>(Clock::now() - begin).count();
}

HfHistoryReport HfHistoryDiagnostic::finish() {
    require(impl_ != nullptr, "history diagnostic was moved from");
    auto& state = *impl_;
    if (state.finished) return state.report;
    const auto begin = Clock::now();
    require(state.next == state.config.stop_sample,
            "history diagnostic is missing part of the public aperture");
    auto& report = state.report;
    report.samples = state.config.stop_sample - state.config.first_sample;
    report.noise_free_energy = static_cast<double>(state.signal_energy);
    report.noise_free_energy_seconds = static_cast<double>(state.signal_energy / state.config.sample_rate_hz);
    if (state.config.include_noisy) report.noisy_energy = static_cast<double>(state.noisy_energy);
    if (state.config.include_noise_only) report.noise_only_energy = static_cast<double>(state.noise_energy);
    for (std::size_t i = 0; i < state.partitions.size(); ++i) {
        auto& partition = state.partitions[i];
        flush(partition, state.config);
        auto& output = report.blocks[i];
        output.block_samples = partition.samples_per_block;
        output.block_count = partition.block_count;
        output.actual_block_seconds = static_cast<double>(partition.samples_per_block) / state.config.sample_rate_hz;
        for (std::size_t bit = 0; bit < 2; ++bit) {
            const auto& sum = partition.totals[bit];
            auto& hypothesis = output.hypotheses[bit];
            hypothesis.rank = sum.rank;
            hypothesis.reference_energy = static_cast<double>(sum.reference_energy);
            hypothesis.projected_signal_energy = static_cast<double>(sum.signal_projection);
            // Cauchy-Schwarz gives a nonnegative residual. Only clamp final
            // floating-point roundoff; the unrounded projection is retained.
            hypothesis.residual_signal_energy = static_cast<double>(std::max(0.L, state.signal_energy - sum.signal_projection));
            if (state.signal_energy > 0)
                hypothesis.retained_signal_fraction = static_cast<double>(sum.signal_projection / state.signal_energy);
            if (state.config.include_noisy) {
                hypothesis.noisy_projected_energy = static_cast<double>(sum.noisy_projection);
                hypothesis.noisy_log_likelihood_ratio = static_cast<double>(sum.noisy_log_lr);
            }
            if (state.config.include_noise_only) {
                hypothesis.noise_only_projected_energy = static_cast<double>(sum.noise_projection);
                hypothesis.noise_only_log_likelihood_ratio = static_cast<double>(sum.noise_log_lr);
            }
        }
        const auto& correct = partition.totals[state.config.wanted_bit];
        const auto& wrong = partition.totals[1 - state.config.wanted_bit];
        output.correct_minus_wrong_projected_signal_energy = static_cast<double>(correct.signal_projection - wrong.signal_projection);
        if (state.config.include_noisy)
            output.correct_minus_wrong_noisy_log_likelihood_ratio = static_cast<double>(correct.noisy_log_lr - wrong.noisy_log_lr);
        if (state.config.include_noise_only)
            output.correct_minus_wrong_noise_only_log_likelihood_ratio = static_cast<double>(correct.noise_log_lr - wrong.noise_log_lr);
    }
    report.evaluator_wall_seconds = state.elapsed_seconds + std::chrono::duration<double>(Clock::now() - begin).count();
    state.finished = true;
    return report;
}

} // namespace appraisal
