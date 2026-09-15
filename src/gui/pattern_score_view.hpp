#pragma once
#include "datapump/live.hpp"
#include <algorithm>

namespace datapump::gui {
// Display retention is independent of receiver history and physical completion.
// Stable observation IDs keep cleared candidates hidden across subsequent polls
// and replay frames, while an equal score from a new observation can appear.
class PatternScoreView {
public:
    using Clock = std::chrono::steady_clock;
    void clear_through(std::uint64_t id) { cleared_through_ = std::max(cleared_through_, id); }
    std::vector<std::complex<double>> scores(const live::Snapshot& snapshot,
                                           Clock::time_point now = Clock::now()) const {
        std::vector<std::complex<double>> result;
        const auto count = std::min({snapshot.pattern_scores.size(), snapshot.pattern_score_observations.size(),
                                     live::Snapshot::pattern_score_limit});
        result.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            const auto& observation = snapshot.pattern_score_observations[i];
            if (observation.id > cleared_through_ && now - observation.observed_at <= live::Snapshot::pattern_score_lifetime)
                result.push_back(snapshot.pattern_scores[i]);
        }
        return result;
    }
private:
    std::uint64_t cleared_through_ = 0;
};
}
