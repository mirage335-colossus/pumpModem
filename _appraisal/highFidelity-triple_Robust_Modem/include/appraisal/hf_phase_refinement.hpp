#pragma once

#include "appraisal/hf_public_statistics.hpp"

#include <array>
#include <limits>

namespace appraisal {

struct HfPhaseRefinementWork {
    std::uint64_t fft_row_stages = 0, spectrum_cells = 0, table_lookups = 0;
    std::uint64_t direct_sweeps = 0, direct_rows = 0;
    // Applying unit phases to q: one per direct objective row and per
    // coherent transform input row. Rotator construction is counted below.
    std::uint64_t phase_rotations = 0;
    std::uint64_t trigonometric_evaluations = 0, likelihood_cells = 0;
    friend bool operator==(const HfPhaseRefinementWork&, const HfPhaseRefinementWork&) = default;
};

struct HfPhaseRefinementPlan {
    static constexpr std::size_t maximum_shapes = 24;
    static constexpr std::size_t maximum_starts = 3;
    static constexpr std::size_t maximum_iterations = 16;
    static constexpr std::size_t maximum_backtracks = 4;
    static constexpr std::size_t intermediate_iterations = 8;

    HfPublicStatisticsPlan statistics;
    std::size_t shape_count = 0, nu_count = 0;
    double nu_step_hz = 0;
    std::size_t segment_rows = 0, segment_count = 0, segment_fft_rows = 0;
    std::size_t segment_frequency_radius = 0, coarse_frequency_radius = 0;
    std::size_t intermediate_group_rows = 0, intermediate_group_count = 0;
    double segment_frequency_step_hz = 0, coarse_frequency_step_hz = 0;
    std::size_t coarse_nu_count = 0;
    double coarse_nu_step_hz = 0;
    std::size_t rank_workspace_bytes = 0, refinement_workspace_bytes = 0;
    HfPhaseRefinementWork rank_work, refinement_work;
    friend bool operator==(const HfPhaseRefinementPlan&, const HfPhaseRefinementPlan&) = default;
};

struct HfPhaseTimingRank {
    std::array<double, 2> bit_scores{};
    HfPhaseRefinementWork work;
};

// Integer Cartesian coordinates in quarter-radians and an integer nu index.
// All valid nonzero shapes have equal prior mass, regardless of selection.
struct HfPhaseShapeId {
    int a = 0, b = 0;
    std::size_t nu = 0;
    friend bool operator==(const HfPhaseShapeId&, const HfPhaseShapeId&) = default;
};

struct HfPhaseProposal {
    double a = 0, b = 0, nu_hz = 0, cfo_hz = 0;
    double segment_score = 0, initial_log_lr = 0, continuous_log_lr = 0;
    double intermediate_initial_log_lr = 0, intermediate_final_log_lr = 0;
    double best_scored_neighbor_log_lr = 0, snapped_loss_nats = 0;
    unsigned bit = 0, accepted_steps = 0, intermediate_accepted_steps = 0;
    bool intermediate_used = false;
};

struct HfPhaseRefinementReport {
    std::array<double, 2> bit_log_e{
        -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    std::array<double, 2> maximum_log_lr = bit_log_e;
    std::array<double, 2> maximum_frequency_hz{};
    std::array<HfPhaseShapeId, 2> maximum_shape{};
    std::array<HfPhaseShapeId, HfPhaseRefinementPlan::maximum_shapes> selected_shapes{};
    std::array<HfPhaseProposal, HfPhaseRefinementPlan::maximum_starts> proposals{};
    std::size_t selected_count = 0, proposal_count = 0, workspace_bytes = 0;
    double retained_shape_prior_mass = 0, log_cell_prior = 0;
    HfPhaseRefinementWork work;
};

// Bounds cover a single timing lane and both bits, excluding caller-owned
// block correlations and energies and the existing shared FFT twiddles.
HfPhaseRefinementPlan hf_phase_refinement_plan(const HfPublicStatisticsPlan&);
long double hf_refined_phase_radians(const HfPhaseRefinementPlan&, const HfPhaseShapeId&,
                                     long double absolute_seconds);

// The same bit/template support and known white-noise variance as the public
// statistics. Segment maxima are proposal rankings, never likelihood evidence.
using HfPhaseBlockCorrelations = std::array<std::span<const datapump::triad::v3::Complex>, 2>;
using HfPhaseBlockEnergies = std::array<std::span<const double>, 2>;
HfPhaseTimingRank hf_phase_timing_rank(const HfPhaseRefinementPlan&,
    const HfPreparedPublicStatistics&, const HfPhaseBlockCorrelations&,
    const HfPhaseBlockEnergies&, std::stop_token = {});

// Evidence includes the full new-shape and CFO prior but excludes the caller's
// timing/bit/family weights. Proposal scores contribute no evidence directly.
HfPhaseRefinementReport hf_phase_refinement(const HfPhaseRefinementPlan&,
    const HfPreparedPublicStatistics&, const HfPhaseBlockCorrelations&,
    const HfPhaseBlockEnergies&, const std::array<double, 2>& total_energy,
    std::stop_token = {});

} // namespace appraisal
