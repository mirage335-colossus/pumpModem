#pragma once

#include "datapump/triad_receiver.hpp"
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

struct CoarseResult {
    std::vector<FineCoordinate> coordinates;
    std::array<CoarseProposal, 4> proposals{};
    std::size_t proposal_count = 0;
    double projected_sample_rate_hz = 0, maximum_frequency_step_hz = 0, clock_gap_ppm = 0;
    std::size_t workspace_peak_bytes = 0, frequency_points = 0, arrival_points = 0, workspaces = 0;
    bool lag_frequency_search = false;
    std::string reason;
};

// Bounds only the coarse stage. The caller retains the input lease and owns
// the worker allocation. Coordinates are snapped by the fine admission grid.
std::size_t coarse_workspace(const ReceiverConfig&, const ReceiverSlotPlan&, unsigned workers);
CoarseResult score_coarse(const ReceiverConfig&, const ReceiverSlotPlan&, const Crypto&,
                          std::span<const Complex>, std::span<const FrequencyInterval>,
                          unsigned workers, std::size_t scratch_limit_bytes, std::stop_token);

} // namespace datapump::triad::v3::receiver_detail
