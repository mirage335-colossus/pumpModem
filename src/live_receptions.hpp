#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace datapump::live::detail {
// Different symbol durations can admit the same PCM. Share presentation state
// across those hypotheses without changing their individual admission or end
// rules. All coordinates are on the bank's sample clock, including receivers
// created after capture starts.
class ReceptionHistory {
public:
    static constexpr std::size_t capacity = 64;
    struct Candidate {
        std::uint64_t id = 0, family = 0;
        std::uint64_t first = 0, end = 0;
        double frequency = 0, frequency_tolerance = 0;
        std::uint64_t absence_samples = 0;
        double score = 0;
        bool complete = false;
    };
    struct Decision {
        std::uint64_t signal_id = 0;
        bool selected = false, limited = false;
    };

    template<class AllocateId>
    Decision observe(const Candidate& candidate, AllocateId allocate_id) {
        // The current owner's stable identity survives timing refinements and
        // lower-score chunks replayed after a whole-bank observation pass.
        auto found = std::find_if(entries_.begin(), entries_.begin() + size_, [&](const auto& entry) {
            return entry.owner.id == candidate.id && entry.owner.family == candidate.family;
        });
        if (found == entries_.begin() + size_)
            found = std::find_if(entries_.begin(), entries_.begin() + size_, [&](const auto& entry) {
                return !entry.complete && matches(entry, candidate);
            });
        if (found == entries_.begin() + size_)
            for (auto entry = entries_.begin(); entry != entries_.begin() + size_; ++entry)
                if (entry->complete && matches(*entry, candidate) &&
                    (found == entries_.begin() + size_ || entry->owner.score > found->owner.score)) found = entry;
        if (found != entries_.begin() + size_ && found->complete) {
            // A late weaker alias neither repeats completion nor extends its
            // veto into later independent PCM. A genuinely stronger symbol can
            // finish after a short prefix already ended; its newly accepted
            // bits still need a pending row, since completed rows stay complete.
            if (found->owner.id == candidate.id || candidate.score <= found->owner.score)
                return {found->signal_id, false, false};
            found = entries_.begin() + size_;
        }
        if (found != entries_.begin() + size_) {
            auto& entry = *found;
            // Retain the observed envelope even for losing hypotheses: their
            // delayed chunks still belong to this reception. Completed groups
            // match overlap only, never a later independent burst across silence.
            entry.first = std::min(entry.first, candidate.first);
            entry.end = std::max(entry.end, candidate.end);
            entry.absence_samples = std::max(entry.absence_samples, candidate.absence_samples);
            if (entry.owner.id != candidate.id) {
                // Equal evidence keeps the incumbent and avoids oscillating
                // between equivalent observations on successive progress polls.
                if (candidate.score <= entry.owner.score) return {entry.signal_id, false, false};
                entry.owner = candidate;
            } else {
                if (candidate.score >= entry.owner.score) {
                    entry.owner.frequency = candidate.frequency;
                    entry.owner.frequency_tolerance = candidate.frequency_tolerance;
                }
                entry.owner.score = std::max(entry.owner.score, candidate.score);
                entry.owner.end = std::max(entry.owner.end, candidate.end);
            }
            entry.complete = candidate.complete;
            return {entry.signal_id, true, false};
        }
        if (size_ == entries_.size()) {
            const auto retired = std::find_if(entries_.begin(), entries_.end(), [](const auto& entry) {
                return entry.complete;
            });
            if (retired == entries_.end()) return {0, false, true};
            // Entries remain in admission order, so the oldest completed
            // history is evicted while every active reception retains its ID.
            std::move(retired + 1, entries_.end(), retired);
            --size_;
        }
        auto& entry = entries_[size_++];
        entry = {candidate, allocate_id(), candidate.first, candidate.end,
                 candidate.absence_samples, candidate.complete};
        return {entry.signal_id, true, false};
    }
    std::size_t size() const { return size_; }

private:
    struct Entry {
        Candidate owner;
        std::uint64_t signal_id = 0, first = 0, end = 0, absence_samples = 0;
        bool complete = false;
    };
    static bool matches(const Entry& entry, const Candidate& candidate) {
        if (entry.owner.family != candidate.family ||
            std::abs(entry.owner.frequency - candidate.frequency) >
                std::max(entry.owner.frequency_tolerance, candidate.frequency_tolerance)) return false;
        if (candidate.first < entry.end && entry.first < candidate.end) return true;
        if (entry.complete) return false;
        const auto absence = std::max(entry.absence_samples, candidate.absence_samples);
        // Subtract ordered coordinates instead of adding silence to an end;
        // long-running clocks can be close to the uint64 limit.
        if (candidate.first >= entry.end) return candidate.first - entry.end < absence;
        return entry.first - candidate.end < absence;
    }
    std::array<Entry, capacity> entries_{};
    std::size_t size_ = 0;
};
}
