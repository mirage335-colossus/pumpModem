#pragma once

#include <cstddef>
#include <cstdint>

namespace datapump::triad::statistics {

// Bounds for one fixed, whitened complex Gaussian matched-subspace score.
// With y ~ CN(mu, I), E = ||Q^H y||^2 has Gamma(rank, 1) under H0 and
// noncentrality ||Q^H mu||^2 under H1. Search-wide claims must use the complete
// hypothesis count, not a single-candidate threshold.
struct Threshold {
    double score = 0;
    double log_hypotheses = 0;
    double log_false_probability_per_hypothesis = 0;
    bool conservative = true;
};

Threshold conservative_null_threshold(std::uint64_t rank,
                                       double log_hypotheses,
                                       double family_false_probability);
// Numerically invert the fixed known-noise Gamma law. Falls back to the
// concentration bound if the bounded solver cannot establish a numerical bracket.
Threshold gamma_null_threshold(std::uint64_t rank, double log_hypotheses,
                               double family_false_probability);
double gamma_log_survival(std::uint64_t rank, double score);

double known_channel_binary_error(double squared_distance,
                                  double complex_noise_variance);

double zero_event_upper_bound(std::uint64_t independent_campaigns,
                              double confidence = 0.95);

struct SearchSpace {
    double monitoring_horizon_seconds = 86400;
    double attempt_spacing_seconds = 60;
    std::uint64_t timing_hypotheses = 1;
    std::uint64_t epoch_hypotheses = 1;
    std::uint64_t sample_clock_hypotheses = 1;
    std::uint64_t frequency_hypotheses = 1;
    std::uint64_t channel_hypotheses = 1;
    std::uint64_t phase_hypotheses = 1;
    std::uint64_t bit_hypotheses = 2;
    std::uint64_t duration_alternatives = 1;
    // Additional declared wrong-message count. The planner conservatively
    // budgets at least the complete per-attempt bank, even if this is smaller.
    std::uint64_t wrong_message_competitors = 1;
};

struct PlannerInput {
    SearchSpace search{};
    double cn0_db_hz = 8;
    double target_correct_probability = 0.99;
    double false_admission_probability_per_horizon = 1e-4;

    // Three-carrier format constraints. Pattern rates are in fine chips/s for
    // one carrier; the legacy GUI Rate maps to inner_rate_hz below.
    double maximum_duration_seconds = 38 * 86400;
    double dwell_seconds = 0.4;
    // Permitted durations are n*quantum+offset, n>=1. Zero quantum selects one
    // dwell. Both values must be whole dwell multiples, with -quantum<offset<=0.
    // A burst source uses quantum=tile*(wait+1), offset=-tile*wait.
    double duration_quantum_seconds = 0;
    double duration_offset_seconds = 0;
    std::uint64_t minimum_dwells = 20;
    double inner_rate_hz = 36;
    std::uint64_t added_symbols = 10;
    std::uint64_t minimum_inner_coefficients = 64;

    // Fixed coherent rank uses coherence_block_seconds=0. Otherwise each
    // scored block contributes rank_per_block dimensions.
    double coherence_block_seconds = 0;
    std::uint64_t rank_per_block = 1;

    // Explicit lower bounds; the planner never invents these from an SNR.
    double usable_time_fraction = 1;
    double maximum_outage_fraction = 0;
    // Outage fraction is measured within usable_time_fraction. Coefficient
    // density assumes whole usable cells; it is not an erasure-identifiability
    // proof. The independent wrong-projection certificate supplies that proof.
    double startup_overhead_seconds = 0;
    double retained_energy_fraction = 1;
    // Upper bound on EACH wrong-bit projected noncentrality divided by the
    // declared true-energy LOWER bound. This must hold across the entire
    // allowed channel/outage family, not just for one fitted realization.
    double wrong_projection_fraction = 0;
    bool noise_model_supported = false;
    bool retained_energy_bound_supported = false;
    bool wrong_projection_bound_supported = false;
    bool outage_bound_supported = false;
    // All candidates compared in one decision must have the same declared
    // rank, including duration/model alternatives, and be fixed before noise.
    bool equal_rank_fixed_bank_supported = false;

    // Computational estimate for streaming acquisition. Operations are a
    // measured or conservative per-sample/per-hypothesis input, not claimed
    // hardware throughput. Workspace is tile-bounded and does not scale with
    // the full observation duration.
    double baseband_samples_per_second = 960;
    std::uint64_t operations_per_sample_per_hypothesis = 64;
    std::uint64_t candidate_batch_size = 32;
    std::uint64_t sample_tile_size = 4096;
    std::uint64_t bytes_per_complex_sample = 8;
    std::uint64_t fixed_workspace_bytes = 1024 * 1024;
    std::uint64_t maximum_operations = UINT64_MAX;
    std::uint64_t maximum_workspace_bytes = 64 * 1024 * 1024;
};

enum class PlannerStatus {
    conditional_model_bound,
    unsupported_assumptions,
    computation_infeasible,
    no_feasible_duration,
};

struct DurationPlan {
    PlannerStatus status = PlannerStatus::unsupported_assumptions;
    bool duration_found = false;
    double duration_seconds = 0;
    double useful_energy_seconds = 0;
    double signal_noncentrality = 0;
    double guaranteed_true_score_lower = 0;
    double null_score_upper = 0;
    double wrong_score_upper = 0;
    double separation_margin = 0;
    double required_null_log_tail = 0;
    double required_correct_log_tail = 0;
    double required_wrong_log_tail = 0;
    double log_hypotheses_per_attempt = 0;
    double log_null_tests_per_horizon = 0;
    std::uint64_t attempts_per_horizon = 0;
    std::uint64_t rank = 0;
    std::uint64_t minimum_inner_coefficients = 0;
    std::uint64_t total_search_hypotheses = 0;
    bool hypothesis_count_saturated = false;
    bool operation_count_saturated = false;
    bool workspace_count_saturated = false;
    std::uint64_t estimated_operations = 0;
    std::uint64_t estimated_workspace_bytes = 0;
    const char* status_name = "unsupported_assumptions";
};

DurationPlan plan_duration(const PlannerInput&);
const char* planner_status_name(PlannerStatus);

} // namespace datapump::triad::statistics
