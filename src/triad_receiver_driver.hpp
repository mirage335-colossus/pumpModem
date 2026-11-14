#pragma once

#include "triad_receiver_fine.hpp"

namespace datapump::triad::v3::receiver_detail {

struct CoarseResult;
struct HelperResult;

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

} // namespace datapump::triad::v3::receiver_detail
