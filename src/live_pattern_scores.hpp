#pragma once
#include "datapump/live.hpp"
#include "datapump/pattern_receiver.hpp"
#include <algorithm>
#include <array>
#include <span>
#include <tuple>

namespace datapump::live::detail {
// Presentation bookkeeping only. Candidate coordinates are complete pattern
// windows; a long unfinished symbol creates no observation or expiry event.
class PatternScoreHistory {
public:
    struct Entry {
        modem::PatternEvidence candidate;
        PatternScoreObservation observation;
    };
    template<class AllocateId>
    void update(std::span<const modem::PatternEvidence> candidates,
                std::chrono::steady_clock::time_point now, AllocateId allocate_id) {
        const auto retained = candidates.last(std::min(candidates.size(), entries_.size()));
        std::array<Entry, Snapshot::pattern_score_limit> next{};
        std::size_t count = 0;
        for (const auto& candidate : retained) {
            const auto same = [&](const Entry& entry) { return key(entry.candidate) == key(candidate); };
            const auto previous = std::find_if(entries_.begin(), entries_.begin() + size_, same);
            const auto duplicate = std::find_if(next.begin(), next.begin() + count, same);
            const auto observation = previous != entries_.begin() + size_ ? previous->observation :
                duplicate != next.begin() + count ? duplicate->observation : PatternScoreObservation{allocate_id(), now};
            next[count++] = {candidate, observation};
        }
        entries_ = next; size_ = count;
    }
    std::span<const Entry> entries() const { return std::span(entries_).first(size_); }
    double best_score(std::chrono::steady_clock::time_point now) const {
        double best = -1;
        for (const auto& entry : entries())
            if (!expired(entry.observation, now)) best = std::max(best, entry.candidate.score);
        return best;
    }
    static bool expired(const PatternScoreObservation& observation, std::chrono::steady_clock::time_point now) {
        return now - observation.observed_at > Snapshot::pattern_score_lifetime;
    }
private:
    static auto key(const modem::PatternEvidence& candidate) {
        return std::tie(candidate.first_sample, candidate.end_sample, candidate.stream_symbol,
            candidate.stream_phase_samples, candidate.frequency_hz, candidate.bit,
            candidate.score, candidate.alternative_score);
    }
    std::array<Entry, Snapshot::pattern_score_limit> entries_{};
    std::size_t size_ = 0;
};

// Rebase CPU-produced observations to their first scheduled presentation
// frame. Repeated histories keep the same age, including across gaps between
// selected receivers. Scanning bounded frames requires no extra storage.
template<class Frames>
void rebase_pattern_score_observations(Frames& frames, std::chrono::steady_clock::time_point started,
                                      std::chrono::steady_clock::duration duration) {
    for (std::size_t index = 0; index < frames.size(); ++index) {
        const auto scheduled = started + duration * index / frames.size();
        for (auto& observation : frames[index].pattern_score_observations) {
            observation.observed_at = scheduled;
            for (auto previous = index; previous > 0; --previous) {
                const auto& old = frames[previous - 1].pattern_score_observations;
                const auto found = std::find_if(old.begin(), old.end(), [&](const auto& item) {
                    return item.id == observation.id;
                });
                if (found == old.end()) continue;
                observation.observed_at = found->observed_at; break;
            }
        }
    }
}
}
