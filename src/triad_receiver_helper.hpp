#pragma once

#include "triad_receiver_detail.hpp"
#include <array>
#include <string>

namespace datapump::triad::v3::receiver_detail {

// Engineering frequency assistance only. An empty interval list preserves
// the complete original frequency prior. Neither onset nor pilot fitting
// supplies a DATA decision, a clock estimate, or a common phase reference.
struct HelperPilotDiagnostic {
    bool usable = false;
    double best_shift_hz = 0, presence_score = 0, peak_background_ratio = 0;
};
struct HelperResult {
    bool narrowed = false;
    std::string reason;
    std::vector<FrequencyInterval> intervals;
    std::vector<std::array<double, 2>> onset_intervals;
    std::array<HelperPilotDiagnostic, 2> coarse_pilots{};
    double onset_noise_level = 0, onset_helper_level = 0;
    double onset_best_arrival_seconds = 0, frequency_observation_seconds = 0;
    std::size_t onset_apertures = 0, prefix_samples = 0, workspace_peak_bytes = 0;
};

// The most jobs one acquisition wave runs at once.
inline constexpr std::size_t helper_widest_wave = 4;

// Scratch only: the caller separately owns and charges the immutable input.
// The bound follows the public calendar. Steps that run at once use only
// what the largest single step already needs, so it has no lane term.
std::size_t helper_workspace(const ReceiverConfig&, const ReceiverSlotPlan&);
// A supplied wave runs the independent pilot fits, target refinements and
// branch filters on its owner's lanes. The result is the same without one.
HelperResult score_helper(const ReceiverConfig&, const ReceiverSlotPlan&,
                          std::span<const Complex>, std::span<const std::uint8_t>,
                          std::size_t scratch_limit_bytes, std::stop_token = {}, const LaneWave& = {});

} // namespace datapump::triad::v3::receiver_detail
