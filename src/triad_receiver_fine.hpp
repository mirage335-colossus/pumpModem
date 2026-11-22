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
    // Logical public-hypothesis block updates: three per model, block and
    // state. Executed updates exclude gaussian_null_skips, the common-null
    // updates proven inert and skipped (see gaussian_signal_update).
    std::uint64_t gaussian_updates = 0, gaussian_null_skips = 0, projection_modes = 0;
    long double projected_template_energy = 0;
    double retained_prior_mass = 0;
};

struct FinePhaseTimes {
    double preparation_seconds = 0, input_check_seconds = 0;
    // Native observation, transition and pruning overlap fixed-affine state
    // leaves; compiled statistics can overlap the next CPU observation.
    // Observation also runs inside dispatch. These intervals are not additive.
    // With resident windows, observation_seconds is only the owner's block
    // placement and receipts; the lanes' projection is the worker time below.
    double observation_seconds = 0, transition_seconds = 0, bulk_statistics_seconds = 0, dispatch_seconds = 0;
    double pruning_seconds = 0, finish_seconds = 0;
    // Worker elapsed times can overlap and are not CPU or service time.
    // Observation is the summed projection leaves of resident windows.
    double worker_basis_statistics_seconds = 0, worker_gaussian_seconds = 0, worker_observation_seconds = 0;
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
FineBlockGeometry fine_block_geometry(const ReceiverConfig&, const ReceiverSlotPlan&);
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
    // Consecutive blocks scored per batch, and the quoted storage bound of
    // one batch block and of one tracking coordinate's statistics in it.
    // workspace_bytes includes batch_blocks * batch_block_bytes.
    std::size_t batch_blocks = 0, batch_block_bytes = 0, batch_track_bytes = 0;
    // The preserved supplemental stage beside workspace_bytes, which the
    // receiver's quote sets: a device session's host memory, its compilation
    // and any unified-memory lease, must fit in it
    // (ReceiverBulkBackend::stage_host_bytes). Zero admits none.
    std::size_t device_envelope_bytes = 0;
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
// The signed half-bin of projected tracking state `state` of a lattice of
// step_hz about -radius on a block of `rows` rows: the bins the projected
// tracker and a varying device session score.
std::int64_t projected_half_bin(std::size_t rows, double fs, double step_hz, std::int64_t radius, std::size_t state);
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
std::size_t varying_quiet_workspace(const ReceiverConfig&, const ReceiverSlotPlan&, unsigned workers);
// Quiet-aperture blocks project on the given lanes; their energy terms are
// summed in public block order, so results do not depend on the lane count.
void score_varying_quiet(const ReceiverConfig&, const ReceiverSlotPlan&, std::span<const Complex>,
                         ReceiverSlotScore&, std::stop_token, unsigned workers, ReceiverExecutor&);

// A fixed finite bank of constant-frequency and linear-drift models. The
// retained timing atoms select only which rows of its public prior to score.
struct DriftPlan {
    std::size_t first = 0, raw_rows = 0, rows = 0, sample_stride = 1, nfft = 0;
    std::size_t slope_budget = 0, workspace_bytes = 0, projection_blocks = 0, ridge_segments = 0;
    std::size_t slow_ridge_segments = 0;
    unsigned stage_workers = 0;
    std::int64_t slope_radius = 0;
    std::int64_t frequency_radius = 0;
    double frequency_step = 0, slope_step = 0, slope_reference_seconds = 0;
    bool project_data = false;
    std::string_view status;
    // The owner-lease stage envelope this plan's stage runs within, at
    // least workspace_bytes: the largest other stage an ordinary plan widens
    // into, or an extended plan's preserved stage. A device proposal route
    // holds its own host memory only in the difference.
    std::size_t device_envelope_bytes = 0;
    // Protected projections may need the smaller block's Bluestein scratch.
    std::size_t projection_scratch_rows = 0;
};
// A positive allowance permits a bandwidth-faithful larger proposal only
// inside a stage envelope already admitted by the complete receiver.
// An ordinary (unextended) plan keeps the former width of at most eight
// lanes; it widens toward the worker budget only while its quote stays
// within the headroom, the largest other stage of the same owner lease.
// A protected observation may retain its original public lattice, quote
// only its cropped input, and remap the original slope reference to that input.
DriftPlan drift_plan(const ReceiverConfig&, const ReceiverSlotPlan&, unsigned workers, bool project_line = false,
                     std::size_t preserved_workspace_bytes = 0, std::size_t ordinary_headroom_bytes = 0,
                     const DriftPlan* public_lattice = nullptr);

// Proposal-only linear-ridge search over short coherent segment spectra (one
// fast or slow segment count). The grid, its native coarse slope order and
// the selection are shared by every ridge implementation; another backend
// may supply only the per-slope records, inserted in the same slope order.
struct DriftRidgeGrid {
    std::size_t segments = 0, fft_rows = 0, centers = 0, slopes = 0;
    double frequency_step = 0;
    std::int64_t bin_radius = 0, bin_stride = 1, slope_stride = 1;
};
// DriftRidgeRecord (triad_receiver_bulk.hpp) holds one slope's record.
struct DriftProposals {
    std::vector<std::int64_t> slopes;
    double rank = 0;
};
// Every public slope fits the exact budget: no ridge ranking is needed.
bool drift_ridge_exhaustive(const DriftPlan&, std::size_t slope_budget);
// Also writes each segment's time center, relative to the slope reference.
DriftRidgeGrid drift_ridge_grid(const DriftPlan&, double proposal_rate_hz, std::size_t segments,
                                std::span<double> segment_centers);
// Native order: the negative public edge, the interior stride grid, then the
// positive public edge.
std::int64_t drift_ridge_slope(const DriftPlan&, const DriftRidgeGrid&, std::size_t index);
// The bin offset of a slope's linear ridge at one segment center, rounded
// on the host. The native record and a device ridge request share it.
std::int64_t drift_ridge_offset(const DriftPlan&, const DriftRidgeGrid&, double segment_center, std::int64_t slope);
// The native records of a block of slopes from an FP64 power table laid
// out as [bit][segment][bin], in one pass over its rows: segments outer,
// then bit, then each slope's center window. Each (slope, bit, center) sum
// starts at zero and adds one value per segment in segment order, read
// from the upper half for a negative bin and skipped when the shifted bin
// leaves the open half band; each bit's record is the first maximum by
// ascending center. A record therefore does not depend on the block size
// or on the other slopes of its block. The block's FP64 center sums
// [slope][bit][center] use the caller's sums, at least 2 * centers per
// slope. It is the native reference of a device ridge.
inline constexpr std::size_t maximum_drift_ridge_block = 64;
void drift_ridge_records(const DriftPlan&, const DriftRidgeGrid&, std::span<const double> power,
                         std::span<const double> segment_centers, std::span<const std::int64_t> slopes,
                         std::span<DriftRidgeRecord> records, std::span<double> sums);
// The cache-bounded block of a grid: at most maximum_drift_ridge_block
// slopes whose sums fit the native block budget.
std::size_t drift_ridge_block(const DriftRidgeGrid&);
// Four best ridges per bit by strict-greater insertion in native slope
// order, then slow-plan local refinement on the same power table and the
// bounded exact slope list. An exhaustive bank needs no records or tables;
// a plan without slow segments reads no power table.
class DriftRidgeFrontier {
public:
    void insert(std::int64_t slope, const DriftRidgeRecord&);
    DriftProposals select(const DriftPlan&, const DriftRidgeGrid&, std::span<const double> power,
                          std::span<const double> segment_centers, std::size_t slope_budget) const;
private:
    struct Ridge {
        double score = -std::numeric_limits<double>::infinity();
        std::int64_t slope = 0, center = 0;
    };
    std::array<std::array<Ridge, 4>, 2> best_{};
};
// The native search: every slope's record inserted in native order, from
// consecutive blocks of at most `block` slopes (and the slope count and
// maximum_drift_ridge_block), checking cancellation once per block.
DriftRidgeFrontier drift_ridge_frontier(const DriftPlan&, const DriftRidgeGrid&, std::span<const double> power,
                                        std::span<const double> segment_centers, std::size_t block,
                                        std::span<double> sums, std::stop_token);
// One dechirped slope's rank at a signed public bin, and the eight best per
// bit retained for exact statistics.
struct DriftPeak {
    double rank = -std::numeric_limits<double>::infinity();
    std::int64_t slope = 0, center = 0;
};
inline constexpr std::size_t retained_drift_per_bit = 8;
using DriftPeakFrontier = std::array<DriftPeak, retained_drift_per_bit>;
// Stable top-eight insertion: an equal rank keeps its arrival order, and a
// NaN or -inf rank never enters. The native scan inserts every bin of each
// slope, by ascending signed bin, in scoring order. Merging each slope's
// stable top eight (equal ranks by ascending signed bin) in the same slope
// order gives the identical frontier, so a slope backend shares this rule.
void merge_drift_peak(DriftPeakFrontier&, const DriftPeak&);
std::array<double, 2> mix_drift_sign_evidence(const std::array<double, 2>& first, std::size_t first_atoms,
                                           const std::array<double, 2>& second, std::size_t second_atoms);
enum class DriftZeroSlopeEvaluation { reuse, recompute };
void score_drift_fine(const ReceiverConfig&, const ReceiverSlotPlan&, const Crypto&, const DriftPlan&,
                       std::span<const Complex>, std::span<const FineCoordinate>,
                       std::span<const FineCoordinate> epoch_timing, unsigned workers,
                       ReceiverSlotScore&, std::stop_token, ReceiverExecutor&, const LineAtom* = nullptr,
                       DriftZeroSlopeEvaluation = DriftZeroSlopeEvaluation::reuse);
void classify_fine_slot(ReceiverSlotScore&, const AdmissionGrid&);
void classify_varying_slot(ReceiverSlotScore&, const AdmissionGrid&, const AdmissionGrid*,
                           const DriftPlan*, double minimum_separation,
                           double threshold = receiver_operational_threshold());
double quiet_noise_threshold(std::size_t modes);

} // namespace datapump::triad::v3::receiver_detail
