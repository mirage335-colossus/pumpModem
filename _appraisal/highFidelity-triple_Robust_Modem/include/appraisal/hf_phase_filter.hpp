#pragma once

#include "appraisal/hf_phase_refinement.hpp"

namespace appraisal {

struct HfPhaseFilterPlan {
    static constexpr std::size_t sample_rate = 1920, block_rows = 128, blocks_per_group = 15;
    static constexpr std::size_t phase_count = 32, frequency_count = 33;
    static constexpr double frequency_step_hz = 1. / 16;
    static constexpr std::array<double, 2> gain_radii{.5, 1.};
    static constexpr std::size_t maximum_samples = 600 * sample_rate, maximum_timing_atoms = 39;
    static constexpr std::size_t lane_workspace_bytes = 128U * 1024;

    std::size_t samples = 0, blocks = 0, groups = 0;
    // The quote covers both bits for one timing atom. Callers own q and E.
    std::uint64_t correlation_products = 0, emission_states = 0, transition_edges = 0;
    friend bool operator==(const HfPhaseFilterPlan&, const HfPhaseFilterPlan&) = default;
};

struct HfPhaseFilterReport {
    std::array<double, 2> bit_log_e{};
    // Each row contains the two fixed-radius scores for that bit.
    std::array<std::array<double, 2>, 2> radius_log_e{};
};

enum class HfPhaseFilterPrior { walk, sticky };

struct HfPhaseFilterTransition {
    double stay, step_each, edge_stay;
};
HfPhaseFilterTransition hf_phase_filter_transition(HfPhaseFilterPrior);

HfPhaseFilterPlan hf_phase_filter_plan(std::size_t samples);

// Exact likelihood ratio for the fixed, discrete, block-constant phase model
// under proper-complex IID white noise with known E|n|². At each one-second
// boundary, transport phase using the old frequency and then walk frequency.
// A partial final group uses only its supplied block statistics. Both bits use
// q[j] = x[j]^H y[j] and E[j] = x[j]^H x[j] for the same public timing atom.
// Output excludes bit, timing and model-family weights.
HfPhaseFilterReport hf_phase_filter(const HfPhaseFilterPlan&, const HfPhaseBlockCorrelations& q,
    const HfPhaseBlockEnergies& energy, double variance, std::stop_token = {},
    HfPhaseFilterPrior = HfPhaseFilterPrior::walk);

struct HfPhaseFilterPathDiagnostic {
    double clean_coherence_power_lower_bound = 0;
    double optimal_clean_correlation_real = 0;
    std::array<double, 2> maximum_noisy_log_lr{};
    std::array<double, 2> maximum_prior_weighted_noisy_path_log_e{};
    std::array<double, 2> conditional_noisy_log_e{};
    std::uint64_t correlation_products = 0, emission_states = 0, transition_edges = 0;
    std::size_t workspace_reserved_bytes = HfPhaseFilterPlan::lane_workspace_bytes;
};

// Private evaluator at one supplied timing and bit. Uses the exact discrete
// support of the public filter, with no search or receiver outputs modified.
// The real-projection optimum lower-bounds the best absolute projection. A
// path maximum is descriptive, including when weighted by its path prior.
HfPhaseFilterPathDiagnostic hf_phase_filter_path_diagnostic(const HfPhaseFilterPlan&,
    std::span<const datapump::triad::v3::Complex> clean_q, std::span<const datapump::triad::v3::Complex> noisy_q,
    std::span<const double> energies, double clean_energy, double variance, std::stop_token = {});

} // namespace appraisal
