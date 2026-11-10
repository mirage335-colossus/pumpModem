#pragma once

#include "triad_receiver_detail.hpp"

namespace datapump::triad::v3::receiver_detail {

struct FineCoordinate {
    std::int64_t epoch_offset_seconds = 0;
    double arrival_seconds = 0, frequency_hz = 0, clock_ppm = 0;
    bool operator==(const FineCoordinate&) const = default;
};

std::size_t fine_workspace(const ReceiverConfig&, const ReceiverSlotPlan&, unsigned workers);
void score_fine(const ReceiverConfig&, const ReceiverSlotPlan&, const Crypto&,
                std::span<const Complex> iq, std::span<const FineCoordinate>,
                double projected_sample_rate_hz, double coarse_clock_gap_ppm,
                unsigned workers, ReceiverSlotScore&, std::stop_token);
double quiet_noise_threshold(std::size_t modes);

} // namespace datapump::triad::v3::receiver_detail
