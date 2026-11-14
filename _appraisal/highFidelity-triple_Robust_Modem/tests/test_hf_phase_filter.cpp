#include "appraisal/hf_phase_filter.hpp"

#include <algorithm>
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
using Wide = std::complex<long double>;
using Plan = HfPhaseFilterPlan;
constexpr auto tau = 2 * std::numbers::pi_v<long double>;
unsigned checks = 0;

void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

void near(long double observed, long double expected, long double tolerance, const char* message) {
    if (!(std::abs(observed - expected) <= tolerance))
        std::cerr << message << ": got " << observed << ", expected " << expected << '\n';
    require(std::abs(observed - expected) <= tolerance, message);
}

template<class Function> void rejects(Function&& function, const char* message) {
    bool rejected = false;
    try { function(); } catch (const std::exception&) { rejected = true; }
    require(rejected, message);
}

struct Input {
    Plan plan;
    std::array<std::vector<Complex>, 2> q;
    std::array<std::vector<double>, 2> energy;

    explicit Input(std::size_t samples) : plan(hf_phase_filter_plan(samples)) {
        for (auto& bit : q) bit.resize(plan.blocks);
        for (auto& bit : energy) bit.resize(plan.blocks);
    }

    HfPhaseFilterReport score(double variance, std::stop_token stop = {},
                              HfPhaseFilterPrior prior = HfPhaseFilterPrior::walk) const {
        return hf_phase_filter(plan, {q[0], q[1]}, {energy[0], energy[1]}, variance, stop, prior);
    }
};

long double sample_center(std::size_t block) {
    return (static_cast<long double>(block) * 128 + 63.5L) / 1920;
}

long double emission(const Input& input, std::size_t bit, std::size_t group,
                     int frequency_bin, long double phase, long double radius, long double variance) {
    long double answer = 0;
    for (std::size_t j = group * 15; j < (group + 1) * 15 && j < input.plan.blocks; ++j) {
        const auto unit = std::polar(1.L, -phase - tau * (sample_center(j) - group) * frequency_bin / 16);
        answer += (2 * radius * std::real(unit * Wide(input.q[bit][j])) -
                   radius * radius * input.energy[bit][j]) / variance;
    }
    return answer;
}

long double log_sum(std::span<const long double> values) {
    const auto maximum = *std::max_element(values.begin(), values.end());
    long double sum = 0;
    for (const auto value : values) sum += std::exp(value - maximum);
    return maximum + std::log(sum);
}

void compare_two_group_enumeration(const Input& input, double variance, long double tolerance) {
    const auto report = input.score(variance);
    const auto sticky = input.score(variance,{},HfPhaseFilterPrior::sticky);
    for (std::size_t bit = 0; bit < 2; ++bit) {
        const auto path = hf_phase_filter_path_diagnostic(input.plan,input.q[bit],input.q[bit],
            input.energy[bit],1e22,variance);
        std::array<long double, 2> mixed{}, sticky_mixed{};
        for (std::size_t r = 0; r < Plan::gain_radii.size(); ++r) {
            const long double radius = Plan::gain_radii[r];
            std::vector<long double> paths, sticky_paths;
            paths.reserve(3 * 33 * 32);
            sticky_paths.reserve(3 * 33 * 32);
            long double raw_max = -std::numeric_limits<long double>::infinity();
            long double weighted_max = raw_max;
            for (int k = -16; k <= 16; ++k)
                for (int p = 0; p < 32; ++p)
                    for (int delta : {-1, 0, 1}) {
                        const auto next = std::min(16, std::max(-16, k + delta));
                        const auto phase = tau * p / 32;
                        const auto next_phase = phase + tau * k / 16;
                        const auto prior = (delta == 0 ? .5L : .25L) / (33 * 32);
                        const auto sticky_prior = (delta == 0 ? 31.L / 32 : 1.L / 64) / (33 * 32);
                        const auto path_log_e = emission(input, bit, 0, k, phase, radius, variance) +
                            emission(input, bit, 1, next, next_phase, radius, variance);
                        paths.push_back(std::log(prior) + path_log_e);
                        sticky_paths.push_back(std::log(sticky_prior) + path_log_e);
                        const long double reflected = (delta == 0 && std::abs(k) == 16) ||
                            (delta != 0 && next == k) ? .75L / (33 * 32) : prior;
                        weighted_max = std::max(weighted_max,std::log(reflected)+path_log_e);
                        raw_max = std::max(raw_max,path_log_e);
                    }
            const auto marginal = log_sum(paths);
            const auto sticky_marginal = log_sum(sticky_paths);
            near(report.radius_log_e[bit][r], marginal, tolerance,
                 "forward result differs from exhaustive path likelihood");
            near(path.maximum_noisy_log_lr[r],raw_max,tolerance,
                 "private unweighted optimum disagrees with exhaustive path enumeration");
            near(path.maximum_prior_weighted_noisy_path_log_e[r],weighted_max,tolerance,
                 "private weighted optimum disagrees with exhaustive path enumeration");
            near(path.conditional_noisy_log_e[r],report.radius_log_e[bit][r],tolerance,
                 "private conditional sum differs from public radius likelihood");
            near(sticky.radius_log_e[bit][r],sticky_marginal,tolerance,
                 "sticky sum differs from exhaustive path likelihood with edge reflection");
            mixed[r] = marginal - std::numbers::ln2_v<long double>;
            sticky_mixed[r] = sticky_marginal - std::numbers::ln2_v<long double>;
        }
        near(report.bit_log_e[bit], log_sum(mixed), tolerance, "fixed radius prior was lost");
        near(sticky.bit_log_e[bit],log_sum(sticky_mixed),tolerance,"sticky fixed radius prior was lost");
    }
}

void two_group_enumeration() {
    Input input(1920 + 2 * 128 + 7);
    require(input.plan.blocks == 18 && input.plan.groups == 2, "partial final group was padded");
    require(input.plan.correlation_products == 2 * 33 * 18 &&
            input.plan.emission_states == 2 * 2 * 33 * 32 * 2 &&
            input.plan.transition_edges == 2 * 2 * 97 * 32, "work quote differs from state lattice");
    for (std::size_t bit = 0; bit < 2; ++bit)
        for (std::size_t j = 0; j < input.plan.blocks; ++j) {
            input.q[bit][j] = std::polar(.3 * (bit + 1), .47 * j - .09 * j * j);
            input.energy[bit][j] = j == input.plan.blocks - 1 ? .02 : .2 + .01 * j;
        }
    compare_two_group_enumeration(input, 1.7, 2e-13L);
}

void dynamic_range() {
    Input input(2 * 1920);
    // The first group favors even phase indices by more than 745 log units.
    // The next favors odd indices even more strongly. The phase transition
    // preserves parity, so an underflowed probability recursion cannot recover.
    for (std::size_t j = 0; j < input.plan.blocks; ++j) {
        input.q[0][j] = j < 15 ? Complex{1e6} : std::polar(1e8, static_cast<double>(tau / 32));
        input.energy[0][j] = input.energy[1][j] = 1.;
    }
    compare_two_group_enumeration(input, 1., 1e-5L);
}

void white_noise_normalization() {
    // Conditional likelihood martingale under H0. Keep the first group's
    // posterior nonuniform and integrate a final one-block group under CN(0,1).
    // Midpoint quadrature over [-6,6] in both normalized real axes.
    Input input(1920 + 17);
    input.q[0][0] = {1.2, -.7};
    input.energy[0][0] = .55;
    const auto prefix = hf_phase_filter(hf_phase_filter_plan(1920),
        {std::span(input.q[0]).first(15), std::span(input.q[1]).first(15)},
        {std::span(input.energy[0]).first(15), std::span(input.energy[1]).first(15)}, 1.);
    input.energy[0].back() = .49;
    constexpr double step = .5;
    std::array<long double,2> integral{};
    for (int i = -12; i < 12; ++i)
        for (int j = -12; j < 12; ++j) {
            const auto x = (i + .5) * step, y = (j + .5) * step;
            input.q[0].back() = std::sqrt(.49) * Complex{x, y};
            const std::array rates{HfPhaseFilterPrior::walk,HfPhaseFilterPrior::sticky};
            for (std::size_t r=0;r<rates.size();++r) {
                const auto log_increment = input.score(1.,{},rates[r]).bit_log_e[0] - prefix.bit_log_e[0];
                integral[r] += std::exp(log_increment - x * x - y * y) * step * step / std::numbers::pi;
            }
        }
    for (const auto value : integral)
        near(value, 1, 2e-12L, "proper-complex H0 predictive likelihood does not have mean one");
}

double fixture(bool changing_frequency) {
    Input input(24 * 1920 - 87);
    int k = changing_frequency ? 13 : -16;
    long double phase = 3 * tau / 32;
    double ideal = 0;
    for (std::size_t g = 0; g < input.plan.groups; ++g) {
        for (std::size_t j = g * 15; j < (g + 1) * 15 && j < input.plan.blocks; ++j) {
            const auto h = std::polar(1.L, phase + tau * k * (sample_center(j) - g) / 16);
            const auto energy = 1.4 + .1 * (j % 7);
            input.q[0][j] = Complex(h) * energy;
            input.energy[0][j] = input.energy[1][j] = energy;
            ideal += energy;
        }
        phase += tau * k / 16;
        if (changing_frequency) {
            if (g < 3) ++k;
            if (g >= 6) --k;
        }
    }
    const auto report = input.score(1.);
    const auto explicit_walk = input.score(1.,{},HfPhaseFilterPrior::walk);
    require(report.bit_log_e==explicit_walk.bit_log_e&&report.radius_log_e==explicit_walk.radius_log_e,
            "explicit walk changed default filter arithmetic");
    const auto score = report.bit_log_e[0];
    require(score < ideal && score > .84 * ideal, "tone/chirp did not retain a charged coherent score");
    require(report.radius_log_e[0][1] > report.radius_log_e[0][0] &&
            report.bit_log_e[1] < 0, "tone/chirp chose smaller radius or wrong bit");
    if (changing_frequency) {
        long double constant = 0;
        for (int k0 = -16; k0 <= 16; ++k0)
            for (int p = 0; p < 32; ++p) {
                const auto phase0 = tau * p / 32;
                long double path = 0;
                for (std::size_t g = 0; g < input.plan.groups; ++g)
                    path += emission(input, 0, g, k0, phase0 + tau * k0 * g / 16, 1., 1.);
                constant = std::max(constant, path);
            }
        require(score > constant + 20, "charged chirp evidence failed to beat even the uncharged constant-tone maximum");
    }
    return score;
}

void rejection_and_missing_data() {
    const auto large = hf_phase_filter_plan(Plan::maximum_samples);
    require(large.blocks == 9000 && large.groups == 600 &&
            large.emission_states * Plan::maximum_timing_atoms < 100'000'000 &&
            large.transition_edges * Plan::maximum_timing_atoms < 300'000'000, "aperture caps changed");
    rejects([] { (void)hf_phase_filter_plan(0); }, "empty aperture accepted");
    rejects([] { (void)hf_phase_filter_plan(Plan::maximum_samples + 1); }, "oversize aperture accepted");
    Input input(3841);
    const auto sticky = hf_phase_filter_transition(HfPhaseFilterPrior::sticky);
    require(sticky.stay==31./32&&sticky.step_each==1./64&&sticky.edge_stay==63./64&&
        sticky.stay+2*sticky.step_each==1.&&sticky.edge_stay+sticky.step_each==1.,
        "sticky fixed prior lost mass at the boundary");
    const auto report = input.score(1.);
    for (double score : report.bit_log_e) near(score, 0, 2e-13L, "zero energy changed H0 evidence");
    const auto sticky_zero = input.score(1.,{},HfPhaseFilterPrior::sticky);
    for (double score : sticky_zero.bit_log_e) near(score,0,2e-13L,"sticky prior lost zero-energy mass");
    rejects([&] { (void)input.score(1.,{},static_cast<HfPhaseFilterPrior>(-1)); },
            "undefined phase prior was accepted");
    rejects([&] { (void)input.score(0); }, "zero noise variance accepted");
    ++input.plan.correlation_products;
    rejects([&] { (void)input.score(1.); }, "modified plan accepted");
    --input.plan.correlation_products;
    input.q[0][0] = 1;
    rejects([&] { (void)input.score(1.); }, "nonzero q for zero energy accepted");
    input.energy[0][0] = 1;
    input.q[0][0] = {std::numeric_limits<double>::infinity(), 0.};
    rejects([&] { (void)input.score(1.); }, "nonfinite q accepted");
    input.q[0][0] = {};
    input.energy[0][0] = -1;
    rejects([&] { (void)input.score(1.); }, "negative energy accepted");
    input.energy[0][0] = 0;
    input.q[1].pop_back();
    rejects([&] { (void)input.score(1.); }, "short input accepted");
    input.q[1].resize(input.plan.blocks);
    std::stop_source stop;
    stop.request_stop();
    rejects([&] { (void)input.score(1., stop.get_token()); }, "cancelled input evaluated");
    const auto paths=[&](double clean_energy, std::stop_token cancellation = {}) {
        return hf_phase_filter_path_diagnostic(input.plan,input.q[0],input.q[1],input.energy[0],clean_energy,
            1.,cancellation);
    };
    rejects([&] { (void)paths(0); }, "clean-energy zero emitted an undefined coherence fraction");
    rejects([&] { (void)paths(1.); }, "zero template energy emitted an undefined coherence fraction");
    input.energy[0][0] = 1.;
    const auto zero = paths(1.);
    near(zero.clean_coherence_power_lower_bound,0,0,"orthogonal zero projection retained clean energy");
    near(zero.maximum_noisy_log_lr[1],-1,1e-14L,"zero noisy correlation lost the norm penalty");
    near(zero.conditional_noisy_log_e[1],-1,1e-14L,"zero noisy correlation summed the wrong prior mass");
    input.q[0][0]=1.;
    const auto aligned = paths(1.);
    near(aligned.clean_coherence_power_lower_bound,1,1e-14L,"aligned clean projection lost coherent energy");
    rejects([&] { (void)paths(.25); }, "impossible clean projection was accepted");
    rejects([&] { (void)paths(1.,stop.get_token()); }, "cancelled private diagnostic evaluated paths");
}
} // namespace

int main() {
    try {
        two_group_enumeration();
        dynamic_range();
        white_noise_normalization();
        (void)fixture(false);
        (void)fixture(true);
        rejection_and_missing_data();
        std::cout << "HF phase filter: " << checks << " checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "HF phase filter failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
