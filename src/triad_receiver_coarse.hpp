#pragma once

#include "datapump/triad_receiver.hpp"
#include "triad_receiver_bulk.hpp"
#include "triad_receiver_detail.hpp"
#include "triad_receiver_fine.hpp"

#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <vector>

namespace datapump::triad::v3::receiver_detail {

struct CoarseProposal {
    FineCoordinate coordinate;
    double score = 0;
    std::uint8_t bit = 0;
    unsigned rank = 0;
};

// Where a signed-lag rank's direct statistic ran: no backend, the native
// rank with a backend present, or certified device totals.
enum class GainRankRoute : std::uint8_t { none, native, device };
inline constexpr std::string_view gain_device_route = "fp32_fft_direct_rank_fp64_accumulation_fp64_rescored_frontier";

struct CoarseMidpointTiming {
    std::vector<CoarseProposal> proposals;
    std::array<CoarseProposal, receiver_signed_lag_gain_max_candidates> gain_proposals{};
    std::array<std::int64_t, receiver_signed_lag_gain_max_candidates> gain_proposal_lags{};
    // Device FFT row-stages of a published device call, apart from the
    // native gain_fft_row_stages below.
    std::uint64_t gain_device_fft_row_stages = 0;
    long double middle_seconds = 0;
    std::size_t gain_proposal_count = 0, gain_fft_rows = 0, gain_frequency_points = 0;
    std::size_t gain_fixed_proposal_count = 0;
    std::size_t gain_blocks = 0, gain_projected_rows = 0, gain_workspace_bytes = 0;
    std::size_t gain_available_blocks = 0, gain_sampled_blocks = 0, gain_verified_correlations = 0;
    std::size_t gain_direct_blocks = 0, gain_fixed_frequency_points = 0;
    std::string_view gain_frequency_status;
    std::size_t gain_tiled_blocks = 0, gain_tiled_verified_correlations = 0;
    std::uint64_t gain_fft_row_stages = 0, gain_score_rows = 0;
    std::uint64_t gain_fft_stage_bound = 0, gain_score_row_bound = 0;
    double gain_seconds = 0;
    std::string_view gain_status{};
    std::int64_t first_lag = 0, last_lag = 0;
    std::size_t lag_count = 0, workspace_bytes = 0;
    std::uint64_t widened_inverses = 0, scored_lags = 0, verified_peaks = 0;
    std::string_view precision;
    // FFT row-stages of the host's FP64 frontier rescoring, whenever it
    // ran, published or not; also apart from gain_fft_row_stages. Then a
    // certified device rank's top-two FP64 gap and rescored frontier lags.
    std::uint64_t gain_rescore_fft_row_stages = 0;
    double gain_top2_gap = 0;
    std::uint16_t gain_rescored_lags = 0;
    GainRankRoute gain_route = GainRankRoute::none;
};

// An online log mean over one block's frequency emissions. Keeping
// only the centered sum and its maximum avoids buffering a second frequency
// grid or exponentiating the original score.
struct GainFrequencyMean {
    double maximum = -std::numeric_limits<double>::infinity(), relative = 0;

    void include(double value) {
        if (value > maximum) {
            relative = 1 + relative * std::exp(maximum - value);
            maximum = value;
        } else {
            relative += std::exp(value - maximum);
        }
    }

    double log_mean(std::size_t frequencies) const {
        return maximum + std::log(relative / frequencies);
    }
};

double fixed_gain_frequency_rank(std::span<const double> frequency_major_totals,
                                 std::size_t lags, std::size_t lag);

enum class MidpointTimingEvaluation { off, short_tracking, verify };
// Every coarse entry checks that its borrowed samples are finite. Only a
// caller that has itself rejected every nonfinite sample of the same
// immutable window, as the slot scorer does, may state that it did.
enum class CoarseInputCheck { scan, already_finite };
// Pure public support. Only the ordinary unprojected short tracker uses
// these proposals. Realized clock, channel and samples never select the plan.
struct CoarseMidpointSupport {
    bool available;
    std::string_view status;
    std::int64_t first_lag = 0, last_lag = 0;
    long double middle_seconds = 0;
    std::size_t projected_rate = 0;
    std::string_view gain_status{};
    std::size_t gain_available_blocks = 0, gain_sampled_blocks = 0, gain_direct_blocks = 0;
    std::size_t gain_fft_rows = 0, gain_frequency_points = 0, gain_workspace_bytes = 0;
    std::uint64_t gain_fft_stage_bound = 0, gain_score_row_bound = 0;
    std::size_t gain_fixed_frequency_points = 0;
    std::string_view gain_frequency_status;
};
CoarseMidpointSupport coarse_midpoint_timing_support(const ReceiverConfig&, const ReceiverSlotPlan&, unsigned workers = 1);
FineCoordinate midpoint_timing_coordinate(const ReceiverSlotPlan&, long double middle_seconds,
    double clock_bound_ppm, std::size_t projected_rate, std::int64_t lag, std::int64_t epoch);

// ---- Signed-lag gain rank: exact shift runs, device seam, certification ----

// Public geometry of the signed-lag rank's rounded per-block lag shifts. The
// field types are those of the coarse plan, so every evaluation performs the
// same long-double operations as the original per-block loop.
struct GainShiftGeometry {
    std::size_t output_fs = 0, fs = 0, taps = 0, block_rows = 0, kernel_rows = 0;
    long double origin = 0;
    double arrival_lower_seconds = 0;
};

// shift() is the only definition of a lag's rounded offset in one block. Each
// rounded operation is monotone in the block centre time, which increases
// with the block, so a lag whose shift is equal at two blocks keeps it in
// every block between them. Runs reuse one evaluated vector over such spans.
// The map borrows the coordinates, which must outlive it.
class GainShiftMap {
public:
    GainShiftMap(const GainShiftGeometry&, std::span<const FineCoordinate>);
    std::size_t blocks() const noexcept { return blocks_; }
    std::size_t lags() const noexcept { return coordinates_.size(); }
    const GainShiftGeometry& geometry() const noexcept { return geometry_; }
    long double time(std::size_t block) const noexcept;
    std::int64_t shift(std::size_t lag, long double time) const noexcept;
    std::int64_t shift(std::size_t lag, std::size_t block) const noexcept { return shift(lag, time(block)); }
    // All lags at one block.
    void fill(std::size_t block, std::span<std::int64_t> shifts) const;
    // First block in (block, limit) whose shift differs from `value`, the
    // lag's shift at `block`; `limit` if none. Binary search over blocks.
    std::size_t lag_end(std::size_t lag, std::size_t block, std::int64_t value, std::size_t limit) const noexcept;
    // First block after `block` at which any shift differs from `shifts`, the
    // vector at `block`, or blocks(). Once that block is known to be at most
    // `horizon` (the caller's next block) the search stops and returns
    // block + 1. The vector is exact on [block, result) either way. The
    // lag with the largest |clock| changes fastest and is searched first.
    std::size_t run_end(std::size_t block, std::span<const std::int64_t> shifts, std::size_t horizon) const;
private:
    GainShiftGeometry geometry_;
    std::span<const FineCoordinate> coordinates_;
    std::size_t blocks_ = 0, fastest_ = 0;
};

// One rank lane's cached shift vector, valid on [begin, end). Lanes visit
// blocks in increasing order within a pass; a block outside the cached run
// evaluates the vector there and searches its run end.
struct GainShiftCursor {
    std::size_t begin = 0, end = 0;
    std::int64_t first_shift = 0, last_shift = 0;
    bool order_known = false, ordered = true;
    std::uint64_t vectors = 0;
    // Returns true when `shifts` was re-evaluated.
    bool seek(const GainShiftMap&, std::size_t block, std::size_t horizon, std::span<std::int64_t> shifts);
};

// Every run of an identical shift vector, for a device block table. With
// more than `maximum_runs` runs (count = maximum_runs + 1), or a run whose
// lags span more than the int32 relative-shift range (`range`), `overflow`
// is set and the tables are empty.
struct GainShiftRuns {
    std::vector<std::size_t> first_blocks;               // ascending; first is 0
    std::vector<std::int64_t> first_shift, last_shift;   // per run
    std::vector<std::int32_t> relative;                  // [run][lag]: shift - first_shift
    std::size_t count = 0;                               // runs found, including an overflowing one
    bool overflow = false, range = false;
    std::size_t run(std::size_t block) const;
};
GainShiftRuns gain_shift_runs(const GainShiftMap&, std::size_t maximum_runs, std::stop_token = {});

// Device direct rank seam: ReceiverBulkBackend::make_gain_rank
// admits a GainRankBulk session for one rank's GainRankShape. The session
// scores the direct statistic of every unsampled block and, as audit, of
// every sampled block, from GainRankBlock tables in a GainRankRequest
// (triad_receiver_bulk.hpp). The receiver records its own reasons with
// record_stage_native; a factory or session records the backend's.

// Predeclared certification constants (docs/triad-gpu-optimization.md,
// "Signed-lag ranks").
struct GainCertificationLimits {
    double band_sigmas = 8;                      // k
    std::size_t frontier_minimum = 8, frontier_maximum = 64;
    double near_tie = 1e-9;                      // relative top-two gap
    std::size_t maximum_shift_runs = 64;
    std::size_t rescore_chunk_blocks = 1024;     // fixed partition, independent of workers
};
struct GainAuditStatistics {
    std::size_t count = 0;
    double magnitude = 0;                        // RMS of native values
    double deviation = 0;                        // sample standard deviation of device - native
};
struct GainCertifiedBit {
    GainAuditStatistics audit;
    // The bit's band is the largest lag band, at band_lag, or the floor.
    double band = 0, audit_band = 0, floor_band = 0;
    // Largest rescored |T - R| and its largest ratio to the lag's own band.
    double maximum_error = 0, band_ratio = 0, top2_gap = 0, tie_threshold = 0;
    // Largest device total outside the frontier, which must stay below
    // max T - 2 band by more than the best lag's roundoff bound.
    double excluded = -std::numeric_limits<double>::infinity();
    std::string_view band_source;
    std::size_t frontier = 0, best_lag = 0, band_lag = 0;
};
struct GainCertificationInput {
    std::size_t lags = 0, unsampled_blocks = 0, scored_blocks = 0;
    std::span<const double> sampled;           // S [2][lags]: native sampled-block sums, lane order
    std::span<const double> device;            // D [2][lags]: device unsampled sums
    std::span<const double> audit_native;      // [audit][2][lags]
    std::span<const float> audit_device;       // [audit][2][lags]
};
// U and sum |v| over the unsampled blocks, FP64, for the frontier lags of each bit.
using GainFrontierRescore = std::function<void(std::array<std::span<const std::size_t>, 2> lags,
    std::array<std::span<double>, 2> sums, std::array<std::span<double>, 2> magnitudes)>;
// Why a signed-lag direct rank stayed native with a backend present: the
// receiver's own StageReason suffixes. A backend decline or fallback
// carries the backend's own reason instead.
inline constexpr std::string_view gain_direct_verify_mode = stage_reason_suffix(StageReason::verify_mode);
inline constexpr std::string_view gain_direct_no_rank = stage_reason_suffix(StageReason::no_direct_rank);
inline constexpr std::string_view gain_direct_no_unsampled = stage_reason_suffix(StageReason::no_unsampled_blocks);
inline constexpr std::string_view gain_direct_workspace = stage_reason_suffix(StageReason::workspace);
inline constexpr std::string_view gain_direct_shift_runs = stage_reason_suffix(StageReason::shift_runs);
inline constexpr std::string_view gain_direct_shift_range = stage_reason_suffix(StageReason::shift_range);
inline constexpr std::string_view gain_direct_nonfinite_input = stage_reason_suffix(StageReason::nonfinite_input);
inline constexpr std::string_view gain_certification_no_audit = stage_reason_suffix(StageReason::audit_unavailable);
inline constexpr std::string_view gain_certification_nonfinite = stage_reason_suffix(StageReason::nonfinite_device);
inline constexpr std::string_view gain_certification_overflow = stage_reason_suffix(StageReason::frontier_overflow);
inline constexpr std::string_view gain_certification_band = stage_reason_suffix(StageReason::band_violated);
inline constexpr std::string_view gain_certification_tie = stage_reason_suffix(StageReason::near_tie);
struct GainCertification {
    std::string_view fallback;                  // empty: certified
    std::array<GainCertifiedBit, 2> bits{};
    std::size_t frontier_lags = 0, rescored_lags = 0;
    std::size_t host_bytes = 0;                 // held by the certification beside the rescoring
    std::vector<double> totals;                 // T = S + D [2][lags]
    std::vector<double> nomination;             // certified: R on the frontier, -inf elsewhere
};
// Decides whether device totals may publish. The published argmax per bit
// equals native FP64 at equal workers whenever the band holds; a band
// self-check, overflow, near-tie (the top two lags or the frontier boundary
// within roundoff) or nonfinite value requests native instead.
GainCertification certify_gain_direct_rank(const GainCertificationInput&, const GainCertificationLimits&,
                                           const GainFrontierRescore&);

// One signed-lag rank's diagnostics. They stay outside the retained timing
// result; only coarse_gain_rank_trial() reads them. The timing result keeps
// the route, device and rescoring FFT row-stages, rescored lags and top-two
// gap, and the backend receipt the frontier, rescoring time and band source.
struct GainDirectReport {
    std::string_view route = "native";          // "device" only for certified nominations
    // Why native with a backend present: the backend's policy decline, the
    // receiver's reason suffix, or, after a factory or session recorded the
    // decline or fallback, the backend receipt's reason of the kind whose
    // counter grew. The receipt keeps the first reason of each kind, so once
    // an earlier call recorded one of that kind, this is the earlier call's
    // reason, not necessarily this call's.
    std::string reason;
    bool fallback = false;                      // the reason followed a session's call
    std::uint64_t shift_vectors = 0;            // full shift-vector evaluations by the rank lanes
    std::size_t shift_runs = 0, device_blocks = 0, frontier_lags = 0, rescored_lags = 0;
    std::size_t device_host_bytes = 0, device_host_held = 0;   // quote and held peak
    // "audit" or "fp32-floor": the band term of the bit with the larger
    // band (bit 0 on a tie). It need not be the term of the lag band behind
    // band_ratio, the larger of the two bits' largest rescored error ratios.
    std::string_view band_source;
    double rescore_seconds = 0, top2_gap = 0, band_ratio = 0;
};

struct CoarseArrivalPeaks {
    std::array<std::size_t, 2> indices{};
    std::size_t count = 0;
};

CoarseArrivalPeaks select_coarse_arrivals(std::span<const double> scores, std::size_t separation);

enum class CoarseState { prepared, searching, searched, finished, cancelled, failed };

struct CoarseOperationCounts {
    // Logical complex transforms, with row-stages equal to sum(N*log2(N)).
    // A compiled backend may batch these; its device receipt reports calls.
    std::uint64_t forward_ffts = 0, inverse_ffts = 0, fft_rows = 0, fft_row_stages = 0;
    std::uint64_t source_render_calls = 0, source_render_rows = 0, projected_columns = 0;
    std::uint64_t fir_input_rows = 0, fir_output_contributions = 0;
    std::uint64_t workspace_frequency_pairs = 0, score_rows = 0, singleton_dot_terms = 0;
    std::uint64_t retained_peaks = 0, selection_peak_rows = 0;
    bool operator==(const CoarseOperationCounts&) const = default;
};

struct CoarsePhaseWork {
    CoarseOperationCounts operations;
    // The template bank, observation filter and mapped-profile gate overlap
    // on one lane set. Each reports its own interval; these are not additive.
    double wall_seconds = 0;
};

struct CoarseWorkPlan {
    CoarseRoute route = CoarseRoute::frequency_tiles;
    std::uint64_t public_slot = 0, pattern_epoch = 0, symbol_ordinal = 0;
    std::uint64_t first_sample = 0, stop_sample = 0;
    unsigned workers = 0;
    std::size_t source_rows = 0, raw_template_rows = 0, projected_template_rows = 0;
    std::size_t projected_observation_rows = 0, template_sampling_step = 0;
    // Prospective exact preparation work. Zero on the mapped route.
    std::size_t exact_render_and_projected_rows = 0, exact_raw_column_rows = 0;
    std::uint64_t exact_preparation_fft_row_stages = 0;
    std::size_t fir_taps = 0, fir_fft_rows = 0, correlation_fft_rows = 0;
    std::size_t frequency_groups = 0, frequency_points = 0, arrival_points = 0;
    std::size_t segmented_timing_blocks = 0, segmented_timing_frequencies = 0, segmented_timing_hop = 0;
    std::size_t workspaces = 0, rays = 0, clocks = 0;
    // One tile advances either adjacent CFOs for all workspaces or adjacent
    // workspaces for all CFOs. The final tile may be smaller.
    std::size_t tile_width = 0, total_tiles = 0;
    std::optional<CoarseFrequencyPlan> bulk_frequency_admission;
    std::size_t workspace_reserved_bytes = 0, workspace_limit_bytes = 0, bulk_host_workspace_bytes = 0;
    double input_sample_rate_hz = 0, projected_sample_rate_hz = 0;
    double maximum_frequency_step_hz = 0, clock_gap_ppm = 0;
    double arrival_lower_seconds = 0, arrival_upper_seconds = 0;
    bool mapped_fidelity_native_fp64 = false;
    std::string fft_precision = "fp64", precision_reason;
    double minimum_eigenvalue_ratio = 1;
    int observation_scale_exponent = 0;
    std::size_t scaled_template_workspaces = 0;
};

struct CoarseProgress {
    CoarseWorkPlan plan;
    CoarseState state = CoarseState::prepared;
    std::size_t completed_tiles = 0, retained_bytes = 0;
    // Operations count completed native work. An interrupted tile does not
    // publish work credit or candidates. Wall time includes interrupted work.
    CoarsePhaseWork filter_setup, mapped_fidelity, template_bank, observation, lag_setup, bulk_prepare, search, selection;
    // Native differential products after channel isolation. This proposal
    // pass has no statistical admission authority or device-work credit.
    CoarsePhaseWork varying_proposals;
    CoarsePhaseWork signed_gain_proposal;
    std::size_t segmented_timing_blocks = 0, segmented_timing_frequencies = 0;
    std::string_view segmented_timing_status;
    std::optional<BulkReceipt> bulk;
    std::string native_fallback_reason;
    double preparation_seconds = 0;
    std::size_t proposals = 0, refined_coordinates = 0;
};

struct CoarseResult {
    std::vector<FineCoordinate> coordinates;
    std::vector<FineCoordinate> varying_coordinates;
    std::vector<FineCoordinate> segmented_timing_coordinates;
    std::vector<FineCoordinate> epoch_timing_coordinates;
    std::array<CoarseProposal, 4> proposals{};
    std::size_t proposal_count = 0;
    double projected_sample_rate_hz = 0, maximum_frequency_step_hz = 0, clock_gap_ppm = 0;
    std::size_t workspace_peak_bytes = 0, frequency_points = 0, arrival_points = 0, workspaces = 0;
    bool lag_frequency_search = false;
    std::string reason;
    CoarseProgress work;
    std::optional<CoarseMidpointTiming> midpoint_timing;
};

// Optional shortlist for the short frequency tracker when its segmented
// timing geometry is unavailable. The fine preparation still snaps these
// points to the original public grid. No search or receiver decision here.
std::vector<FineCoordinate> nominate_quarter_clock_timing(
    const ReceiverConfig&, const ReceiverSlotPlan&, const CoarseResult&);

// What frequency acquisition adds while it overlaps preparation: its live
// scratch, and the most jobs any one of its waves runs at once.
struct AssistanceBound {
    std::size_t workspace_bytes = 0, widest_wave = 0;
};

// Frequency intervals that are still being acquired when a session is built.
// The session runs `acquire` once, during its construction. Its owner thread
// does so with a wave on the session's own lanes; when the mapped-profile
// gate needs the owner, one of those lanes does so with no wave. The
// returned intervals must outlive construction.
struct FrequencyAssistance {
    std::function<std::span<const FrequencyInterval>(const LaneWave&, std::stop_token)> acquire;
    AssistanceBound bound;
};

// Optional in-place observation mask for a native proposal-only pass.
// Bounds are relative to the complete public window. Geometry, template
// support and the configured coarse search grid remain unchanged.
struct CoarseInputSupport {
    std::size_t first, stop;
};

// One full configured search over a fixed, immutable IQ lease. Preparation
// consumes that lease synchronously and retains the projected observation.
// Subsequent calls are confined to the owner thread, except cancel(). Without
// a shared executor the session owns a private one for its admitted lanes.
class CoarseSession {
public:
    CoarseSession(const ReceiverConfig&, const ReceiverSlotPlan&, const Crypto&,
                  std::span<const Complex>, std::span<const FrequencyInterval>,
                  unsigned workers, std::size_t scratch_limit_bytes, std::stop_token = {},
                  ReceiverExecutor* = nullptr, MidpointTimingEvaluation = MidpointTimingEvaluation::off,
                  CoarseInputCheck = CoarseInputCheck::scan,
                  std::optional<CoarseInputSupport> = std::nullopt);
    // Acquisition runs beside the template bank and observation filter. This
    // requires coarse_defers_frequency_grid(). Results are identical to a
    // session given the acquired intervals.
    CoarseSession(const ReceiverConfig&, const ReceiverSlotPlan&, const Crypto&,
                  std::span<const Complex>, const FrequencyAssistance&,
                  unsigned workers, std::size_t scratch_limit_bytes, std::stop_token = {},
                  ReceiverExecutor* = nullptr, MidpointTimingEvaluation = MidpointTimingEvaluation::off,
                  CoarseInputCheck = CoarseInputCheck::scan);
    ~CoarseSession();
    CoarseSession(CoarseSession&&) noexcept;
    CoarseSession& operator=(CoarseSession&&) noexcept;
    CoarseSession(const CoarseSession&) = delete;
    CoarseSession& operator=(const CoarseSession&) = delete;

    // Native tiles of one call are searched as a single span. An interrupted
    // call publishes none of them.
    void advance(std::size_t maximum_tiles);
    CoarseState state() const noexcept;
    CoarseProgress progress() const;
    std::span<const double> frequency_grid() const;
    // Selection/refinement are deferred until every public pair has been
    // scored. An unfinished, cancelled or failed session returns no result.
    std::optional<CoarseResult> finish();
    void cancel() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Bounds only the coarse stage. The caller retains the input lease and owns
// the worker allocation. Coordinates are snapped by the fine admission grid.
// An overlapped acquisition is charged beside the whole of preparation.
std::size_t coarse_workspace(const ReceiverConfig&, const ReceiverSlotPlan&, unsigned workers,
                             AssistanceBound overlapped = {},
                             MidpointTimingEvaluation = MidpointTimingEvaluation::off);
struct CoarseWorkspace {
    std::size_t host_bytes, drift_envelope_bytes;
};
CoarseWorkspace coarse_workspace_bounds(const ReceiverConfig&, const ReceiverSlotPlan&, unsigned workers,
    AssistanceBound overlapped, MidpointTimingEvaluation);
// Public geometry alone decides whether preparation can precede the grid.
bool coarse_defers_frequency_grid(const ReceiverConfig&, const ReceiverSlotPlan&);
// Geometry and workspace quote from the same planner as a scoring session.
// No input, preparation, arithmetic selection or device work is involved.
CoarseWorkPlan coarse_work_plan(const ReceiverConfig&, const ReceiverSlotPlan&,
                                std::span<const FrequencyInterval>, unsigned workers);
struct CoarseFineGeometry {
    double projected_sample_rate_hz = 0, maximum_frequency_step_hz = 0, clock_gap_ppm = 0;
};
// Pure public geometry for bounded non-admitting fine work probes.
CoarseFineGeometry coarse_fine_geometry(const ReceiverConfig&, const ReceiverSlotPlan&);
CoarseResult score_coarse(const ReceiverConfig&, const ReceiverSlotPlan&, const Crypto&,
                          std::span<const Complex>, std::span<const FrequencyInterval>,
                          unsigned workers, std::size_t scratch_limit_bytes, std::stop_token,
                          ReceiverExecutor* = nullptr, MidpointTimingEvaluation = MidpointTimingEvaluation::off,
                          CoarseInputCheck = CoarseInputCheck::scan,
                          std::optional<CoarseInputSupport> = std::nullopt);
CoarseResult score_coarse(const ReceiverConfig&, const ReceiverSlotPlan&, const Crypto&,
                          std::span<const Complex>, const FrequencyAssistance&,
                          unsigned workers, std::size_t scratch_limit_bytes, std::stop_token,
                          ReceiverExecutor* = nullptr, MidpointTimingEvaluation = MidpointTimingEvaluation::off,
                          CoarseInputCheck = CoarseInputCheck::scan);

// Test-only: the signed-lag rank's shift geometry, lag coordinates, sampled
// blocks (in sample order) and lane counts as planned for this slot.
struct CoarseGainShiftSetup {
    GainShiftGeometry geometry;
    std::vector<FineCoordinate> coordinates;
    std::vector<std::size_t> sampled;
    std::size_t blocks = 0, fixed_lanes = 0, rank_lanes = 0;
    bool full_direct = false;
    std::size_t observation_rows = 0;
};
CoarseGainShiftSetup coarse_gain_shift_setup(const ReceiverConfig&, const ReceiverSlotPlan&, unsigned workers);

// Test-only: one score_coarse call whose signed-lag direct rank may use
// `backend` (null: native only). Its device host buffers are admitted only
// within `limit` beyond the native coarse quote (`device_host_bytes`).
// Reports the reduced lane totals (native, or the sampled part S when
// certified), device totals, both audit copies, the certification and the
// rank's diagnostics.
struct CoarseGainRankTrial {
    CoarseResult coarse;
    std::vector<double> fixed_totals, direct_totals, device_totals, audit_native;
    std::vector<float> audit_device;
    std::optional<GainCertification> certification;
    GainDirectReport report;
    std::size_t native_quote = 0, device_host_bytes = 0;
};
CoarseGainRankTrial coarse_gain_rank_trial(const ReceiverConfig&, const ReceiverSlotPlan&, const Crypto&,
                                           std::span<const Complex>, std::span<const FrequencyInterval>,
                                           unsigned workers, std::size_t limit, MidpointTimingEvaluation,
                                           ReceiverBulkBackend*, const GainCertificationLimits& = {});

// Test-only check of the chunked native observation filter. A synthetic
// Kaiser plan with `taps` taps and the given decimation filters `iq`
// serially, as chunk jobs on an executor of `workers` lanes, and as chunk
// jobs on one lane in an order shuffled by `seed`. Coarse preparation runs
// the same chunk jobs and ordered combine.
struct CoarseFirChunkTrial {
    std::vector<Complex> serial, lanes, shuffled;
    std::array<CoarseOperationCounts, 3> operations{};
    std::size_t chunks = 0, staged_rows = 0, completions = 0;
};
CoarseFirChunkTrial coarse_fir_chunk_trial(std::span<const Complex> iq, std::size_t decimation, std::size_t taps,
                                           std::size_t crop_first, std::size_t crop_rows, unsigned workers,
                                           std::uint64_t seed);

} // namespace datapump::triad::v3::receiver_detail
