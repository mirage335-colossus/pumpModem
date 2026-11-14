#pragma once

#include "appraisal/hf_public_statistics.hpp"
#include "triad_receiver_driver.hpp"
#include "triad_probe_json.hpp"

namespace appraisal {

struct HfPublicBasisPairReport {
    std::uint64_t bank_calls=0, bank_direct_columns=0, bank_column_samples=0;
    std::uint64_t fallback_calls=0, coordinate_guard_fallbacks=0;
    std::size_t private_source_bytes=0;
    std::string native_fallback_reason;
};
// Exact optional adapter around the canonical two-source renderer. It retains
// the original MappedBasis as fallback; no waveform algorithm is copied here.
class HfPublicBasisPair {
public:
    HfPublicBasisPair(const datapump::triad::v3::ReceiverConfig&,
        const datapump::triad::v3::ReceiverSlotPlan&,const datapump::Crypto&,
        const datapump::triad::v3::receiver_detail::MappedBasisRow&,bool enable=true);
    ~HfPublicBasisPair();
    static std::size_t workspace_bytes(const datapump::triad::v3::ReceiverConfig&,
                                       const datapump::triad::v3::ReceiverSlotPlan&);
    void render(datapump::triad::v3::receiver_detail::MappedBasis&,
        std::span<const datapump::triad::v3::Tap>,std::uint64_t first,
        std::span<const double> offsets,std::span<datapump::triad::v3::Complex>,std::stop_token);
    HfPublicBasisPairReport report() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct HfPublicTimingEvidence {
    HfPublicStatisticsReport statistics;
    std::array<double, 2> energy{};
    double basis_seconds = 0;
    HfPublicBasisPairReport basis_pair;
};

struct HfPublicTimingSelection {
    std::vector<datapump::triad::v3::receiver_detail::FineCoordinate> coordinates;
    std::size_t canonical_selected_timing_atoms = 0;
    bool arrival_grid_closed = false;
};
// Pure fixed-lattice selection: optional closure adds every arrival cell only
// when there are <=3 cells, for each already retained (epoch, clock) key.
HfPublicTimingSelection select_hf_public_timing(
    const datapump::triad::v3::ReceiverConfig&, const datapump::triad::v3::ReceiverSlotPlan&,
    const datapump::triad::v3::receiver_detail::FinePreparation&, bool full_timing,
    bool close_small_arrival_grid);

struct HfPublicSearchResult {
    HfPublicStatisticsPlan plan;
    std::vector<datapump::triad::v3::receiver_detail::FineCoordinate> timing;
    std::vector<HfPublicTimingEvidence> evidence;
    std::array<std::array<double, 2>, 3> bit_log_e;
    datapump::triad::v3::receiver_detail::MappedBasisReport cache;
    std::size_t public_timing_atoms = 0, canonical_selected_timing_atoms = 0, block_rows = 0;
    bool arrival_grid_closed = false;
    std::size_t workspace_bytes = 0, basis_workspace_bytes = 0;
    std::size_t metadata_reserve_bytes = 0, statistics_actual_workspace_peak_bytes = 0;
    std::size_t statistics_shared_workspace_bytes = 0, basis_pair_workspace_bytes_per_lane = 0;
    HfPublicBasisPairReport basis_pair;
    std::uint64_t statistics_phase_values_prepared = 0;
    double statistics_preparation_seconds = 0;
    // These sums contain overlapping per-timing elapsed intervals. They are
    // diagnostic stage attribution, not CPU time or complete service time.
    double statistics_summed_worker_wall_seconds = 0, basis_summed_worker_wall_seconds = 0;
    unsigned stage_workers = 0;
    double search_seconds = 0, phase_error_bound_radians = 0;
};

// Public geometry only. Resource-lane shape and observations cannot enable or
// disable the prior. The simulator caller separately verifies the noise model.
bool hf_public_search_supported(const datapump::triad::v3::ReceiverConfig&,
                                const datapump::triad::v3::ReceiverSlotPlan&);
// Pure worst-case selected-timing quote, excluding input and worker stacks.
// All persistent cache, lane scratch and temporary result storage are charged.
std::size_t hf_public_search_workspace(const datapump::triad::v3::ReceiverConfig&,
    const datapump::triad::v3::ReceiverSlotPlan&, unsigned workers, bool include_affine = false,
    bool close_small_arrival_grid = false);
// Both the shadow diagnostic and experimental receiver call this implementation.
// The immutable preparation comes from a completed canonical public acquisition.
// Full timing is a diagnostic option; ordinary selection retains full prior mass.
HfPublicSearchResult score_hf_public_search(
    const datapump::triad::v3::ReceiverConfig&, const datapump::triad::v3::ReceiverSlotPlan&,
    const datapump::Crypto&, std::span<const datapump::triad::v3::Complex>,
    const datapump::triad::v3::receiver_detail::FinePreparation&, unsigned workers,
    std::size_t workspace_limit_bytes, bool full_timing, bool close_small_arrival_grid, bool include_affine,
    std::stop_token, datapump::triad::v3::receiver_detail::ReceiverExecutor&,
    bool use_native_pair_render = true);

// Shadow detector only: no native bit, quiet, codec or completion decision.
// The caller admits a complete, valid raw-IQ slot and verifies the simulator
// has known proper-complex IID white noise and no nonlinear receive frontend.
// No transmitted bit, channel realization or oracle template enters this API.
// total_memory_limit_bytes includes the already materialized input/process.
datapump::triad::probe::Json diagnose_hf_public_search(
    const datapump::triad::v3::ReceiverConfig&,
    const datapump::triad::v3::ReceiverSlotPlan&, const datapump::Crypto&,
    std::span<const datapump::triad::v3::Complex>, unsigned workers,
    std::size_t total_memory_limit_bytes, bool full_timing, std::stop_token);

} // namespace appraisal
