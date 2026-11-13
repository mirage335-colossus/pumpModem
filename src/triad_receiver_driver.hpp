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

} // namespace datapump::triad::v3::receiver_detail
