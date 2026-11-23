#pragma once

#include "triad_receiver_fine.hpp"

namespace datapump::triad::v3::receiver_detail {

struct CoarseResult;
struct HelperResult;

// Dry receipt from the same frozen complete-slot planner used by scoring.
// It does not score input or select a different resource policy.
struct ReceiverDriftAdmission {
    DriftPlan ordinary, line;
    std::size_t workspace_bytes = 0, overlap_workspace_bytes = 0;
};
ReceiverDriftAdmission receiver_drift_admission(const ReceiverConfig&, const ReceiverSlotPlan&,
                                               unsigned workers, bool overlap = false);
// An alternative complete coarse shape for an otherwise unadmittable CPU
// owner. It preserves the ordinary slot's public geometry and fine families.
std::optional<std::size_t> grouped_receiver_slot_workspace(
    const ReceiverConfig&, const ReceiverSlotPlan&, unsigned workers);

// The selected span borrows either the Helper result or the caller's fixed
// full-prior fallback. The original public grid still owns admission mass.
std::span<const FrequencyInterval> public_frequency_intervals(
    const ReceiverConfig&, const HelperResult&, FrequencyInterval& fallback);

struct FinePreparation {
    AdmissionGrid grid;
    std::vector<FineCoordinate> coordinates;
    std::vector<FineCoordinate> varying_coordinates;
    std::vector<FineCoordinate> epoch_timing_coordinates;
    std::vector<FineCoordinate> segmented_timing_coordinates;
};

// A complete coarse search selects these coordinates. Partial work must never
// supply candidates to an operational fine session.
FinePreparation prepare_fine_coordinates(const ReceiverConfig&, const ReceiverSlotPlan&, const CoarseResult&);
// Retain the diagnostics and unique coordinates of an already merged affine
// contribution together. The late continuation must see the same atom bank.
void retain_affine_atoms(FinePreparation&, ReceiverSlotScore&, const ReceiverSlotScore&,
                         std::span<const FineCoordinate>);

inline constexpr std::size_t maximum_fine_coordinates = 64;

// A failed ordinary admission can retain this fixed-size descriptor while
// waiting for its chronological predecessor. All masses are set by the
// original public model. No predecessor likelihood or gain enters it.
struct AffineContinuationContext {
    bool eligible = false, check_line = false;
    // Sequential affine/line scratch. The caller holds the input and original
    // receipt separately and uses the same worker shape as ordinary scoring.
    std::size_t scratch_bytes = 0, ordinary_count = 0;
    std::array<FineCoordinate, maximum_fine_coordinates> ordinary{};
    std::optional<AdmissionGrid> grid;
    double projected_sample_rate_hz = 0, coarse_clock_gap_ppm = 0, atom_log_mass = 0;
};

ReceiverSlotScore score_slot_for_continuation(const ReceiverConfig&, const ReceiverSlotPlan&, const Crypto&,
    std::span<const Complex>, std::span<const std::uint8_t>, unsigned, std::size_t, std::stop_token,
    ReceiverExecutor*, AffineContinuationContext&);
ReceiverSlotScore continue_affine_slot(const ReceiverConfig&, const ReceiverSlotPlan&, const Crypto&,
    std::span<const Complex>, unsigned, std::size_t, std::stop_token, ReceiverExecutor*,
    const AffineContinuationContext&, const ReceiverSlotScore::TimingProposal&, ReceiverSlotScore,
    bool protected_data = false);

// The whole packed-calendar observation supplies coarse
// proposals and full validity. Admitting statistics use only the declared
// neighbor-free interval, with the original model-family weights.
std::size_t protected_slot_workspace(const ReceiverConfig&, const ReceiverSlotPlan&, unsigned);
ReceiverSlotScore score_protected_data_slot(const ReceiverConfig&, const ReceiverSlotPlan&, const Crypto&,
    std::span<const Complex>, std::span<const std::uint8_t>, unsigned, std::size_t, std::stop_token,
    ReceiverExecutor*, AffineContinuationContext*);
// Qualification only. The caller must verify independent proper-complex
// Gaussian simulator noise at the raw input, at the configured variance.
std::size_t protected_white_beat_slot_workspace(const ReceiverConfig&, const ReceiverSlotPlan&, unsigned);
// Qualification only. Joint real-augmented Gaussian covariance bounded by
// half the declared variance and constant per-sample expected IQ power.
std::size_t protected_colored_beat_slot_workspace(const ReceiverConfig&, const ReceiverSlotPlan&, unsigned);
ReceiverSlotScore score_protected_slot(const ReceiverConfig&, const ReceiverSlotPlan&, const Crypto&,
    std::span<const Complex>, std::span<const std::uint8_t>, unsigned, std::size_t, std::stop_token,
    ReceiverExecutor*, bool discover_protected_affine = false, bool carrier_beat_polarity = false,
    bool white_beat_evidence = false, bool colored_beat_evidence = false);

// Extra raw short-track timing cells on the prepared public grid, available
// only when the coarse plan did not reserve segmented timing.
std::vector<FineCoordinate> prepare_quarter_clock_timing(const ReceiverConfig&, const ReceiverSlotPlan&,
                                                         const CoarseResult&, const AdmissionGrid&);
// Ordinary Helper-off short tracking keeps the coarse quarter-clock rows
// and supplements them with a bounded grid at the configured epoch.
std::vector<FineCoordinate> prepare_short_tracking_timing(const ReceiverConfig&, const ReceiverSlotPlan&,
                                                          const CoarseResult&, const AdmissionGrid&);

// Apply once after ordinary model selection and quiet scoring. Never feeds
// back into fallback scheduling, and can only turn DATA into an erasure.
void apply_wrong_key_admission(ReceiverSlotScore&, WrongKeyAdmission);

// Optional internal evidence extension. An application fixture may provide
// its own fixed-prior alternative without duplicating canonical acquisition,
// admission, nuisance-line handling, quiet scoring or stream completion.
// This is not a public modem configuration or a default processing branch.
struct SlotEvidenceSupplement {
    // Fixed before observing IQ, with 0 <= fixed_weight < 1. A zero weight
    // exactly disables this extension. Otherwise every earlier admission
    // reserves this mass even if the supplement is eventually skipped.
    double fixed_weight = 0;
    // Fixed public-geometry support, independent of observed IQ, available
    // lanes, admitted owner shape and backlog. False disables the extension
    // before inference; a resource-dependent decision must never restore mass.
    std::function<bool(const ReceiverConfig&, const ReceiverSlotPlan&)> supported;
    // Deterministic quote for a supported shape, excluding input and receipts.
    // Must be positive or throw; zero cannot mean a resource-based fallback.
    // Includes all cache, scratch, retained results and admitted workers.
    std::function<std::size_t(const ReceiverConfig&, const ReceiverSlotPlan&, unsigned)> workspace;
    // Return per-bit log e-values, each already charged for its complete fixed
    // public alternative prior, but without the equal bit prior or the outer
    // fixed_weight. Same-data selected cells keep their original full weights.
    // -infinity is permitted for omitted mass. NaN and +infinity are invalid.
    // The borrowed input and preparation are immutable; all leaves must join
    // before return, using only the supplied executor and admitted lane shape.
    // Auxiliary telemetry is owned and bounded by the caller. No observation-
    // dependent model fit or plug-in noise estimate is implicitly authorized.
    std::function<std::array<double, 2>(
        const ReceiverConfig&, const ReceiverSlotPlan&, const Crypto&,
        std::span<const Complex>, const FinePreparation&, unsigned, std::size_t,
        std::stop_token, ReceiverExecutor&)> score;
};

std::size_t receiver_slot_workspace_with_supplement(
    const ReceiverConfig&, const ReceiverSlotPlan&, unsigned,
    const SlotEvidenceSupplement&, bool overlap = false);
ReceiverSlotScore score_slot_with_supplement(
    const ReceiverConfig&, const ReceiverSlotPlan&, const Crypto&,
    std::span<const Complex>, std::span<const std::uint8_t>, unsigned,
    std::size_t, std::stop_token, ReceiverExecutor*, const SlotEvidenceSupplement&);

// Internal comparison for stream fixtures. Uses the normal sequential and
// overlapped workspace quotes; admitted stages still run one at a time.
ReceiverSlotScore score_slot_comparing_admitted_evidence(
    const ReceiverConfig&, const ReceiverSlotPlan&, const Crypto&,
    std::span<const Complex>, std::span<const std::uint8_t>, unsigned,
    std::size_t, std::stop_token, ReceiverExecutor*);

// Test-only comparison. Disable the stationary-plus-OU alternative before
// affine scoring and admission, using the conservative operational quote.
ReceiverSlotScore score_slot_without_stationary_fading(
    const ReceiverConfig&, const ReceiverSlotPlan&, const Crypto&,
    std::span<const Complex>, std::span<const std::uint8_t>, unsigned,
    std::size_t, std::stop_token, ReceiverExecutor*);

} // namespace datapump::triad::v3::receiver_detail
