#pragma once

// Externally serialized, receiver-local metadata only; this is not thread-safe.
// Capture can run ahead of processing,
// but storage/slab boundaries never choose the dense receiver's processing blocks.
// Original caller endpoints and virtual <=512 processing fences are independent.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <type_traits>

namespace datapump::modem::detail::scheduled_processing {

class Error: public std::runtime_error { public: using std::runtime_error::runtime_error; };
enum class LedgerKind { pushes, processing, differences };
class CapacityError: public Error {
public:
    LedgerKind kind;
    CapacityError(const char* message,LedgerKind which):Error(message),kind(which){}
};
inline std::uint64_t coordinate_end(std::uint64_t first,std::uint64_t count) {
    if(count>std::numeric_limits<std::uint64_t>::max()-first)throw Error("processing journal coordinate overflow");
    return first+count;
}
inline std::size_t size_add(std::size_t a,std::size_t b) {
    if(b>std::numeric_limits<std::size_t>::max()-a)throw Error("processing journal workspace overflow");
    return a+b;
}
inline std::size_t size_product(std::size_t a,std::size_t b) {
    if(a && b>std::numeric_limits<std::size_t>::max()/a)throw Error("processing journal workspace overflow");
    return a*b;
}

struct Fence {
    std::uint64_t original_first=0,original_count=0,selected_first=0,selected_count=0;
    friend bool operator==(const Fence&,const Fence&)=default;
};

// Adjacent equal original lengths use one run. A run never expands into one
// record per call, block, symbol or hypothesis. All failures precede mutation.
template<std::uint64_t MaximumCount>class FenceLedger {
    struct Run {std::uint64_t first=0,count=0,repeats=0;};
    std::unique_ptr<Run[]> runs_;
    std::size_t capacity_=0,head_=0,size_=0;
    std::uint64_t end_=0;
    const Run& at(std::size_t i)const{return runs_[(head_+i)%capacity_];}
    Run& at(std::size_t i){return runs_[(head_+i)%capacity_];}
    static std::uint64_t span(const Run& r) {
        if(r.repeats>std::numeric_limits<std::uint64_t>::max()/r.count)
            throw Error("processing journal run overflow");
        return r.count*r.repeats;
    }
    // Original run ends are strictly increasing, even after partial retirement
    // or ring replacement. Seek without rescanning all earlier provider cuts.
    std::size_t first_intersecting(std::uint64_t position)const {
        std::size_t low=0,high=size_;
        while(low<high) {
            const auto middle=low+(high-low)/2;const auto& r=at(middle);
            if(coordinate_end(r.first,span(r))<=position)low=middle+1;else high=middle;
        }
        return low;
    }
public:
    static std::size_t allocation_bytes(std::size_t maximum_runs) {
        if(!maximum_runs)throw Error("invalid processing journal run limit");
        return size_add(size_product(maximum_runs,sizeof(Run)),128);
    }
    static std::size_t required_bytes(std::size_t maximum_runs) {
        return size_add(sizeof(FenceLedger),allocation_bytes(maximum_runs));
    }
    explicit FenceLedger(std::size_t maximum_runs,std::uint64_t origin=0,
                         std::size_t workspace_bytes=std::numeric_limits<std::size_t>::max()):
        capacity_(maximum_runs),end_(origin) {
        if(required_bytes(maximum_runs)>workspace_bytes)throw Error("processing journal metadata exceeds workspace");
        runs_=std::make_unique<Run[]>(maximum_runs);
    }
    std::uint64_t end()const{return end_;}
    std::uint64_t retained_first()const{return size_?at(0).first:end_;}
    std::size_t run_count()const{return size_;}
    std::size_t capacity()const{return capacity_;}
    std::size_t reserved_bytes()const{return required_bytes(capacity_);}
    // Explicit owner opt-in. The reservation callback runs before allocation
    // and must charge the FULL replacement while old backing remains live.
    // No endpoint, restart boundary, repetition or retained floor is changed.
    template<class Reserve>bool grow(Reserve&& reserve,std::stop_token stop={}) {
        if(stop.stop_requested())return false;
        const auto next_capacity=size_product(capacity_,2);
        const auto bytes=allocation_bytes(next_capacity);
        reserve(bytes);if(stop.stop_requested())return false;
        auto next=std::make_unique<Run[]>(next_capacity);
        for(std::size_t i=0;i<size_;++i) {
            if((i%128)==0 && stop.stop_requested())return false;
            next[i]=at(i);
        }
        if(stop.stop_requested())return false;
        runs_.swap(next);capacity_=next_capacity;head_=0;
        return true;
    }
    // Zero input is an identity operation and adds no observable processing fence.
    void check_append(std::uint64_t first,std::uint64_t count)const {
        coordinate_end(first,count);
        if(first!=end_ || count>MaximumCount)throw Error("invalid processing journal fence");
        if(!count)return;
        if(size_ && at(size_-1).count==count) {
            if(at(size_-1).repeats==std::numeric_limits<std::uint64_t>::max())
                throw Error("processing journal repetitions exhausted");
        } else if(size_==capacity_)throw CapacityError("processing journal metadata exhausted",
            MaximumCount==512?LedgerKind::processing:LedgerKind::pushes);
    }
    void append(std::uint64_t first,std::uint64_t count) {
        check_append(first,count);if(!count)return;
        if(size_ && at(size_-1).count==count)++at(size_-1).repeats;
        else {at(size_)={first,count,1};++size_;}
        end_=first+count;
    }
    // Keep the complete containing fence: recurrence starts at original_first,
    // advances through the unselected prefix, then consumes selected_first/count.
    Fence containing(std::uint64_t position)const {
        if(position>=end_)throw Error("processing journal position is not retained");
        for(std::size_t i=first_intersecting(position);i<size_;++i) {
            const auto& r=at(i);const auto finish=coordinate_end(r.first,span(r));
            if(position<r.first)break;
            if(position<finish) {
                const auto first=r.first+(position-r.first)/r.count*r.count;
                return {first,r.count,position,first+r.count-position};
            }
        }
        throw Error("processing journal history missing");
    }
    std::uint64_t replay_floor(std::uint64_t position)const {
        if(position==end_)return position;
        return containing(position).original_first;
    }
    void discard_before(std::uint64_t position) {
        if(position>end_)throw Error("processing journal future retirement");
        while(size_) {
            auto& r=at(0);if(position<=r.first)break;
            const auto completed=std::min(r.repeats,(position-r.first)/r.count);
            r.first+=completed*r.count;r.repeats-=completed;
            if(r.repeats)break;
            r=Run{};head_=(head_+1)%capacity_;--size_;
        }
    }
    template<class Callback>void visit(std::uint64_t first,std::uint64_t count,Callback&& callback)const {
        const auto finish=coordinate_end(first,count);
        if(finish>end_ || first<retained_first())throw Error("processing journal range is not retained");
        auto cursor=first;
        // Arithmetic skip inside each run; callback still observes every original
        // fence, including the complete seed fence for an interior start.
        for(std::size_t i=first_intersecting(cursor);i<size_ && cursor<finish;++i) {
            const auto& r=at(i);const auto run_end=r.first+span(r);
            if(run_end<=cursor)continue;
            if(r.first>cursor)throw Error("processing journal history missing");
            auto original=r.first+(cursor-r.first)/r.count*r.count;
            while(cursor<finish && original<run_end) {
                const auto next=std::min(finish,original+r.count);
                callback(Fence{original,r.count,cursor,next-cursor});cursor=next;original+=r.count;
            }
        }
        if(cursor!=finish)throw Error("processing journal history missing");
    }
};
using PushEnds=FenceLedger<std::numeric_limits<std::uint64_t>::max()>;
using ProcessingFences=FenceLedger<512>;

// Conservative union of affected original sample ranges. Intervals are emitted
// in processing order, can extend back to the containing storage seed fence, and
// merge without per-fence growth while virtual and storage grids remain shifted.
class DifferenceIntervals {
    struct Interval {std::uint64_t first=0,end=0;};
    std::unique_ptr<Interval[]> intervals_;
    std::size_t capacity_=0,head_=0,size_=0;
    std::uint64_t retired_before_=0;
    const Interval& at(std::size_t i)const{return intervals_[(head_+i)%capacity_];}
    Interval& at(std::size_t i){return intervals_[(head_+i)%capacity_];}
public:
    static std::size_t allocation_bytes(std::size_t maximum_intervals) {
        if(!maximum_intervals)throw Error("invalid processing difference limit");
        return size_add(size_product(maximum_intervals,sizeof(Interval)),128);
    }
    static std::size_t required_bytes(std::size_t maximum_intervals) {
        return size_add(sizeof(DifferenceIntervals),allocation_bytes(maximum_intervals));
    }
    explicit DifferenceIntervals(std::size_t maximum_intervals):capacity_(maximum_intervals) {
        allocation_bytes(maximum_intervals);intervals_=std::make_unique<Interval[]>(maximum_intervals);
    }
    std::size_t interval_count()const{return size_;}
    std::size_t capacity()const{return capacity_;}
    std::size_t reserved_bytes()const{return required_bytes(capacity_);}
    template<class Reserve>bool grow(Reserve&& reserve,std::stop_token stop={}) {
        if(stop.stop_requested())return false;
        const auto next_capacity=size_product(capacity_,2);
        reserve(allocation_bytes(next_capacity));if(stop.stop_requested())return false;
        auto next=std::make_unique<Interval[]>(next_capacity);
        for(std::size_t i=0;i<size_;++i) {
            if((i%128)==0 && stop.stop_requested())return false;
            next[i]=at(i);
        }
        if(stop.stop_requested())return false;
        intervals_.swap(next);capacity_=next_capacity;head_=0;
        return true;
    }
    void check_append(std::uint64_t first,std::uint64_t end)const {
        if(first>=end)throw Error("invalid processing difference interval");
        first=std::max(first,retired_before_);if(first>=end)return;
        if(size_ && first<at(size_-1).first && end<at(size_-1).first)
            throw Error("out of order processing difference interval");
        if((!size_ || first>at(size_-1).end) && size_==capacity_)
            throw CapacityError("processing difference metadata exhausted",LedgerKind::differences);
    }
    void append(std::uint64_t first,std::uint64_t end) {
        check_append(first,end);
        first=std::max(first,retired_before_);if(first>=end)return;
        if(size_ && first<=at(size_-1).end) {
            at(size_-1).first=std::min(first,at(size_-1).first);
            at(size_-1).end=std::max(at(size_-1).end,end);
            while(size_>1 && at(size_-2).end>=at(size_-1).first) {
                at(size_-2).first=std::min(at(size_-2).first,at(size_-1).first);
                at(size_-2).end=std::max(at(size_-2).end,at(size_-1).end);
                at(size_-1)={};--size_;
            }
        }
        else {at(size_)={first,end};++size_;}
    }
    bool overlaps(std::uint64_t first,std::uint64_t count)const {
        const auto end=coordinate_end(first,count);if(!count)return false;
        for(std::size_t i=0;i<size_;++i) {
            const auto& range=at(i);if(range.first>=end)break;
            if(first<range.end)return true;
        }
        return false;
    }
    void discard_before(std::uint64_t position) {
        retired_before_=std::max(retired_before_,position);
        position=retired_before_;
        while(size_ && at(0).end<=position) {at(0)={};head_=(head_+1)%capacity_;--size_;}
        if(size_ && at(0).first<position)at(0).first=position;
    }
};

struct Limits {std::size_t push_runs=0,processing_runs=0,difference_intervals=0,workspace_bytes=0;};
struct Accounting {
    std::size_t reserved_bytes=0,push_runs=0,processing_runs=0,difference_intervals=0;
    std::uint64_t registered_end=0,processed_end=0,recorded_end=0,retained_first=0;
};
struct Block {
    std::uint64_t token=0,first=0,count=0,push_first=0,push_end=0;
    bool storage_geometry_differs=false;
    bool starts_push()const{return first==push_first;}
    friend bool operator==(const Block&,const Block&)=default;
};

class ProcessingJournal {
    PushEnds pushes_;
    ProcessingFences processing_;
    DifferenceIntervals differences_;
    std::uint64_t position_=0,next_token_=1;
    std::optional<Block> prepared_;
    std::size_t reserved_bytes_=0;
    static Limits checked_limits(Limits l) {
        if(required_bytes(l)>l.workspace_bytes)throw Error("processing journal exceeds workspace");
        return l;
    }
    ProcessingJournal(Limits l,std::uint64_t origin,int):pushes_(l.push_runs,origin),
        processing_(l.processing_runs,origin),differences_(l.difference_intervals),position_(origin),
        reserved_bytes_(required_bytes(l)) {}
public:
    static std::size_t required_bytes(Limits l) {
        auto bytes=size_add(sizeof(ProcessingJournal),PushEnds::allocation_bytes(l.push_runs));
        bytes=size_add(bytes,ProcessingFences::allocation_bytes(l.processing_runs));
        return size_add(bytes,DifferenceIntervals::allocation_bytes(l.difference_intervals));
    }
    explicit ProcessingJournal(Limits l,std::uint64_t origin=0):ProcessingJournal(checked_limits(l),origin,0) {}
    void append_push(std::uint64_t first,std::uint64_t count){pushes_.append(first,count);}
    const PushEnds& pushes()const{return pushes_;}
    const ProcessingFences& processing_fences()const{return processing_;}
    const DifferenceIntervals& difference_intervals()const{return differences_;}
    std::uint64_t position()const{return position_;}
    bool pending()const{return position_<pushes_.end();}
    Accounting accounting()const {
        return {reserved_bytes_,pushes_.run_count(),processing_.run_count(),differences_.interval_count(),
                pushes_.end(),position_,processing_.end(),processing_.retained_first()};
    }
    template<class Reserve>bool grow(LedgerKind kind,Reserve&& reserve,std::stop_token stop={}) {
        const auto previous=reserved_bytes_;
        struct Restore {std::size_t& charge;std::size_t old;bool committed=false;
            ~Restore(){if(!committed)charge=old;}} restore{reserved_bytes_,previous};
        const auto precharge=[&](std::size_t bytes) {
            reserve(bytes);reserved_bytes_=size_add(previous,bytes);
        };
        bool changed=false;
        switch(kind) {
        case LedgerKind::pushes:changed=pushes_.grow(precharge,stop);break;
        case LedgerKind::processing:changed=processing_.grow(precharge,stop);break;
        case LedgerKind::differences:changed=differences_.grow(precharge,stop);break;
        default:throw Error("invalid processing metadata ledger");
        }
        if(!changed)return false;
        reserved_bytes_=required_bytes({pushes_.capacity(),processing_.capacity(),differences_.capacity(),0});
        restore.committed=true;return true;
    }
    // Count reads current committed receiver state. storage_at(p) returns the
    // original <=512 storage fence containing p, never an owner slab. No scoring
    // or mutable receiver callbacks run here. Cancellation/exhaustion leaves all
    // metadata unchanged. available_end is the actual retained PCM capture end,
    // independent of a registered original push endpoint. Incomplete capture waits
    // for the complete selected block; it never clips a new processing boundary.
    // After recording, a retry returns this same frozen block.
    template<class ProcessCount,class StorageAt>std::optional<Block> prepare(
            ProcessCount&& count,StorageAt&& storage_at,std::stop_token stop={},
            std::uint64_t available_end=std::numeric_limits<std::uint64_t>::max()) {
        if(stop.stop_requested())return {};
        if(available_end<position_)throw Error("processing capture frontier precedes committed position");
        if(prepared_) {
            if(prepared_->first+prepared_->count>available_end)throw Error("prepared processing PCM is unavailable");
            return prepared_;
        }
        if(!pending())return {};
        const auto push=pushes_.containing(position_);
        const auto push_end=coordinate_end(push.original_first,push.original_count);
        const auto maximum=static_cast<std::size_t>(std::min<std::uint64_t>(512,push_end-position_));
        const auto selected=count(maximum);
        static_assert(std::is_integral_v<decltype(selected)>,"process_count must return an integer");
        if(stop.stop_requested())return {};
        if constexpr(std::is_signed_v<decltype(selected)>) {
            if(selected<=0)throw Error("invalid runtime processing count");
        } else if(!selected)throw Error("invalid runtime processing count");
        if(static_cast<std::uint64_t>(selected)>maximum)throw Error("invalid runtime processing count");
        const auto finish=coordinate_end(position_,static_cast<std::uint64_t>(selected));
        if(finish>available_end)return {};
        auto cursor=position_,difference_first=position_,difference_end=finish;
        bool different=false;
        while(cursor<finish) {
            const auto fence=storage_at(cursor);
            if(stop.stop_requested())return {};
            if(!fence.original_count || fence.original_count>512 || fence.original_first>cursor)
                throw Error("invalid original storage fence");
            if(cursor!=position_ && fence.original_first!=cursor)
                throw Error("overlapping original storage fences");
            const auto end=coordinate_end(fence.original_first,fence.original_count);
            if(end<=cursor || end>pushes_.end())throw Error("original storage fence does not cover processing range");
            if(fence.original_first<push.original_first || end>push_end)
                throw Error("original storage fence crosses original push endpoint");
            different|=fence.original_first!=position_ || fence.original_count!=selected;
            difference_first=std::min(difference_first,fence.original_first);difference_end=std::max(difference_end,end);
            cursor=std::min(finish,end);
        }
        processing_.check_append(position_,selected);
        if(different)differences_.check_append(difference_first,difference_end);
        if(next_token_==std::numeric_limits<std::uint64_t>::max())throw Error("processing journal block tokens exhausted");
        if(stop.stop_requested())return {};
        // Every capacity/coordinate check is complete. These appends cannot fail
        // in this serialized journal; fence publication precedes exact scoring.
        processing_.append(position_,selected);
        if(different)differences_.append(difference_first,difference_end);
        prepared_=Block{next_token_++,position_,static_cast<std::uint64_t>(selected),
                        push.original_first,push_end,different};
        return prepared_;
    }
    // Caller preserves original block -> lane -> symbol order, scores the block,
    // commits original detector results, and settles timing BEFORE this advance.
    // Do not advance a partial/cancelled scorer. Original push identity permits
    // existing once-per-push preparation even when replay spans separate calls.
    void advance(const Block& block) {
        if(!prepared_ || block!=*prepared_)throw Error("stale or unprepared processing block");
        position_=coordinate_end(block.first,block.count);prepared_.reset();
    }
    // Retain complete original virtual seed fences; never discard unprocessed
    // or prepared work. The returned floor also governs retained PCM/oscillators.
    std::uint64_t discard_before(std::uint64_t requested) {
        if(requested>position_)throw Error("cannot retire unprocessed journal history");
        const auto floor=processing_.replay_floor(requested);
        processing_.discard_before(floor);differences_.discard_before(floor);
        // Metadata retains the containing whole original push so historical
        // consumers can recover once-per-push preparation context. PCM retention
        // still uses the virtual <=512 seed floor returned here.
        pushes_.discard_before(floor);return floor;
    }
};

} // namespace datapump::modem::detail::scheduled_processing
