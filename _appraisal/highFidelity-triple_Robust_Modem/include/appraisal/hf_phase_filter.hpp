#pragma once

#include "appraisal/hf_phase_refinement.hpp"

#include <optional>
#include <vector>

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

// Fixed four-second edges with linear frequency, sampled at the existing
// nominal block centers. This compiled scorer shares its immutable rotations
// across timing lanes; each call owns only the bounded forward-state scratch.
class HfPreparedPhaseBridge {
public:
    static constexpr std::size_t group_seconds = 4, blocks_per_group = 4 * HfPhaseFilterPlan::blocks_per_group;
    static constexpr std::size_t edge_count = 3 * HfPhaseFilterPlan::frequency_count - 2;
    static constexpr std::size_t lane_workspace_bytes = 128U * 1024, shared_workspace_bytes = 96U * 1024;

    explicit HfPreparedPhaseBridge(std::stop_token = {});
    ~HfPreparedPhaseBridge();
    HfPreparedPhaseBridge(const HfPreparedPhaseBridge&) = delete;
    HfPreparedPhaseBridge& operator=(const HfPreparedPhaseBridge&) = delete;
    HfPhaseFilterReport score(const HfPhaseFilterPlan&, const HfPhaseBlockCorrelations&,
                              const HfPhaseBlockEnergies&, double variance, std::stop_token = {}) const;

private:
    struct Impl;
    std::unique_ptr<const Impl> impl_;
};

struct HfPhaseBridgeSupportProjection {
    double real_correlation=0, clean_coherence_power=0;
};

struct HfPhaseBridgeSupportDiagnostic {
    static constexpr std::size_t workspace_reserved_bytes=256U*1024;
    static constexpr std::size_t original_edges=97, phase_relaxed_edges=291, rate_relaxed_edges=159;
    std::size_t groups=0, correlation_products=0, max_plus_terms=0;
    HfPhaseBridgeSupportProjection original, phase_relaxed, rate_relaxed;
};

// Evaluator-only support maxima at one physical-nearest public timing and
// bit. The alternative supports are deliberate counterfactuals. Their maxima
// neither use noisy IQ nor enter any posterior or admission decision.
HfPhaseBridgeSupportDiagnostic hf_phase_bridge_support_diagnostic(const HfPhaseFilterPlan&,
    std::span<const datapump::triad::v3::Complex> clean_q,std::span<const double> energies,
    double clean_energy,std::stop_token = {});

struct HfPhaseFilterCleanPath {
    double real_correlation = 0, clean_coherence_power = 0;
    std::vector<int> frequency_bins, phase_bins;
    std::size_t jumps = 0, up_jumps = 0, down_jumps = 0, adjacent_reversals = 0,
        successive_jump_reversals = 0, edge_stays = 0, longest_dwell_groups = 0;
    double walk_log_prior = 0, sticky_log_prior = 0;
    double effective_phase_rms_radians = 0, effective_aligned_phase_rms_radians = 0;
    double carrier_phase_rms_radians = 0, carrier_aligned_phase_rms_radians = 0;
    double carrier_frequency_rms_hz = 0;
    std::optional<double> carrier_frequency_4_group_rms_hz;
    std::size_t reversals_below_half_bin_carrier_change = 0;
};

struct HfPhaseFilterCleanTrace {
    static constexpr double target_projection_power_fraction_of_best_real = .99;
    HfPhaseFilterCleanPath maximum_projection, penalized_projection;
    double penalty_correlation_units = 0, target_real_correlation = 0,
        penalized_objective_correlation = 0, certificate_tolerance_correlation = 0;
    std::size_t minimum_jumps_lower_bound = 0;
    bool target_reached = false;
    double carrier_frequency_total_variation_hz = 0, carrier_frequency_rms_step_hz = 0;
    std::size_t carrier_reversal_count = 0, analytic_carrier_points = 0, workspace_reserved_bytes = 0,
        extra_state_evaluations = 0, extra_transition_edges = 0, predecessor_labels = 0;
};

struct HfPhaseFilterCarrierReference {
    // Unwrapped carrier phase in cycles, excluding group delay and envelope.
    // The final boundary uses the actual elapsed time, with no terminal padding.
    std::span<const double> block_center_cycles, group_boundary_cycles;
};

std::size_t hf_phase_filter_clean_trace_workspace(const HfPhaseFilterPlan&);

struct HfPhaseFilterPathDiagnostic {
    double clean_coherence_power_lower_bound = 0;
    double optimal_clean_correlation_real = 0;
    std::array<double, 2> maximum_noisy_log_lr{};
    std::array<double, 2> maximum_prior_weighted_noisy_path_log_e{};
    std::array<double, 2> conditional_noisy_log_e{};
    std::uint64_t correlation_products = 0, emission_states = 0, transition_edges = 0;
    std::size_t workspace_reserved_bytes = HfPhaseFilterPlan::lane_workspace_bytes;
    std::optional<HfPhaseFilterCleanTrace> clean_trace;
};

// Private evaluator at one supplied timing and bit. Uses the exact discrete
// support of the public filter, with no search or receiver outputs modified.
// The real-projection optimum lower-bounds the best absolute projection. A
// path maximum is descriptive, including when weighted by its path prior.
HfPhaseFilterPathDiagnostic hf_phase_filter_path_diagnostic(const HfPhaseFilterPlan&,
    std::span<const datapump::triad::v3::Complex> clean_q, std::span<const datapump::triad::v3::Complex> noisy_q,
    std::span<const double> energies, double clean_energy, double variance, std::stop_token = {},
    std::optional<HfPhaseFilterCarrierReference> carrier = std::nullopt);

} // namespace appraisal
