#pragma once
#include "datapump/live.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>

namespace datapump::live::detail {
// Reserve IDs at the cheap completed-window fence, not when diagnostic scores
// are materialized. Hidden and unresolved completed observations therefore stay
// below a GUI clear-through watermark. ID gaps are harmless; ID reuse is not.
inline std::uint64_t reserve_pattern_observation_ids(std::atomic<std::uint64_t>& highwater,
                                                     std::uint64_t count) {
    if (!count) throw Error("pattern observation reservation must be nonempty");
    auto previous=highwater.load(std::memory_order_relaxed);
    for (;;) {
        if (count>std::numeric_limits<std::uint64_t>::max()-previous)
            throw Error("pattern observation identity exhausted");
        if (highwater.compare_exchange_weak(previous,previous+count,
                                            std::memory_order_relaxed,std::memory_order_relaxed))
            return previous+1;
    }
}

// This is presentation provenance only. It neither observes PCM nor changes
// detector scores, trial charges, thresholds, timing hypotheses or completion.
// Each instance belongs to one receiver lifetime. Generations must never rewind.
// A generation denotes the original committed completed-window/journal ordinal,
// including an event which later proves too weak to enter the diagnostic FIFO.
class DeferredPatternHistoryOrdinals {
public:
    using Clock=std::chrono::steady_clock;
    static constexpr std::size_t range_capacity=64;
    struct Range {
        std::uint64_t first=0,last=0,first_id=0;
        Clock::time_point observed_at{};
    };
    bool can_record()const noexcept{return size_<range_capacity;}
    bool requires_materialization()const noexcept{return size_+1>=range_capacity;}
    std::size_t range_count()const noexcept{return size_;}
    std::uint64_t generation()const noexcept{return generation_;}
    std::size_t working_bytes()const noexcept{return sizeof(*this);}
    std::optional<std::uint64_t> oldest_recorded()const noexcept {
        return size_?std::optional<std::uint64_t>{ranges_[0].first}:std::nullopt;
    }

    // Call after EVERY feed/process_pending completion, before returning a
    // snapshot or running expensive candidates(). Use the presentation clock
    // here; use a separate real work clock for the polling cadence below.
    // Caller checks can_record()/requires_materialization before admitting a
    // further processing fence and materializes at pressure. This bounded array
    // has no heap allocation and is charged by sizeof(Receiver).
    template<class ReserveIds>
    void record(std::uint64_t generation,Clock::time_point observed_at,ReserveIds reserve_ids) {
        if(generation<generation_)throw Error("pattern history generation rewound");
        if(generation==generation_)return; // No new evidence: never re-age old evidence.
        if(!can_record())throw Error("pattern history provenance requires materialization");
        const auto count=generation-generation_;
        const auto first_id=reserve_ids(count);
        if(!first_id || count-1>std::numeric_limits<std::uint64_t>::max()-first_id)
            throw Error("invalid pattern observation reservation");
        ranges_[size_++]={generation_+1,generation,first_id,observed_at};
        generation_=generation;
    }

    // Only call for a newly returned FIFO candidate. Previously materialized
    // observations retain their ID/time/threshold in PatternScoreHistory, even
    // after these transient provenance ranges have been released.
    PatternScoreObservation observation(std::uint64_t ordinal,double threshold)const {
        if(!ordinal || ordinal>generation_)throw Error("unrecorded pattern history ordinal");
        for(std::size_t i=0;i<size_;++i) {
            const auto& r=ranges_[i];
            if(ordinal<r.first)break;
            if(ordinal<=r.last)return {r.first_id+(ordinal-r.first),r.observed_at,threshold};
        }
        throw Error("pattern history provenance retired before materialization");
    }

    // The producer must certify that every earlier unresolved journal event has
    // been materialized or is permanently excluded from the exact last-N FIFO.
    // Old retained candidates are already pinned by PatternScoreHistory and do
    // not pin all intervening ranges forever. Never prune merely by wall age.
    void discard_resolved_through(std::uint64_t through) {
        if(through>generation_)throw Error("pattern history resolved fence exceeds generation");
        std::size_t removed=0;
        while(removed<size_ && ranges_[removed].last<=through)++removed;
        for(std::size_t i=removed;i<size_;++i)ranges_[i-removed]=ranges_[i];
        size_-=removed;
        if(size_ && ranges_[0].first<=through) {
            const auto skipped=through-ranges_[0].first+1;
            ranges_[0].first+=skipped;ranges_[0].first_id+=skipped;
        }
    }
private:
    std::array<Range,range_capacity> ranges_{};
    std::size_t size_=0;
    std::uint64_t generation_=0;
};

// Cadence throttles diagnostic reconstruction only. Publication of accepted bits
// and physical END remains independent and precedes this optional work.
class DeferredPatternHistoryPoll {
public:
    using Clock=std::chrono::steady_clock;
    static constexpr auto period=std::chrono::milliseconds(250);
    bool due(std::uint64_t generation,Clock::time_point work_now,bool pressure=false)const noexcept {
        // Explicit pressure may retry an unchanged generation if a producer
        // still has required unresolved metadata. Routine unchanged polls skip.
        if(pressure)return generation!=0;
        if(generation<=queried_)return false;
        return !completed_at_ ||
            (work_now>=*completed_at_ && work_now-*completed_at_>=period);
    }
    void completed(std::uint64_t generation,Clock::time_point work_now) {
        if(generation<queried_)throw Error("pattern history query generation rewound");
        queried_=generation;completed_at_=work_now; // After query: do not spin after a slow query.
    }
    std::uint64_t queried_generation()const noexcept{return queried_;}
private:
    std::optional<Clock::time_point> completed_at_;
    std::uint64_t queried_=0;
};
}
