#pragma once

#include "triad_receiver_basis.hpp"
#include "triad_receiver_bulk.hpp"
#include "triad_receiver_detail.hpp"

#include <compare>
#include <functional>

namespace datapump::triad::v3::receiver_detail {

class ReceiverExecutor;

struct FineCoordinate {
    std::int64_t epoch_offset_seconds = 0;
    double arrival_seconds = 0, frequency_hz = 0, clock_ppm = 0;
    bool operator==(const FineCoordinate&) const = default;
};

enum class FineRunKind { operational, probe, cold_maximum };
enum class AffineGainEvaluation { operational, without_stationary_fading };
enum class FineLanes { fixed_affine, fixed_and_adaptive };

inline constexpr std::array<double, receiver_independent_gain_power_count> independent_gain_powers{
    1., 1. / 16, 1. / 256, 1. / 4096};
inline constexpr std::array<double, receiver_independent_gain_power_count> independent_gain_power_priors{
    .75, 1. / 12, 1. / 12, 1. / 12};

// After consuming the two-tap statistics, reuse their storage for the
// scalar direct-path r/G, laid out as block × {null, bit 0, bit 1}.
void pack_direct_gain_statistics(std::span<Complex, 12> correlations, std::span<Complex, 24> gram,
                                 std::span<const unsigned, 2> ranks) noexcept;

struct FineObservation {
    std::uint64_t first_sample = 0;
    std::size_t rows = 0;
    FineRunKind kind = FineRunKind::operational;
    bool record_work = false;
    FineLanes lanes = FineLanes::fixed_affine;
    // Optional internal diagnostic witness, after a dispatch's projection.
    // It may run on an executor leaf: captured state must be thread-safe and
    // remain alive until advance_to has drained, including on failure. The
    // callback receives the dispatch's first row and its effective stop token;
    // it cannot change production dispatch or arithmetic choices.
    std::function<void(std::size_t, std::stop_token)> observed = {};
};

struct FineStateKey {
    unsigned group = 0;
    std::int64_t delay = 0;
    int rate = 0;
    auto operator<=>(const FineStateKey&) const = default;
};

struct FineLaneWork {
    std::vector<FineStateKey> retained_states;
    std::array<double, 2> bit_scores{};
    std::size_t initial_states = 0, state_cap = 0, proposal_cap = 0;
    std::size_t last_emission_states = 0, peak_emission_states = 0, peak_retained_states = 0;
    std::size_t peak_transition_targets = 0, pruning_events = 0, states_dropped = 0;
    std::uint64_t transition_edges = 0, merged_channel_components = 0;
    std::uint64_t basis_calls = 0, basis_rows = 0, positive_basis_blocks = 0;
    std::uint64_t gaussian_updates = 0, projection_modes = 0;
    long double projected_template_energy = 0;
    double retained_prior_mass = 0;
};

struct FinePhaseTimes {
    double preparation_seconds = 0, input_check_seconds = 0;
    // Native observation, transition and pruning overlap fixed-affine state
    // leaves; compiled statistics can overlap the next CPU observation.
    // Observation also runs inside dispatch. These intervals are not additive.
    double observation_seconds = 0, transition_seconds = 0, bulk_statistics_seconds = 0, dispatch_seconds = 0;
    double pruning_seconds = 0, finish_seconds = 0;
    // Worker elapsed times can overlap and are not CPU or service time.
    double worker_basis_statistics_seconds = 0, worker_gaussian_seconds = 0;
};

struct FineProgress {
    FineObservation observation;
    std::uint64_t public_slot = 0, pattern_epoch = 0, symbol_ordinal = 0;
    std::size_t available_rows = 0, processed_rows = 0, segments = 0, blocks = 0;
    // Nominal geometry; the public terminal segment/block can be enlarged
    // to absorb a short remainder. last_block_rows is the actual last block.
    std::size_t block_rows = 0, segment_rows = 0, last_block_rows = 0;
    std::size_t requested_invalid_rows = 0, additional_discarded_rows = 0, erased_blocks = 0;
    std::size_t observed_projection_modes = 0, quiet_modes = 0, workspace_bytes = 0;
    double sample_rate_hz = 0, delay_step_seconds = 0, rate_step_ppm = 0;
    double quiet_energy = 0, quiet_threshold = 0;
    unsigned workers = 0, stage_workers = 0, tap_count = 0;
    int rate_radius = 0;
    bool observation_complete = false, representative_work = false, finished = false;
    std::array<double, 2> slow_gain_coherence_seconds{};
    // The normalized slow-family contribution before the outer mixture.
    std::array<double, 2> slow_gain_bit_evidence{};
    FineLaneWork initial_affine, adaptive;
    MappedBasisReport cache;
    std::optional<BulkReceipt> bulk;
    FinePhaseTimes times;
};

// Uses one immutable input lease (the owner retains backing storage). The
// extent and physical coordinates are chosen before observing samples. An
// advance reveals a prefix of that lease and cannot change the terminal block,
// reset the source/clock/cache, or score an incomplete segment. All calls are
// on the owning controller thread; every leaf of an advance completes before
// it returns. Without a shared executor the session owns a private one.
class FineSession {
public:
    FineSession(const ReceiverConfig&, const ReceiverSlotPlan&, const Crypto&, FineObservation,
                std::span<const Complex> iq, std::span<const std::uint8_t> valid,
                std::span<const FineCoordinate>, double projected_sample_rate_hz,
                double coarse_clock_gap_ppm, unsigned workers, std::size_t scratch_limit_bytes,
                std::stop_token = {}, ReceiverExecutor* = nullptr,
                AffineGainEvaluation = AffineGainEvaluation::operational);
    ~FineSession();
    FineSession(const FineSession&) = delete;
    FineSession& operator=(const FineSession&) = delete;

    void advance_to(std::size_t available_rows);
    // Diagnostics and state-key copies are outside the timed service interval.
    FineProgress progress() const;
    // An abbreviated observation may finish its work but cannot finish a slot.
    void finish(ReceiverSlotScore&);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::size_t fine_workspace(const ReceiverConfig&, const ReceiverSlotPlan&, unsigned workers);
struct FineBlockGeometry {
    // Nominal geometry before the complete public observation fixes its tail.
    std::size_t block_rows = 0, segment_rows = 0;
};
FineBlockGeometry fine_block_geometry(double sample_rate_hz);
std::size_t fine_workspace(const ReceiverConfig&, const ReceiverSlotPlan&, FineObservation, unsigned workers);
void score_fine(const ReceiverConfig&, const ReceiverSlotPlan&, const Crypto&,
                std::span<const Complex> iq, std::span<const FineCoordinate>,
                double projected_sample_rate_hz, double coarse_clock_gap_ppm,
                unsigned workers, ReceiverSlotScore&, std::size_t scratch_limit_bytes, std::stop_token,
                ReceiverExecutor* = nullptr, AffineGainEvaluation = AffineGainEvaluation::operational,
                FineLanes = FineLanes::fixed_affine);
// Optional, separately normalized fixed-affine likelihood. Public geometry
// determines whether a short-block projection can separate DATA and Helper.
struct VaryingFinePlan {
    std::size_t block_rows = 0, workspace_bytes = 0;
    std::string_view status;
    bool track_frequency = false;
    bool coherent_gain_blocks = false;
    bool midpoint_tracking = false;
    // Finite unit-power spatial alternatives at the configured nominal
    // epoch, central clock and public arrival endpoints.
    std::size_t nominal_independent_gain_candidates = 0;
};
struct LineAtom {
    std::size_t first = 0, stop = 0;
    double frequency_hz = 0, log_weighted_evidence = 0;
};
std::size_t varying_line_workspace(const ReceiverConfig&, const ReceiverSlotPlan&);
std::optional<LineAtom> find_varying_line(const ReceiverConfig&, const ReceiverSlotPlan&,
                                        std::span<const Complex>, std::stop_token,
                                        double threshold = receiver_operational_threshold());
VaryingFinePlan varying_fine_plan(const ReceiverConfig&, const ReceiverSlotPlan&, unsigned workers,
                                 bool project_line = false, bool coherent_gain_blocks = false);
// Evaluator only: projections onto the exact unprojected short tracker's
// template spans. No likelihood, candidate feedback or admission decision.
struct TrackedFrequencySpanBlock {
    double captured_energy = 0, frequency_hz = 0;
    bool rank_ambiguous = false, verified = false;
};
struct TrackedFrequencySpanCoordinate {
    FineCoordinate coordinate;
    std::vector<std::array<TrackedFrequencySpanBlock, 2>> blocks;
};
struct TrackedFrequencySpan {
    std::vector<double> observation_energy;
    std::vector<TrackedFrequencySpanCoordinate> coordinates;
};
// Optional appraisal comparison in the existing supplemental timing budget.
// It appends snapped timing cells and only scores their frequency tracks.
// All ordinary results remain in ReceiverSlotScore, unchanged.
struct TrackedFrequencyClockShadow {
    std::span<const FineCoordinate> nominations;
    std::size_t ordinary_coordinates = 0, added_coordinates = 0;
    std::uint64_t added_basis_rows = 0;
    std::array<double, 2> tracked_bit_evidence{}, varying_bit_evidence{};
    double tracked_log_prior_adjustment = 0;
};
inline constexpr std::size_t maximum_midpoint_timing_coordinates = 6;
struct MidpointTrackInput {
    std::span<const FineCoordinate> nominations;
    std::size_t public_lag_count = 0;
    double original_timing_log_count = 0;
    std::span<const std::int64_t> gain_nominations{};
    bool gain_family_admitted = false;
    std::int64_t first_lag = 0;
    long double middle_seconds = 0;
};
void enable_midpoint_tracking(VaryingFinePlan&);
std::size_t tracked_frequency_span_workspace(const ReceiverSlotPlan&, const VaryingFinePlan&,
                                              bool clock_shadow = false);
std::size_t nominal_epoch_timing_capacity(const ReceiverConfig&, const ReceiverSlotPlan&);
void score_varying_fine(const ReceiverConfig&, const ReceiverSlotPlan&, const Crypto&,
                        const VaryingFinePlan&, std::span<const Complex>, std::span<const FineCoordinate>,
                        std::span<const FineCoordinate> supplemental_timing,
                        double projected_sample_rate_hz, double coarse_clock_gap_ppm, unsigned workers,
                        ReceiverSlotScore&, std::stop_token, ReceiverExecutor&, const LineAtom* = nullptr,
                        TrackedFrequencySpan* = nullptr, TrackedFrequencyClockShadow* = nullptr,
                        const MidpointTrackInput* = nullptr);
std::size_t varying_quiet_workspace(const ReceiverConfig&, const ReceiverSlotPlan&);
void score_varying_quiet(const ReceiverConfig&, const ReceiverSlotPlan&, std::span<const Complex>,
                         ReceiverSlotScore&, std::stop_token);

// A fixed finite bank of constant-frequency and linear-drift models. The
// retained timing atoms select only which rows of its public prior to score.
struct DriftPlan {
    std::size_t first = 0, raw_rows = 0, rows = 0, sample_stride = 1, nfft = 0;
    std::size_t slope_budget = 0, workspace_bytes = 0, projection_blocks = 0, ridge_segments = 0;
    std::size_t slow_ridge_segments = 0;
    std::int64_t slope_radius = 0;
    std::int64_t frequency_radius = 0;
    double frequency_step = 0, slope_step = 0, slope_reference_seconds = 0;
    bool project_data = false;
    std::string_view status;
};
DriftPlan drift_plan(const ReceiverConfig&, const ReceiverSlotPlan&, unsigned workers, bool project_line = false);
void score_drift_fine(const ReceiverConfig&, const ReceiverSlotPlan&, const Crypto&, const DriftPlan&,
                       std::span<const Complex>, std::span<const FineCoordinate>,
                       std::span<const FineCoordinate> epoch_timing, unsigned workers,
                       ReceiverSlotScore&, std::stop_token, ReceiverExecutor&, const LineAtom* = nullptr);
void classify_fine_slot(ReceiverSlotScore&, const AdmissionGrid&);
void classify_varying_slot(ReceiverSlotScore&, const AdmissionGrid&, const AdmissionGrid*,
                           const DriftPlan*, double minimum_separation,
                           double threshold = receiver_operational_threshold());
double quiet_noise_threshold(std::size_t modes);

} // namespace datapump::triad::v3::receiver_detail
