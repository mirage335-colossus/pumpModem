#include "appraisal/hf_history.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace {
using namespace appraisal;
using Complex = datapump::triad::v3::Complex;
std::size_t checks = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
void near(double actual, double expected, double tolerance, const char* message) {
    check(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}
template<class Function> void rejects(Function function, const char* message) {
    bool rejected = false;
    try { function(); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, message);
}

struct Inputs {
    std::vector<Complex> zero, one, signal, noisy, noise;
    explicit Inputs(std::size_t size) : zero(size), one(size), signal(size), noisy(size), noise(size) {}
};
HfHistoryReport evaluate(const HfHistoryConfig& config, const Inputs& input, std::size_t chunk) {
    HfHistoryDiagnostic diagnostic(config);
    for (std::size_t first = 0; first < input.signal.size();) {
        const auto count = std::min(chunk, input.signal.size() - first);
        const auto span = [&](const auto& values) { return std::span<const Complex>(values).subspan(first, count); };
        diagnostic.append(config.first_sample + first, span(input.zero), span(input.one), span(input.signal),
                          config.include_noisy ? span(input.noisy) : std::span<const Complex>{},
                          config.include_noise_only ? span(input.noise) : std::span<const Complex>{});
        first += count;
    }
    const auto first_report = diagnostic.finish();
    const auto second_report = diagnostic.finish();
    check(first_report.evaluator_wall_seconds == second_report.evaluator_wall_seconds,
          "finish is idempotent, without counting its earlier flush twice");
    rejects([&] { diagnostic.append(config.stop_sample, {}, {}, {}); }, "finished accumulator rejects append");
    return first_report;
}

// An independent batch implementation uses a normalized template vector and
// explicit covariance determinant/quadratic form, unlike the streamed sums.
struct Reference {
    std::uint64_t rank = 0;
    double reference_energy = 0, projection = 0, log_lr = 0;
};
Reference direct(std::span<const Complex> basis, std::span<const Complex> values,
                 std::size_t block, double variance) {
    Reference result;
    for (std::size_t start = 0; start < basis.size(); start += block) {
        const auto count = std::min(block, basis.size() - start);
        double energy = 0;
        for (std::size_t j = 0; j < count; ++j) energy += std::norm(basis[start + j]);
        result.reference_energy += energy;
        if (energy == 0) continue;
        ++result.rank;
        Complex dot{};
        for (std::size_t j = 0; j < count; ++j)
            dot += std::conj(basis[start + j] / std::sqrt(energy)) * values[start + j];
        const auto projection = std::norm(dot);
        result.projection += projection;
        if (variance > 0)
            result.log_lr += std::log(variance / (variance + energy)) +
                             projection * (1 / variance - 1 / (variance + energy));
    }
    return result;
}

void constant_phase_and_partial_blocks() {
    constexpr std::size_t count = 1071;
    Inputs input(count);
    const auto gain = std::polar(2., .7);
    for (std::size_t i = 0; i < count; ++i) {
        input.zero[i] = 1;
        input.one[i] = i % 2 ? -1 : 1;
        input.signal[i] = gain;
        input.noise[i] = {.13 * std::sin(.33 * i), .19 * std::cos(.71 * i)};
        input.noisy[i] = input.signal[i] + input.noise[i];
    }
    HfHistoryConfig config{64, 43, 43 + count, 0, .3, true, true};
    const auto whole = evaluate(config, input, count);
    const auto chunked = evaluate(config, input, 17);
    check(whole.samples == count && whole.config.first_sample == 43, "physical public aperture identity retained");
    near(whole.noise_free_energy, 4. * count, 1e-10, "constant gain energy");
    near(whole.noise_free_energy_seconds, count / 16., 1e-11, "sample energy converted using physical clock");
    near(whole.blocks.back().hypotheses[1].projected_signal_energy, 4. / count, 1e-12,
         "wrong alternating basis is rejected by whole-aperture cancellation");
    check(!whole.blocks.back().requested_block_seconds, "full-aperture block has no invented fixed duration");
    for (std::size_t p = 0; p < whole.blocks.size(); ++p) {
        const auto& row = whole.blocks[p];
        const auto& other = chunked.blocks[p];
        const auto blocks = (count + row.block_samples - 1) / row.block_samples;
        check(row.block_count == blocks, "partial terminal block counted exactly once");
        near(row.hypotheses[0].projected_signal_energy, 4. * count, 1e-10,
             "unknown constant complex gain retains complete signal energy in every partition");
        near(row.hypotheses[0].residual_signal_energy, 0., 1e-10, "correct constant-gain residual vanishes");
        near(*row.hypotheses[0].retained_signal_fraction, 1., 1e-14, "unit retained fraction");
        near(row.correct_minus_wrong_projected_signal_energy,
             row.hypotheses[0].projected_signal_energy - row.hypotheses[1].projected_signal_energy,
             1e-10, "correct-minus-wrong uses specified bit index");
        for (std::size_t bit = 0; bit < 2; ++bit) {
            const auto& basis = bit ? input.one : input.zero;
            const auto signal = direct(basis, input.signal, row.block_samples, .3);
            const auto noisy = direct(basis, input.noisy, row.block_samples, .3);
            const auto noise = direct(basis, input.noise, row.block_samples, .3);
            const auto& hypothesis = row.hypotheses[bit];
            check(hypothesis.rank == signal.rank, "projection rank matches independent batch blocks");
            near(hypothesis.reference_energy, signal.reference_energy, 1e-10, "reference-energy normalization");
            near(hypothesis.projected_signal_energy, signal.projection, 2e-9, "independent normalized-vector projection");
            near(*hypothesis.noisy_log_likelihood_ratio, noisy.log_lr, 2e-9,
                 "fixed-prior complex-Gaussian likelihood agrees with covariance determinant");
            near(*hypothesis.noise_only_log_likelihood_ratio, noise.log_lr, 2e-9,
                 "noise-only likelihood uses same fixed prior and variance");
            check(hypothesis.projected_signal_energy == other.hypotheses[bit].projected_signal_energy &&
                  hypothesis.noisy_log_likelihood_ratio == other.hypotheses[bit].noisy_log_likelihood_ratio &&
                  hypothesis.noise_only_log_likelihood_ratio == other.hypotheses[bit].noise_only_log_likelihood_ratio,
                  "caller chunking does not change physical blocks or accumulation order");
        }
    }
    check(whole.blocks.front().hypotheses[0].rank > whole.blocks.back().hypotheses[0].rank,
          "more retained local phase comes with explicit extra nuisance rank");
}

void phase_history_loss() {
    constexpr std::size_t count = 1024;
    Inputs input(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto t = static_cast<double>(i) / 64;
        input.zero[i] = 1;
        input.one[i] = std::polar(1., 2 * std::numbers::pi * 8 * t);
        input.signal[i] = std::polar(1., .1 * t * t);
    }
    const HfHistoryConfig config{64, 100, 100 + count, 0};
    const auto curved = evaluate(config, input, 37);
    near(curved.noise_free_energy, count, 1e-11, "quadratic phase preserves physical signal energy");
    check(*curved.blocks.front().hypotheses[0].retained_signal_fraction > .98,
          "short fixed blocks retain slow curved phase locally");
    check(*curved.blocks.back().hypotheses[0].retained_signal_fraction < .1,
          "one constant-gain hypothesis loses long curved phase");
    for (const auto& row : curved.blocks) {
        const auto reference = direct(input.zero, input.signal, row.block_samples, 0);
        near(row.hypotheses[0].projected_signal_energy, reference.projection, 1e-10,
             "quadratic-phase projection agrees with independent direct integration");
        check(!row.hypotheses[0].noisy_log_likelihood_ratio && !row.hypotheses[0].noise_only_log_likelihood_ratio,
              "absent noisy observations do not become fabricated likelihoods");
    }
    // An exact controlled counterexample: no energy is lost, but each quarter
    // second changes sign. Long coherent sums cancel; local gains absorb it.
    for (std::size_t i = 0; i < count; ++i) input.signal[i] = (i / 16) % 2 ? -1 : 1;
    const auto flipped = evaluate(config, input, 31);
    near(flipped.blocks.front().hypotheses[0].projected_signal_energy, count, 0., "piecewise phase is locally exact");
    near(flipped.blocks.back().hypotheses[0].projected_signal_energy, 0., 0., "coherent sign-reversal cancellation");
    check(flipped.blocks.front().hypotheses[0].rank == 64 && flipped.blocks.back().hypotheses[0].rank == 1,
          "coherent cancellation example exposes the dimensionality cost");
}

void zero_template_and_likelihood_penalty() {
    Inputs input(65);
    std::fill(input.zero.begin(), input.zero.end(), Complex{1, 0});
    std::fill(input.signal.begin(), input.signal.end(), Complex{1, 0});
    const HfHistoryConfig config{64, 0, 65, 1, 2, true, true};
    const auto report = evaluate(config, input, 13);
    for (const auto& row : report.blocks) {
        const auto& zero = row.hypotheses[1];
        check(zero.rank == 0, "zero template has zero rank including partial blocks");
        near(zero.projected_signal_energy, 0, 0, "zero template has no projected energy");
        near(zero.residual_signal_energy, 65, 0, "zero-template residual retains all signal energy");
        near(*zero.noisy_log_likelihood_ratio, 0, 0, "zero template is exactly the null model");
        check(*row.hypotheses[0].noisy_log_likelihood_ratio < 0,
              "zero observation pays a fixed-prior complexity penalty");
        near(row.correct_minus_wrong_projected_signal_energy, -65, 0, "wanted label one reverses correct-minus-wrong sign");
        check(*row.correct_minus_wrong_noisy_log_likelihood_ratio > 0,
              "zero template wins against noisy-model complexity on zero observations");
    }
    std::fill(input.signal.begin(), input.signal.end(), Complex{});
    const auto absent = evaluate(config, input, 3);
    for (const auto& row : absent.blocks)
        for (const auto& hypothesis : row.hypotheses)
            check(!hypothesis.retained_signal_fraction, "zero-energy observation has no defined retained fraction");
}

void validation() {
    rejects([] { HfHistoryDiagnostic diagnostic({64, 2, 2}); }, "empty aperture rejected");
    rejects([] { HfHistoryDiagnostic diagnostic({0, 0, 2}); }, "zero sample rate rejected");
    rejects([] { HfHistoryDiagnostic diagnostic({64, 0, 2, 2}); }, "nonbinary wanted label rejected");
    rejects([] { HfHistoryDiagnostic diagnostic({64, 0, 2, 0, 0, true}); }, "likelihood without variance rejected");
    HfHistoryDiagnostic diagnostic({64, 4, 6});
    const std::array<Complex, 2> samples{Complex{1, 0}, Complex{2, 0}};
    rejects([&] { diagnostic.append(3, samples, samples, samples); }, "unobserved initial sample rejected");
    rejects([&] { diagnostic.append(4, samples, std::span(samples).first(1), samples); }, "unequal spans rejected");
    rejects([&] { diagnostic.append(4, samples, samples, samples, samples); }, "unexpected noisy observation rejected");
    rejects([&] { diagnostic.finish(); }, "incomplete aperture never finishes silently");
    auto bad = samples;
    bad.back() = {std::numeric_limits<double>::quiet_NaN(), 0};
    rejects([&] { diagnostic.append(4, samples, samples, bad); }, "nonfinite input rejected before partial mutation");
    diagnostic.append(4, std::span(samples).first(1), std::span(samples).first(1), std::span(samples).first(1));
    rejects([&] { diagnostic.append(5, samples, samples, samples); }, "aperture overrun rejected");
    diagnostic.append(5, std::span(samples).last(1), std::span(samples).last(1), std::span(samples).last(1));
    near(diagnostic.finish().noise_free_energy, 5, 0, "rejected calls preserve existing accumulator state");
}
} // namespace

int main() {
    try {
        constant_phase_and_partial_blocks();
        phase_history_loss();
        zero_template_and_likelihood_penalty();
        validation();
        std::cout << "HF history diagnostic: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "HF history diagnostic failure after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
