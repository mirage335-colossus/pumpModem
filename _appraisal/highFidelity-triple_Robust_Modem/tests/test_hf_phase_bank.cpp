#include "appraisal/hf_phase_bank.hpp"
#include "hf_phase_bank_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace {
using namespace appraisal;
using Complex = datapump::triad::v3::Complex;
constexpr double two_pi = 2 * std::numbers::pi;
unsigned checks = 0;

void require(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
void near(long double actual, long double expected, long double tolerance, const char* message) {
    if (!(std::abs(actual - expected) <= tolerance))
        std::cerr << message << ": got " << actual << ", expected " << expected << '\n';
    require(std::abs(actual - expected) <= tolerance, message);
}

template<class Function> void rejects(Function&& function, const char* message) {
    bool rejected = false;
    try { function(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, message);
}

void priors() {
    const auto bank = hf_phase_bank_models();
    require(bank.size() == 97, "expected one zero and 96 distinct nonzero models");
    require(bank.front().amplitude_radians == 0 && bank.front().phase_radians == 0 && bank.front().period_seconds == 0,
            "zero phase trajectory must occur first exactly once");
    for (std::size_t i = 1; i < bank.size(); ++i) {
        const auto& m = bank[i];
        const auto n = i - 1;
        near(m.amplitude_radians, 4 * (n % 4 + 1), 0, "fixed trajectory amplitude order");
        near(m.period_seconds, std::array{90., 120., 180.}[n / 32], 0, "fixed trajectory period order");
        near(m.phase_radians, two_pi * ((n / 4) % 8) / 8, 1e-15, "fixed trajectory initial phase order");
    }
    constexpr std::array values{0.L, -1.L, 3.L};
    constexpr std::array duplicate{0.L, -1.L, 3.L, 0.L, -1.L, 3.L};
    const auto direct = std::log((1 + std::exp(-1.L) + std::exp(3.L)) / 3);
    near(detail::hf_phase_log_mean_exp(values), direct, 1e-17L, "mixture is unnormalized or max-selected");
    near(detail::hf_phase_log_mean_exp(duplicate), detail::hf_phase_log_mean_exp(values), 1e-17L,
         "replicating the whole dictionary changes its probability measure");
    constexpr std::array uniform{4.L, 4.L, 4.L, 4.L};
    near(detail::hf_phase_log_mean_exp(uniform), 4, 0, "equivalent identical models change evidence");
}

void analytic_coherent() {
    constexpr std::uint64_t count = 200, first = 32;
    constexpr double variance = .8;
    const auto gain = std::polar(.7, 1.31);
    std::vector<Complex> ref0(count, 1.), ref1(count), noisy(count, gain), noise(count);
    for (std::size_t i = 0; i < count; ++i) ref1[i] = i % 2 ? -1. : 1.;
    HfPhaseBankDiagnostic scorer({50, first, first + count, variance});
    scorer.append(first, ref0, ref1, noisy, noise);
    const auto report = scorer.finish();
    require(report.samples == count && report.active_rows == count && report.model_count == 97 &&
            report.complex_multiply_accumulates == 4 * 97 * count && report.workspace_bytes == hf_phase_bank_workspace_bytes(),
            "phase-bank work or memory receipt is inconsistent");
    const auto null = -std::log1p(count / variance);
    const auto l0 = null + std::norm(gain) * count * count / (variance * (variance + count));
    near(report.wanted_observation.hypotheses[0].zero_phase_log_likelihood_ratio, l0, 1e-12,
         "one unknown complex gain is not analytically marginalized");
    near(report.wanted_observation.hypotheses[1].zero_phase_log_likelihood_ratio, null, 1e-13,
         "orthogonal wrong bit gets spurious coherent evidence");
    for (const auto& h : report.noise_only_observation.hypotheses) {
        near(h.mixture_log_likelihood_ratio, null, 1e-13, "zero matched observation likelihood");
        near(h.maximum_model_posterior, 1. / 97, 1e-15, "zero observation must retain model prior");
    }
    near(report.noise_only_observation.bit_one_probability_given_signal, .5, 1e-15, "equal bit prior not normalized");
    near(scorer.finish().wanted_observation.mixture_log_likelihood_ratio,
         report.wanted_observation.mixture_log_likelihood_ratio, 0, "sealed report is unstable");
}

void phase_history() {
    constexpr double fs = 32, variance = .7;
    constexpr std::uint64_t first = 2291, count = 32 * 120, stop = first + count;
    constexpr double injected_amp = 12, injected_phase = two_pi / 8, injected_period = 120;
    const auto gain = std::polar(.22, 1.07);
    std::vector<Complex> ref0(count), ref1(count), noisy(count), noise(count);
    std::array<long double, 97> noise_oracle{};
    const auto bank = hf_phase_bank_models();
    std::array<std::complex<long double>, 97> null_dots{};
    std::size_t active = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const double t = (first + i) / fs;
        noise[i] = std::polar(.21, .027 * i) + std::polar(.11, -.731 * i);
        if (i % 57 != 0) {
            ref0[i] = std::polar(1., .419 * i);
            ref1[i] = (i % 2 ? -1. : 1.) * ref0[i];
            ++active;
        }
        const auto phase = injected_amp * std::sin(two_pi * t / injected_period + injected_phase);
        noisy[i] = gain * std::polar(1., phase) * ref1[i] + noise[i];
        for (std::size_t model = 0; model < bank.size(); ++model) {
            const auto& m = bank[model];
            const auto correction = model == 0 ? Complex{1, 0} :
                std::polar(1., -m.amplitude_radians * std::sin(two_pi * t / m.period_seconds + m.phase_radians));
            null_dots[model] += static_cast<std::complex<long double>>(correction * std::conj(ref1[i]) * noise[i]);
        }
    }
    const auto penalty = -std::log1p(active / variance);
    for (std::size_t i = 0; i < bank.size(); ++i)
        noise_oracle[i] = penalty + std::norm(null_dots[i]) / (variance * (variance + active));

    const auto capture = [&](std::size_t chunk) {
        HfPhaseBankDiagnostic scorer({fs, first, stop, variance});
        for (std::size_t i = 0; i < count;) {
            const auto n = std::min(chunk, count - i);
            scorer.append(first + i, std::span(ref0).subspan(i, n), std::span(ref1).subspan(i, n),
                          std::span(noisy).subspan(i, n), std::span(noise).subspan(i, n));
            i += n;
        }
        return scorer.finish();
    };
    const auto direct = capture(count), partitioned = capture(57);
    for (std::size_t bit = 0; bit < 2; ++bit) {
        near(direct.wanted_observation.hypotheses[bit].mixture_log_likelihood_ratio,
             partitioned.wanted_observation.hypotheses[bit].mixture_log_likelihood_ratio, 0, "chunking changes phase evidence");
        near(direct.noise_only_observation.hypotheses[bit].mixture_log_likelihood_ratio,
             partitioned.noise_only_observation.hypotheses[bit].mixture_log_likelihood_ratio, 0, "chunking changes null evidence");
    }
    require(direct.active_rows == active, "zero-template rows were charged as correlations");
    const auto& wanted = direct.wanted_observation.hypotheses[1];
    near(wanted.maximum_model.amplitude_radians, injected_amp, 0, "bank selected incorrect cyclic amplitude");
    near(wanted.maximum_model.phase_radians, injected_phase, 1e-15, "bank used aperture-relative phase origin");
    near(wanted.maximum_model.period_seconds, injected_period, 0, "bank selected incorrect cyclic period");
    require(wanted.mixture_log_likelihood_ratio > wanted.zero_phase_log_likelihood_ratio + 100 &&
            wanted.mixture_log_likelihood_ratio > direct.wanted_observation.hypotheses[0].mixture_log_likelihood_ratio + 100,
            "cyclic phase did not restore discrimination at fixed template timing");
    near(wanted.mixture_log_likelihood_ratio,
         wanted.maximum_model_log_likelihood_ratio - std::log(97.), 1e-10,
         "well-separated dominant trajectory did not pay its prior mass");
    near(direct.noise_only_observation.hypotheses[1].mixture_log_likelihood_ratio,
         detail::hf_phase_log_mean_exp(noise_oracle), 1e-11,
         "recurring bank disagrees with independently evaluated null phasors");
    require(direct.wanted_observation.bit_one_probability_given_signal > .999 &&
            direct.noise_only_observation.mixture_log_likelihood_ratio < 0, "phase bank admitted its fixed null control");
}

void input_guards() {
    rejects([] { HfPhaseBankDiagnostic invalid({0, 0, 2, 1}); }, "zero sample rate accepted");
    rejects([] { HfPhaseBankDiagnostic invalid({1, 0, 2, 0}); }, "zero white-noise variance accepted");
    HfPhaseBankDiagnostic scorer({1920, 100, 102, 1});
    std::array<Complex, 2> x{1, 2}, y{.3, .2};
    rejects([&] { scorer.append(99, x, x, y, y); }, "phase bank accepted discontinuity");
    rejects([&] { scorer.finish(); }, "phase bank emitted a partial receipt");
    y[1] = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { scorer.append(100, x, x, x, y); }, "phase bank accepted nonfinite samples");
    require(scorer.next_sample() == 100, "rejected append partially altered state");
    std::stop_source stopped;
    stopped.request_stop();
    bool cancelled = false;
    try { scorer.append(100, x, x, x, x, stopped.get_token()); }
    catch (const std::runtime_error&) { cancelled = true; }
    require(cancelled && scorer.next_sample() == 100, "phase bank did not preserve pre-cancelled state");
    scorer.append(100, x, x, x, x);
    require(scorer.finish().samples == 2, "phase bank could not resume valid work after rejected input");
    rejects([&] { scorer.append(102, {}, {}, {}, {}); }, "sealed scorer accepted more samples");
}
}

int main() {
    try {
        priors();
        analytic_coherent();
        phase_history();
        input_guards();
        std::cout << "HF phase-bank assertions passed: " << checks << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "HF phase-bank test failed after " << checks << " assertions: " << error.what() << '\n';
        return 1;
    }
}
