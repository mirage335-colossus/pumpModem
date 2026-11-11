#pragma once

#include "datapump/triad_receiver.hpp"
#include "triad_receiver_bulk.hpp"
#include "triad_receiver_fine.hpp"

namespace datapump::triad::v3::receiver_detail {

struct FrequencyInterval;

struct CoarseProposal {
    FineCoordinate coordinate;
    double score = 0;
    std::uint8_t bit = 0;
    unsigned rank = 0;
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
};

struct CoarsePhaseWork {
    CoarseOperationCounts operations;
    double wall_seconds = 0;
};

struct CoarseWorkPlan {
    CoarseRoute route = CoarseRoute::frequency_tiles;
    std::uint64_t public_slot = 0, pattern_epoch = 0, symbol_ordinal = 0;
    std::uint64_t first_sample = 0, stop_sample = 0;
    unsigned workers = 0;
    std::size_t source_rows = 0, raw_template_rows = 0, projected_template_rows = 0;
    std::size_t projected_observation_rows = 0, template_sampling_step = 0;
    std::size_t fir_taps = 0, fir_fft_rows = 0, correlation_fft_rows = 0;
    std::size_t frequency_groups = 0, frequency_points = 0, arrival_points = 0;
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
    std::optional<BulkReceipt> bulk;
    std::string native_fallback_reason;
    double preparation_seconds = 0;
    std::size_t proposals = 0, refined_coordinates = 0;
};

struct CoarseResult {
    std::vector<FineCoordinate> coordinates;
    std::array<CoarseProposal, 4> proposals{};
    std::size_t proposal_count = 0;
    double projected_sample_rate_hz = 0, maximum_frequency_step_hz = 0, clock_gap_ppm = 0;
    std::size_t workspace_peak_bytes = 0, frequency_points = 0, arrival_points = 0, workspaces = 0;
    bool lag_frequency_search = false;
    std::string reason;
    CoarseProgress work;
};

// One full configured search over a fixed, immutable IQ lease. Preparation
// consumes that lease synchronously and retains the projected observation.
// Subsequent calls are confined to the owner thread, except cancel().
class CoarseSession {
public:
    CoarseSession(const ReceiverConfig&, const ReceiverSlotPlan&, const Crypto&,
                  std::span<const Complex>, std::span<const FrequencyInterval>,
                  unsigned workers, std::size_t scratch_limit_bytes, std::stop_token = {},
                  ReceiverExecutor* = nullptr);
    ~CoarseSession();
    CoarseSession(CoarseSession&&) noexcept;
    CoarseSession& operator=(CoarseSession&&) noexcept;
    CoarseSession(const CoarseSession&) = delete;
    CoarseSession& operator=(const CoarseSession&) = delete;

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
std::size_t coarse_workspace(const ReceiverConfig&, const ReceiverSlotPlan&, unsigned workers);
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
                          ReceiverExecutor* = nullptr);

} // namespace datapump::triad::v3::receiver_detail
