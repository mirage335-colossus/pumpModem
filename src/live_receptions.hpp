#pragma once
#include "datapump/symbol_burst.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <optional>

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
        double score = 0; // Arbitration support, not a displayed native score.
        bool complete = false;
        // Optional helper-derived reception never suppresses compatible
        // ordinary progress, even when its earlier prefix has more support.
        // Stored in the existing alignment padding before signal_id.
        bool helper_assisted = false;
        std::uint64_t signal_id = 0, symbol_samples = 0, timing_tolerance = 0;
        // Valid only for amplitude candidates. These are actual retained FFT
        // passbands; their reported centers are not coherent carrier estimates.
        double amplitude_low_hz = 0, amplitude_high_hz = 0;
        // Exact source-calendar identity for optional Burst continuation.
        // Physical overlap, evidence and completion still use scored PCM.
        std::optional<modem::SymbolBurstCoordinate> burst_first, burst_next;
    };
    struct Decision {
        std::uint64_t signal_id = 0;
        bool selected = false, limited = false;
        std::uint64_t revision = 0;
        // This view remains valid until the next observation. Consumers copy
        // it into every selected update, so event coalescing cannot lose a merge.
        std::span<const std::uint64_t> superseded_ids;
    };

    template<class AllocateId>
    Decision observe(const Candidate& candidate, AllocateId allocate_id) {
        auto observed = candidate;
        std::size_t identity = size_;
        for (std::size_t i = 0; i < size_; ++i) {
            const auto& entry = entries_[i];
            if (entry.owner.family != candidate.family) continue;
            if (entry.owner.id == candidate.id) { identity = i; break; }
            if (candidate.signal_id && contains(entry, candidate.signal_id)) identity = i;
        }
        if (identity < size_ && entries_[identity].owner.id == candidate.id) {
            auto& entry = entries_[identity];
            // A completed owner cannot be reopened by the bank's provisional
            // pass or by replaying its earlier chunks. A different, stronger
            // hypothesis may revise that interpretation under the same row ID.
            if (entry.complete) return decision(entry, false);
            observed.first = std::min(observed.first, entry.owner.first);
            observed.end = std::max(observed.end, entry.owner.end);
            observed.absence_samples = std::max(observed.absence_samples, entry.owner.absence_samples);
            if(entry.owner.burst_next && (!observed.burst_next || *observed.burst_next<*entry.owner.burst_next))
                observed.burst_next=entry.owner.burst_next;
            if (observed.score < entry.owner.score) {
                observed.frequency = entry.owner.frequency;
                observed.frequency_tolerance = entry.owner.frequency_tolerance;
                observed.amplitude_low_hz = entry.owner.amplitude_low_hz;
                observed.amplitude_high_hz = entry.owner.amplitude_high_hz;
            }
            observed.score = std::max(observed.score, entry.owner.score);
        }

        std::array<bool, capacity> matched{};
        std::size_t selected = size_, count = 0;
        for (std::size_t i = 0; i < size_; ++i) {
            if (i != identity && !matches(entries_[i], observed)) continue;
            matched[i] = true; ++count;
            if (stronger(entries_[i].owner, observed) &&
                (selected == size_ || stronger(entries_[i].owner, entries_[selected].owner))) selected = i;
        }
        if (selected < size_) {
            // A losing observation never extends a group's span, silence
            // allowance or frequency range. In particular, a weak bridge must
            // not merge two distinct stronger receptions on opposite sides.
            return decision(entries_[selected], false);
        }
        if (count) {
            std::size_t oldest = size_;
            for (std::size_t i = 0; i < size_; ++i)
                if (matched[i] && (oldest == size_ || entries_[i].signal_id < entries_[oldest].signal_id)) oldest = i;
            auto combined = entries_[oldest];
            const auto limited = [&] {
                // Alias pressure may prevent merging another historical row,
                // but cannot withhold accepted progress from an existing owner.
                if (identity < size_ && entries_[identity].owner.id == observed.id) {
                    auto& existing = entries_[identity];
                    existing.owner = observed; existing.complete = observed.complete;
                    return decision(existing, true, true);
                }
                return decision(entries_[oldest], false, true);
            };
            for (std::size_t i = 0; i < size_; ++i) {
                if (!matched[i] || i == oldest) continue;
                if (!add_alias(combined, entries_[i].signal_id)) return limited();
                for (const auto alias : aliases(entries_[i]))
                    if (!add_alias(combined, alias)) return limited();
            }
            const bool changed = combined.owner.id != observed.id || count > 1;
            combined.owner = observed;
            combined.complete = observed.complete;
            if (changed) combined.revision = ++revision_;
            // Only the selected owner's fully scored observations define the
            // envelope. Historical/losing spans must not poison later matches.
            entries_[oldest] = combined;
            std::size_t destination = 0, result = 0;
            for (std::size_t i = 0; i < size_; ++i) {
                if (matched[i] && i != oldest) continue;
                if (i == oldest) result = destination;
                if (destination != i) entries_[destination] = entries_[i];
                ++destination;
            }
            size_ = destination;
            return decision(entries_[result], true);
        }
        if (size_ == entries_.size()) {
            const auto retired = std::find_if(entries_.begin(), entries_.end(), [](const auto& entry) {
                return entry.complete || entry.retired;
            });
            if (retired == entries_.end()) return {0, false, true, 0, {}};
            // Entries remain in admission order. Preserve all active rows and
            // retire the oldest completed or producer-free history at the bound.
            std::move(retired + 1, entries_.end(), retired);
            --size_;
        }
        auto& entry = entries_[size_++];
        entry = {};
        entry.owner = observed;
        entry.signal_id = allocate_id();
        entry.revision = ++revision_;
        entry.complete = observed.complete;
        return decision(entry, true);
    }
    std::size_t size() const { return size_; }
    // A producer-free row remains incomplete. Keep its overlap history until
    // capacity is needed, but it cannot reserve storage or bridge a later gap.
    template<class Retained> void retire_unreferenced(Retained retained) {
        for(std::size_t i=0;i<size_;++i) {
            auto& entry=entries_[i];
            entry.retired=!retained(entry.signal_id) &&
                std::none_of(aliases(entry).begin(),aliases(entry).end(),retained);
        }
    }

private:
    struct Entry {
        Candidate owner;
        std::uint64_t signal_id = 0, revision = 0;
        std::array<std::uint64_t, capacity> superseded_ids{};
        std::size_t superseded_count = 0;
        bool complete = false, retired = false;
    };
    static std::span<const std::uint64_t> aliases(const Entry& entry) {
        return {entry.superseded_ids.data(), entry.superseded_count};
    }
    static bool contains(const Entry& entry, std::uint64_t id) {
        return entry.signal_id == id || std::find(aliases(entry).begin(), aliases(entry).end(), id) != aliases(entry).end();
    }
    static bool add_alias(Entry& entry, std::uint64_t id) {
        if (contains(entry, id)) return true;
        if (entry.superseded_count == capacity) return false;
        entry.superseded_ids[entry.superseded_count++] = id;
        std::sort(entry.superseded_ids.begin(), entry.superseded_ids.begin() + entry.superseded_count);
        return true;
    }
    static Decision decision(Entry& entry, bool selected, bool limited = false) {
        entry.retired=false;
        return {entry.signal_id, selected, limited, entry.revision, aliases(entry)};
    }
    static bool stronger(const Candidate& a, const Candidate& b) {
        if(a.helper_assisted!=b.helper_assisted)return !a.helper_assisted;
        if (a.score != b.score) return a.score > b.score;
        // Intrinsic geometry resolves equal evidence independently of target
        // order. Candidate identity only breaks otherwise indistinguishable fits.
        if (a.symbol_samples != b.symbol_samples) return a.symbol_samples > b.symbol_samples;
        if (a.first != b.first) return a.first < b.first;
        if (a.frequency != b.frequency) return a.frequency < b.frequency;
        return a.id < b.id;
    }
    static bool matches(const Entry& entry, const Candidate& candidate) {
        const auto& owner = entry.owner;
        if (owner.family != candidate.family) return false;
        const bool owner_amplitude=owner.amplitude_high_hz>owner.amplitude_low_hz;
        const bool candidate_amplitude=candidate.amplitude_high_hz>candidate.amplitude_low_hz;
        const bool amplitude_pair=owner_amplitude&&candidate_amplitude;
        if(amplitude_pair) {
            if(owner.amplitude_low_hz>=candidate.amplitude_high_hz||
               candidate.amplitude_low_hz>=owner.amplitude_high_hz)return false;
        } else if(std::abs(owner.frequency-candidate.frequency)>
                  std::max(owner.frequency_tolerance,candidate.frequency_tolerance))return false;
        if(!owner_amplitude && !candidate_amplitude && owner.symbol_samples && owner.symbol_samples==candidate.symbol_samples &&
           !owner.burst_first && !candidate.burst_first) {
            // Overlap alone does not identify one ordinary symbol stream.
            // Competing epochs may score unrelated clocks over the same PCM;
            // their extra support must not replace an incompatible stream.
            const auto symbol=owner.symbol_samples;
            const auto distance=owner.first>candidate.first?owner.first-candidate.first:candidate.first-owner.first;
            const auto tolerance=std::min(std::max(owner.timing_tolerance,candidate.timing_tolerance),
                                          (symbol-1)/2);
            const auto remainder=distance%symbol;
            if(remainder>tolerance && symbol-remainder>tolerance)return false;
        }
        if (candidate.first < owner.end && owner.first < candidate.end) return true;
        if (entry.complete || entry.retired) return false;
        const auto& earlier = candidate.first >= owner.end ? owner : candidate;
        const auto& later = candidate.first >= owner.end ? candidate : owner;
        // A later long-duration hypothesis cannot enlarge an earlier stream's
        // absence window. Within that window, a separate short send is not an
        // alias merely because a long receiver has yet to score its silence.
        const auto gap = later.first - earlier.end;
        if (gap >= earlier.absence_samples) return false;
        if(earlier.burst_first || later.burst_first)
            return earlier.burst_first && earlier.burst_next && later.burst_first && later.burst_next &&
                earlier.symbol_samples==later.symbol_samples && *earlier.burst_next==*later.burst_first &&
                // A disjoint suffix has no copy of the leading accepted bits.
                // Reuse an earlier stronger row without transferring ownership;
                // a stronger suffix remains independent until actual overlap
                // supplies the existing full-prefix revision/merge evidence.
                stronger(earlier,later);
        const auto symbol = std::max(earlier.symbol_samples, later.symbol_samples);
        if (!symbol) return true;
        // Short aliases start on the longer profile's symbol clock. When the
        // earlier observation is a short prefix, align its start rather than
        // its truncated end. All differences are ordered to avoid overflowing
        // the global sample clock by adding a large duration to an endpoint.
        const auto distance = earlier.symbol_samples >= later.symbol_samples ?
            gap : later.first - earlier.first;
        const auto tolerance = std::min(std::max(earlier.timing_tolerance, later.timing_tolerance),
                                        (symbol - 1) / 2);
        const auto remainder = distance % symbol;
        if (remainder <= tolerance) return true;
        const auto ahead = symbol - remainder;
        // Do not round a near-boundary fragment onto a slot that already lies
        // beyond a full absence duration. For hours-long symbols only the
        // immediately contiguous next slot can match before that duration ends.
        return ahead <= tolerance && ahead < earlier.absence_samples - gap;
    }
    std::array<Entry, capacity> entries_{};
    std::size_t size_ = 0;
    std::uint64_t revision_ = 0;
};
}
