#pragma once
#include "datapump/live.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <memory>
#include <stop_token>
#include <utility>

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
    static constexpr std::size_t range_capacity=64,maximum_range_capacity=65536;
    struct Range {
        std::uint64_t first=0,last=0,first_id=0;
        Clock::time_point observed_at{};
    };
    DeferredPatternHistoryOrdinals()=default;
    DeferredPatternHistoryOrdinals(const DeferredPatternHistoryOrdinals&)=delete;
    DeferredPatternHistoryOrdinals& operator=(const DeferredPatternHistoryOrdinals&)=delete;
    DeferredPatternHistoryOrdinals(DeferredPatternHistoryOrdinals&& other)noexcept { *this=std::move(other); }
    DeferredPatternHistoryOrdinals& operator=(DeferredPatternHistoryOrdinals&& other)noexcept {
        if(this!=&other) {
            inline_ranges_=std::move(other.inline_ranges_);extra_ranges_=std::move(other.extra_ranges_);
            capacity_=std::exchange(other.capacity_,range_capacity);size_=std::exchange(other.size_,0);
            generation_=std::exchange(other.generation_,0);
        }
        return *this;
    }
    bool can_record()const noexcept{return size_<capacity_;}
    bool requires_materialization()const noexcept{return size_+1>=capacity_;}
    std::size_t capacity()const noexcept{return capacity_;}
    static std::size_t growth_bytes(std::size_t count) {
        if(count<=range_capacity || count>maximum_range_capacity ||
           count>(std::numeric_limits<std::size_t>::max()-128)/sizeof(Range))
            throw Error("pattern history provenance growth exceeds bound");
        return count*sizeof(Range)+128;
    }
    std::size_t heap_bytes()const noexcept{return extra_ranges_?capacity_*sizeof(Range)+128:0;}
    // Both old and replacement heaps are live until publication. The caller
    // reserves the COMPLETE replacement and charges resulting heap_bytes().
    // No IDs, generations, observation dates or detector state change here.
    template<class Reserve>
    bool grow(std::size_t count,Reserve&& reserve,std::stop_token stop={}) {
        if(stop.stop_requested())return false;
        if(count<=capacity_)throw Error("pattern history provenance growth must increase capacity");
        reserve(growth_bytes(count));
        if(stop.stop_requested())return false;
        auto replacement=std::make_unique<Range[]>(count);
        for(std::size_t i=0;i<size_;++i) {
            if(stop.stop_requested())return false;
            replacement[i]=storage()[i];
        }
        if(stop.stop_requested())return false;
        extra_ranges_.swap(replacement);capacity_=count;
        return true;
    }
    std::size_t range_count()const noexcept{return size_;}
    std::uint64_t generation()const noexcept{return generation_;}
    std::size_t working_bytes()const noexcept{return sizeof(*this)+heap_bytes();}
    std::optional<std::uint64_t> oldest_recorded()const noexcept {
        return size_?std::optional<std::uint64_t>{storage()[0].first}:std::nullopt;
    }

    // Call after EVERY feed/process_pending completion, before returning a
    // snapshot or running expensive candidates(). Use the presentation clock
    // here; use a separate real work clock for the polling cadence below.
    // Caller checks can_record()/requires_materialization before admitting a
    // further processing fence and grows metadata or materializes at pressure.
    // Inline storage is charged by sizeof(Receiver); grown backing is separate.
    template<class ReserveIds>
    void record(std::uint64_t generation,Clock::time_point observed_at,ReserveIds reserve_ids) {
        if(generation<generation_)throw Error("pattern history generation rewound");
        if(generation==generation_)return; // No new evidence: never re-age old evidence.
        if(!can_record())throw Error("pattern history provenance requires materialization");
        const auto count=generation-generation_;
        const auto first_id=reserve_ids(count);
        if(!first_id || count-1>std::numeric_limits<std::uint64_t>::max()-first_id)
            throw Error("invalid pattern observation reservation");
        storage()[size_++]={generation_+1,generation,first_id,observed_at};
        generation_=generation;
    }

    // Only call for a newly returned FIFO candidate. Previously materialized
    // observations retain their ID/time/threshold in PatternScoreHistory, even
    // after these transient provenance ranges have been released.
    PatternScoreObservation observation(std::uint64_t ordinal,double threshold)const {
        if(!ordinal || ordinal>generation_)throw Error("unrecorded pattern history ordinal");
        for(std::size_t i=0;i<size_;++i) {
            const auto& r=storage()[i];
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
        while(removed<size_ && storage()[removed].last<=through)++removed;
        for(std::size_t i=removed;i<size_;++i)storage()[i-removed]=storage()[i];
        size_-=removed;
        if(size_ && storage()[0].first<=through) {
            const auto skipped=through-storage()[0].first+1;
            storage()[0].first+=skipped;storage()[0].first_id+=skipped;
        }
    }
private:
    std::array<Range,range_capacity> inline_ranges_{};
    std::unique_ptr<Range[]> extra_ranges_;
    Range* storage()noexcept{return extra_ranges_?extra_ranges_.get():inline_ranges_.data();}
    const Range* storage()const noexcept{return extra_ranges_?extra_ranges_.get():inline_ranges_.data();}
    std::size_t capacity_=range_capacity,size_=0;
    std::uint64_t generation_=0;
};

// Routine numerical plots may wait behind captured work. This is an execution
// priority decision only: required provenance/PCM-retirement pressure bypasses
// it. The frontier includes the current callback's physical extent, including
// coordinate-only certified nulls and the tail of a popped ingress block.
inline bool optional_pattern_history_ready(std::uint64_t prospective_ingress,
    std::uint64_t all_consumer_frontier,std::uint64_t captured_ingress,bool queue_empty) noexcept {
    return all_consumer_frontier==prospective_ingress && captured_ingress==all_consumer_frontier && queue_empty;
}

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
