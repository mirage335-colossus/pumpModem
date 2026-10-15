#pragma once

// Externally serialized, bounded metadata for exact deferred diagnostics.
// No hypothesis, fitted product, detector score or PCM is retained here.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <stop_token>

namespace datapump::modem::detail::scheduled_diagnostics {

class Error: public std::runtime_error {public:using std::runtime_error::runtime_error;};
enum class RejectionProvenance : std::uint8_t { original_certified, default_point };
struct Event {
    std::uint64_t ordinal=0,global_lane=0,source_index=0,original_trials=0,processing_block_end=0;
    RejectionProvenance provenance=RejectionProvenance::original_certified;
    friend bool operator==(const Event&,const Event&)=default;
};
class DeferredEvents;
struct Cursor {
    const DeferredEvents* owner=nullptr;
    std::uint64_t through=0,at_or_before=0,retirement_revision=0;
    std::size_t run_index=0;
    std::uint64_t run_offset=0;
    bool done=false;
};
struct Accounting {
    std::size_t reserved_bytes=0,runs=0;
    std::uint64_t unresolved_events=0,last_ordinal=0,resolved_through=0;
};

class DeferredEvents {
    struct Run {Event first;std::uint64_t count=0,lane_stride=0;};
    std::unique_ptr<Run[]> runs_;
    std::size_t capacity_=0,head_=0,size_=0,reserved_bytes_=0;
    std::uint64_t last_ordinal_=0,resolved_through_=0,unresolved_=0,retirement_revision_=0;
    const Run& at(std::size_t i)const{return runs_[(head_+i)%capacity_];}
    Run& at(std::size_t i){return runs_[(head_+i)%capacity_];}
    static std::uint64_t add(std::uint64_t value,std::uint64_t offset) {
        if(offset>std::numeric_limits<std::uint64_t>::max()-value)throw Error("diagnostic event coordinate overflow");
        return value+offset;
    }
    static std::uint64_t product(std::uint64_t a,std::uint64_t b) {
        if(a && b>std::numeric_limits<std::uint64_t>::max()/a)throw Error("diagnostic lane stride overflow");
        return a*b;
    }
    static Event event_at(const Run& run,std::uint64_t offset) {
        if(offset>=run.count)throw Error("invalid diagnostic run offset");
        return {add(run.first.ordinal,offset),add(run.first.global_lane,product(run.lane_stride,offset)),run.first.source_index,
                add(run.first.original_trials,offset),run.first.processing_block_end,run.first.provenance};
    }
    static bool increments(std::uint64_t previous,std::uint64_t next) {
        return previous<std::numeric_limits<std::uint64_t>::max() && previous+1==next;
    }
    static bool continues(const Run& run,const Event& next) {
        const auto previous=event_at(run,run.count-1);
        if(!increments(previous.ordinal,next.ordinal) || !increments(previous.original_trials,next.original_trials) ||
           previous.source_index!=next.source_index || previous.processing_block_end!=next.processing_block_end ||
           previous.provenance!=next.provenance ||
           next.global_lane<=previous.global_lane)return false;
        const auto stride=next.global_lane-previous.global_lane;
        return run.count==1 || stride==run.lane_stride;
    }
    void validate(const Cursor& cursor)const {
        if(cursor.owner!=this)throw Error("diagnostic query belongs to another journal");
        if(cursor.retirement_revision!=retirement_revision_)throw Error("diagnostic query snapshot was retired");
        if(cursor.through>last_ordinal_ || cursor.at_or_before>cursor.through)
            throw Error("invalid diagnostic query cursor");
    }
public:
    static std::size_t required_bytes(std::size_t maximum_runs) {
        if(!maximum_runs)throw Error("invalid diagnostic run capacity");
        const auto max=std::numeric_limits<std::size_t>::max();
        if(maximum_runs>max/sizeof(Run))throw Error("diagnostic journal workspace overflow");
        const auto payload=maximum_runs*sizeof(Run);
        if(sizeof(DeferredEvents)>max-128 || payload>max-sizeof(DeferredEvents)-128)
            throw Error("diagnostic journal workspace overflow");
        return sizeof(DeferredEvents)+payload+128;
    }
    explicit DeferredEvents(std::size_t maximum_runs,std::uint64_t completed_origin=0,
            std::size_t workspace_bytes=std::numeric_limits<std::size_t>::max()):capacity_(maximum_runs),
        reserved_bytes_(required_bytes(maximum_runs)),last_ordinal_(completed_origin),resolved_through_(completed_origin) {
        if(reserved_bytes_>workspace_bytes)throw Error("diagnostic journal exceeds workspace");
        runs_=std::make_unique<Run[]>(maximum_runs);
    }
    DeferredEvents(const DeferredEvents&)=delete;
    DeferredEvents& operator=(const DeferredEvents&)=delete;
    DeferredEvents(DeferredEvents&&)=delete;
    DeferredEvents& operator=(DeferredEvents&&)=delete;
    std::size_t working_bytes()const{return reserved_bytes_;}
    std::uint64_t last_ordinal()const{return last_ordinal_;}
    std::uint64_t resolved_through()const{return resolved_through_;}
    std::size_t run_count()const{return size_;}
    std::size_t run_capacity()const{return capacity_;}
    // Grow only metadata. Both arrays coexist until publication, so the caller
    // must reserve the COMPLETE replacement, not merely the capacity increment.
    // Logical run indices (including frozen cursors) survive ring linearization.
    template<class Reserve> bool grow(std::size_t maximum_runs,Reserve&& reserve,std::stop_token stop={}) {
        if(stop.stop_requested())return false;
        if(maximum_runs<=capacity_)throw Error("diagnostic journal growth must increase capacity");
        const auto replacement_bytes=required_bytes(maximum_runs);
        reserve(replacement_bytes);
        if(stop.stop_requested())return false;
        auto replacement=std::make_unique<Run[]>(maximum_runs);
        for(std::size_t i=0;i<size_;++i) {
            if(stop.stop_requested())return false;
            replacement[i]=at(i);
        }
        if(stop.stop_requested())return false;
        runs_.swap(replacement);capacity_=maximum_runs;head_=0;reserved_bytes_=replacement_bytes;
        return true;
    }
    Accounting accounting()const{return {reserved_bytes_,size_,unresolved_,last_ordinal_,resolved_through_};}
    std::optional<Event> oldest()const {
        if(!size_)return {};return at(0).first;
    }
    // A run learns a positive lane stride from its second event, then requires
    // that same stride, ordinal/trials+1 and unchanged source index/block end.
    // An ordinal gap is a producer assertion that every intervening event was
    // already resolved eagerly or permanently excluded by the original guards.
    // Its chronology still prevents compression across eager/nonretained events.
    void check_append_cold(const Event& event)const {
        if(event.provenance!=RejectionProvenance::original_certified && event.provenance!=RejectionProvenance::default_point)
            throw Error("diagnostic rejection provenance invalid");
        if(event.ordinal<=last_ordinal_)throw Error("diagnostic events are not chronological");
        if(unresolved_==std::numeric_limits<std::uint64_t>::max())throw Error("diagnostic event count overflow");
        const bool merge=size_ && continues(at(size_-1),event);
        if(merge && at(size_-1).count==std::numeric_limits<std::uint64_t>::max())
            throw Error("diagnostic run repetitions exhausted");
        if(!merge && size_==capacity_)throw Error("diagnostic journal metadata exhausted; resolve before PCM retirement");
    }
    bool can_append(const Event& event)const noexcept {
        try{check_append_cold(event);return true;}catch(...){return false;}
    }
    void append_cold(const Event& event) {
        check_append_cold(event);
        const bool merge=size_ && continues(at(size_-1),event);
        if(!size_)resolved_through_=event.ordinal-1;
        if(merge) {
            auto& run=at(size_-1);
            if(run.count==1)run.lane_stride=event.global_lane-run.first.global_lane;
            ++run.count;
        } else {at(size_)={event,1,0};++size_;}
        ++unresolved_;last_ordinal_=event.ordinal;
    }
    // No cold payload is added for an eager or permanently excluded event.
    // Reobserving the current highwater is harmless; older ordinals are rejected.
    // This does not mark an existing cold event resolved or remove its payload.
    void observe_resolved(std::uint64_t ordinal) {
        if(ordinal<last_ordinal_)throw Error("resolved diagnostic event precedes highwater");
        last_ordinal_=ordinal;if(!size_)resolved_through_=ordinal;
    }
    // Cursor borrows this nonmovable journal; do not use it after destruction.
    Cursor newest_first()const {
        Cursor cursor;cursor.owner=this;cursor.through=last_ordinal_;cursor.at_or_before=last_ordinal_;
        cursor.retirement_revision=retirement_revision_;cursor.done=!size_;
        if(size_){cursor.run_index=size_-1;cursor.run_offset=at(size_-1).count-1;}
        return cursor;
    }
    // Snapshot includes only events<=through. Appending/observing newer events
    // preserves it, including when append extends the same compressed run.
    std::optional<Event> peek(const Cursor& cursor,std::stop_token stop={})const {
        if(stop.stop_requested())return {};validate(cursor);if(cursor.done)return {};
        if(cursor.run_index>=size_)throw Error("invalid diagnostic query run");
        const auto event=event_at(at(cursor.run_index),cursor.run_offset);
        if(event.ordinal>cursor.through || event.ordinal>cursor.at_or_before)
            throw Error("diagnostic event exceeds query ceiling");
        return event;
    }
    // Evaluate outside the ledger. Commit external exact materialization (dedup
    // by ordinal), then advance; cancellation can leave this same event pending.
    void advance(Cursor& cursor,const Event& event)const {
        const auto pending=peek(cursor);
        if(!pending || *pending!=event)throw Error("stale diagnostic query event");
        cursor.at_or_before=event.ordinal-1;
        if(cursor.run_offset)--cursor.run_offset;
        else if(cursor.run_index) {
            --cursor.run_index;cursor.run_offset=at(cursor.run_index).count-1;
        } else cursor.done=true;
    }
    // Metadata-only traversal convenience. It advances BEFORE returning; exact
    // cancellable evaluation must use peek/evaluate/materialize/advance instead.
    std::optional<Event> next(Cursor& cursor,std::stop_token stop={})const {
        const auto event=peek(cursor,stop);if(!event || stop.stop_requested())return {};
        advance(cursor,*event);return event;
    }
    // Explicit external proof boundary: ALL unresolved cold events<=cutoff must
    // already be permanently excluded or exactly materialized. Last-K exclusion
    // is valid only after K confirmed newer retained events (merged with eager
    // FIFO) exist. The ledger does not infer proof from age, score or PCM pressure.
    // Use the query's frozen through, not a newer concurrently observed highwater.
    void discard_resolved_through(std::uint64_t cutoff) {
        if(cutoff>last_ordinal_)throw Error("diagnostic resolution exceeds completed highwater");
        if(cutoff<=resolved_through_)return;
        if(retirement_revision_==std::numeric_limits<std::uint64_t>::max())
            throw Error("diagnostic retirement revision exhausted");
        while(size_ && at(0).first.ordinal<=cutoff) {
            auto& run=at(0);
            const auto completed=std::min(run.count,cutoff-run.first.ordinal+1);
            unresolved_-=completed;
            if(completed==run.count) {run={};head_=(head_+1)%capacity_;--size_;}
            else {run.first=event_at(run,completed);run.count-=completed;break;}
        }
        // Intervening ordinal gaps were explicitly asserted resolved/excluded
        // by producers, so retain the next unresolved event and advance to it.
        resolved_through_=size_?at(0).first.ordinal-1:last_ordinal_;
        ++retirement_revision_;
    }
};

} // namespace datapump::modem::detail::scheduled_diagnostics
