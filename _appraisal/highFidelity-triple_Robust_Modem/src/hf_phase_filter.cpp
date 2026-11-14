#include "appraisal/hf_phase_filter.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace appraisal {
namespace {
using Plan = HfPhaseFilterPlan;
using Complex = datapump::triad::v3::Complex;
constexpr auto phases = Plan::phase_count, frequencies = Plan::frequency_count;
constexpr auto radii = Plan::gain_radii.size();
constexpr int frequency_center = static_cast<int>(frequencies / 2);
constexpr int phase_wrap = static_cast<int>(phases);
constexpr std::size_t states = phases * frequencies;
constexpr double tau = 2 * std::numbers::pi;
constexpr double log_two = std::numbers::ln2, log_quarter = -2 * log_two;
constexpr double log_three_quarters = -.28768207245178092744;
using Prior = HfPhaseFilterPrior;
constexpr HfPhaseFilterTransition walk_transition{.5, .25, .75};
constexpr HfPhaseFilterTransition sticky_transition{31. / 32, 1. / 64, 63. / 64};
constexpr HfPhaseFilterTransition unweighted_path{};
constexpr HfPhaseFilterTransition walk_log_transition{-log_two, log_quarter, log_three_quarters};

using State = std::array<double, states>;
using LagRotations = std::array<std::array<Complex, Plan::blocks_per_group>, frequencies>;

struct Rotations {
    LagRotations lag{};
    std::array<Complex, phases> phase{};
    std::array<std::array<std::size_t, phases>, frequencies> previous{};

    Rotations() {
        for (std::size_t f = 0; f < frequencies; ++f) {
            const auto k = static_cast<int>(f) - frequency_center;
            const auto hz = k * Plan::frequency_step_hz;
            for (std::size_t j = 0; j < Plan::blocks_per_group; ++j) {
                const double seconds = (j * Plan::block_rows + (Plan::block_rows - 1) * .5) /
                    Plan::sample_rate;
                lag[f][j] = std::polar(1., -tau * hz * seconds);
            }
            for (std::size_t p = 0; p < phases; ++p) {
                const auto turn = static_cast<int>(p) - 2 * k;
                previous[f][p] = static_cast<std::size_t>((turn + phase_wrap) % phase_wrap);
            }
        }
        for (std::size_t p = 0; p < phases; ++p)
            phase[p] = std::polar(1., -tau * p / phases);
    }
};

void require(bool valid, const char* message) {
    if (!valid) throw std::invalid_argument(message);
}

void check_stop(std::stop_token stop) {
    if (stop.stop_requested()) throw std::runtime_error("HF phase filter cancelled");
}

HfPhaseFilterTransition log_transition(Prior prior) {
    switch (prior) {
    // Reuse the original constants exactly for the default and private paths.
    case Prior::walk: return walk_log_transition;
    case Prior::sticky: return {std::log(sticky_transition.stay), -6 * log_two,
                                std::log(sticky_transition.edge_stay)};
    }
    throw std::invalid_argument("unknown HF phase filter transition prior");
}

State phase_projection(std::span<const Complex> correlation, const Rotations& rotations) {
    State projection{};
    for (std::size_t f = 0; f < frequencies; ++f) {
        Complex sum{};
        for (std::size_t j = 0; j < correlation.size(); ++j)
            sum += correlation[j] * rotations.lag[f][j];
        for (std::size_t p = 0; p < phases; ++p)
            projection[f * phases + p] = std::real(sum * rotations.phase[p]);
    }
    return projection;
}

double emit(State& weights, const State& projection, double radius, double energy) {
    auto maximum = -std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < states; ++i) {
        weights[i] += 2 * radius * projection[i];
        maximum = std::max(maximum, weights[i]);
    }
    require(std::isfinite(maximum), "HF phase filter has invalid emission projection");
    double normalizer = 0;
    for (auto& value : weights) {
        value -= maximum;
        normalizer += std::exp(value);
    }
    const auto log_normalizer = std::log(normalizer);
    for (auto& value : weights) value -= log_normalizer;
    const auto answer = (maximum - radius * radius * energy) + log_normalizer;
    require(std::isfinite(answer), "HF phase filter has invalid emission likelihood");
    return answer;
}

double log_add(double a, double b) {
    return std::max(a, b) + std::log1p(std::exp(-std::abs(a - b)));
}

template<class Combine>
void advance(State& weights, const Rotations& rotations, HfPhaseFilterTransition log_prior, Combine combine) {
    State next{};
    for (std::size_t f = 0; f < frequencies; ++f) {
        // At either boundary the disallowed step stays at the edge.
        const auto stay = f == 0 || f + 1 == frequencies ? log_prior.edge_stay : log_prior.stay;
        const auto neighbor = log_prior.step_each;
        for (std::size_t p = 0; p < phases; ++p) {
            auto weight = weights[f * phases + rotations.previous[f][p]] + stay;
            if (f != 0)
                weight = combine(weight, weights[(f - 1) * phases + rotations.previous[f - 1][p]] + neighbor);
            if (f + 1 != frequencies)
                weight = combine(weight, weights[(f + 1) * phases + rotations.previous[f + 1][p]] + neighbor);
            next[f * phases + p] = weight;
        }
    }
    weights = next;
}

const Rotations& phase_filter_rotations() {
    static const Rotations rotations;
    return rotations;
}

double normalize(std::span<const Complex> q, std::span<const double> energies, double variance,
                 std::span<Complex> normalized) {
    double energy = 0;
    for (std::size_t j = 0; j < q.size(); ++j) {
        const auto g = energies[j];
        require(std::isfinite(g) && g >= 0 && (g != 0 || q[j] == Complex{}),
                "HF phase filter requires finite consistent template energy");
        normalized[j] = q[j] / variance;
        require(std::isfinite(normalized[j].real()) && std::isfinite(normalized[j].imag()),
                "HF phase filter requires finite normalized correlation");
        energy += g / variance;
    }
    require(std::isfinite(energy), "HF phase filter requires finite normalized energy");
    return energy;
}

std::array<double, radii> score_bit(std::span<const Complex> q, std::span<const double> energies,
                                   double variance, const Rotations& rotations,
                                   HfPhaseFilterTransition log_prior, std::stop_token stop) {
    std::array<double, radii> score{};
    std::array<State, radii> weights{};
    for (auto& radius_weights : weights) radius_weights.fill(-std::log(static_cast<double>(states)));
    std::array<Complex, Plan::blocks_per_group> normalized{};
    for (std::size_t first = 0; first < q.size(); first += Plan::blocks_per_group) {
        check_stop(stop);
        const auto count = std::min(Plan::blocks_per_group, q.size() - first);
        const auto energy = normalize(q.subspan(first,count),energies.subspan(first,count),variance,normalized);
        const auto projection = phase_projection(std::span(normalized).first(count), rotations);
        for (std::size_t r = 0; r < radii; ++r) {
            score[r] += emit(weights[r], projection, Plan::gain_radii[r], energy);
            if (first + count < q.size()) advance(weights[r], rotations, log_prior, log_add);
        }
    }
    return score;
}
} // namespace

HfPhaseFilterTransition hf_phase_filter_transition(Prior prior) {
    switch (prior) {
    case Prior::walk: return walk_transition;
    case Prior::sticky: return sticky_transition;
    }
    throw std::invalid_argument("unknown HF phase filter transition prior");
}

HfPhaseFilterPlan hf_phase_filter_plan(std::size_t samples) {
    require(samples > 0 && samples <= Plan::maximum_samples, "HF phase filter aperture is outside the 600 s limit");
    const auto blocks = 1 + (samples - 1) / Plan::block_rows;
    const auto groups = 1 + (blocks - 1) / Plan::blocks_per_group;
    constexpr auto bits = 2U;
    return {samples, blocks, groups, bits * frequencies * blocks, bits * radii * states * groups,
            bits * radii * phases * (3 * frequencies - 2) * (groups - 1)};
}

HfPhaseFilterReport hf_phase_filter(const Plan& plan, const HfPhaseBlockCorrelations& q,
    const HfPhaseBlockEnergies& energies, double variance, std::stop_token stop, Prior prior) {
    check_stop(stop);
    require(plan == hf_phase_filter_plan(plan.samples), "HF phase filter plan is inconsistent");
    require(std::isfinite(variance) && variance > 0, "HF phase filter requires finite positive noise variance");
    const auto& rotations = phase_filter_rotations();
    const auto log_prior = log_transition(prior);
    HfPhaseFilterReport report;
    for (std::size_t bit = 0; bit < 2; ++bit) {
        require(q[bit].size() == plan.blocks && energies[bit].size() == plan.blocks,
                "HF phase filter block statistics do not match the public aperture");
        const auto scores = score_bit(q[bit], energies[bit], variance, rotations, log_prior, stop);
        const auto maximum = std::max(scores[0], scores[1]);
        const auto minimum = std::min(scores[0], scores[1]);
        require(std::isfinite(maximum) && std::isfinite(minimum), "HF phase filter accumulated likelihood is nonfinite");
        report.radius_log_e[bit] = scores;
        report.bit_log_e[bit] = log_add(maximum, minimum) - log_two;
    }
    return report;
}

HfPhaseFilterPathDiagnostic hf_phase_filter_path_diagnostic(const Plan& plan, std::span<const Complex> clean_q,
    std::span<const Complex> noisy_q, std::span<const double> energies, double clean_energy,
    double variance, std::stop_token stop) {
    check_stop(stop);
    require(plan == hf_phase_filter_plan(plan.samples) && clean_q.size() == plan.blocks &&
        noisy_q.size() == plan.blocks && energies.size() == plan.blocks,
        "HF phase path diagnostic requires complete consistent block statistics");
    require(std::isfinite(clean_energy) && clean_energy > 0 && std::isfinite(variance) && variance > 0,
        "HF phase path diagnostic requires finite positive clean energy and noise variance");
    const auto& rotations = phase_filter_rotations();
    const auto maximum = [](double a,double b) { return std::max(a,b); };
    const auto state_maximum = [](const State& values) { return *std::max_element(values.begin(),values.end()); };
    State clean_path{};
    std::array<State,radii> raw_path{}, prior_path{}, posterior{};
    const auto initial_log_prior = -std::log(static_cast<double>(states));
    for (auto& path : prior_path) path.fill(initial_log_prior);
    for (auto& path : posterior) path.fill(initial_log_prior);
    std::array<Complex,Plan::blocks_per_group> normalized{};
    HfPhaseFilterPathDiagnostic report;
    double template_energy = 0;
    for (std::size_t first = 0; first < plan.blocks; first += Plan::blocks_per_group) {
        check_stop(stop);
        const auto count = std::min(Plan::blocks_per_group, plan.blocks - first);
        const auto group_energies = energies.subspan(first,count);
        template_energy += normalize(clean_q.subspan(first,count),group_energies,1.,normalized);
        const auto clean = phase_projection(std::span(normalized).first(count), rotations);
        const auto noise_scaled_energy = normalize(noisy_q.subspan(first,count),group_energies,variance,normalized);
        const auto noisy = phase_projection(std::span(normalized).first(count), rotations);
        for (std::size_t i = 0; i < states; ++i) clean_path[i] += clean[i];
        for (std::size_t r = 0; r < radii; ++r) {
            const double radius = Plan::gain_radii[r];
            for (std::size_t i = 0; i < states; ++i) {
                const auto log_lr = radius * (2 * noisy[i] - radius * noise_scaled_energy);
                raw_path[r][i] += log_lr;
                prior_path[r][i] += log_lr;
            }
            report.conditional_noisy_log_e[r] += emit(posterior[r],noisy,radius,noise_scaled_energy);
        }
        if (first + count < plan.blocks) {
            advance(clean_path,rotations,unweighted_path,maximum);
            for (std::size_t r = 0; r < radii; ++r) {
                advance(raw_path[r],rotations,unweighted_path,maximum);
                advance(prior_path[r],rotations,walk_log_transition,maximum);
                advance(posterior[r],rotations,walk_log_transition,log_add);
            }
        }
    }
    for (std::size_t r = 0; r < radii; ++r) {
        report.maximum_noisy_log_lr[r] = state_maximum(raw_path[r]);
        report.maximum_prior_weighted_noisy_path_log_e[r] = state_maximum(prior_path[r]);
        require(std::isfinite(report.maximum_noisy_log_lr[r]) &&
                std::isfinite(report.maximum_prior_weighted_noisy_path_log_e[r]) &&
                std::isfinite(report.conditional_noisy_log_e[r]), "HF phase path diagnostic score is nonfinite");
    }
    report.optimal_clean_correlation_real = state_maximum(clean_path);
    require(std::isfinite(template_energy) && template_energy > 0 &&
            std::isfinite(report.optimal_clean_correlation_real) && report.optimal_clean_correlation_real >= 0,
            "HF phase path diagnostic has invalid clean projection");
    const auto fraction = (report.optimal_clean_correlation_real / std::sqrt(template_energy)) / std::sqrt(clean_energy);
    require(fraction <= 1 + 1e-8, "HF phase path diagnostic projection exceeds supplied clean energy");
    report.clean_coherence_power_lower_bound = std::min(1.,fraction * fraction);
    constexpr auto tracks = 1 + 3 * radii;
    report.correlation_products = 2 * frequencies * plan.blocks;
    report.emission_states = tracks * states * plan.groups;
    report.transition_edges = tracks * phases * (3 * frequencies - 2) * (plan.groups - 1);
    return report;
}

} // namespace appraisal
