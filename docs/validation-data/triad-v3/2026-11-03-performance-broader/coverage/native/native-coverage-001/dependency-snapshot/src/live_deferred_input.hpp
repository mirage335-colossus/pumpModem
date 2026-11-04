#pragma once
#include "datapump/types.hpp"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace datapump::live::detail {
// Coordinates are in one capture revision. A synchronous consumer contributes
// ingress; a delayed consumer contributes its GLOBAL admission origin + LOCAL
// committed event frontier. New zero-local-frontier consumers cannot pin old
// acquisition history at absolute zero.
struct DeferredConsumerFrontier {
    std::uint64_t origin=0;
    std::optional<std::uint64_t> local;
};
inline std::uint64_t committed_frontier(std::uint64_t ingress,
    std::span<const DeferredConsumerFrontier> consumers) {
    auto committed=ingress;
    for(const auto& c:consumers) {
        if(c.origin>ingress)throw Error("receiver admission origin exceeds capture ingress");
        if(!c.local)continue;
        if(*c.local>ingress-c.origin)throw Error("delayed receiver frontier exceeds its captured input");
        committed=std::min(committed,c.origin+*c.local);
    }
    return committed;
}

// One bounded metadata ring per Live cohort, never a PCM copy or one ring per
// key/epoch. Original queued_at survives block splitting, delayed batching and
// idle polling. Scored symbol/lookahead retention is NOT computational backlog.
class DeferredInputFences {
public:
    using Clock=std::chrono::steady_clock;
    struct Fence {
        std::uint64_t first=0,end=0;
        Clock::time_point queued_at{};
        bool pcm=false;
    };
    struct Pending {
        std::uint64_t pcm_samples=0;
        std::optional<Clock::time_point> oldest;
    };
    void reserve(std::size_t bytes) {
        if(!rows_.empty())return;
        const auto capacity=bytes/sizeof(Fence);
        if(!capacity)throw Error("DSP workspace cannot hold deferred input timing metadata");
        rows_.resize(capacity);
        // Charge actual allocator capacity, not the requested entry count.
        if(working_bytes()>bytes) {
            std::vector<Fence>{}.swap(rows_);
            throw Error("deferred input timing allocation exceeds workspace");
        }
    }
    std::size_t working_bytes()const noexcept{return rows_.capacity()*sizeof(Fence);}
    std::size_t size()const noexcept{return count_;}
    bool empty()const noexcept{return count_==0;}
    void record(std::uint64_t first,std::uint64_t count,Clock::time_point at,bool pcm) {
        if(!count)return;
        if(count>std::numeric_limits<std::uint64_t>::max()-first)
            throw Error("deferred input coordinate exhausted");
        if(last_queued_at_ && at<*last_queued_at_)
            throw Error("deferred input enqueue timestamps are not monotone");
        if(last_end_ && first<*last_end_)
            throw Error("deferred input capture intervals overlap");
        // Merge adjacent equal-kind callbacks within a bounded ten-ms age
        // bucket. Count/coordinates remain exact; oldest age is conservatively
        // overestimated by at most ten ms after a partial trim, never refreshed.
        // This prevents tiny callbacks from creating a metadata-only RAM cliff.
        if(count_ && first==row(count_-1).end && pcm==row(count_-1).pcm &&
           at-row(count_-1).queued_at<=std::chrono::milliseconds(10)) {
            rows_[(begin_+count_-1)%rows_.size()].end=first+count;
            last_queued_at_=at;last_end_=first+count;return;
        }
        if(count_==rows_.size())throw Error("deferred input metadata workspace exhausted");
        rows_[(begin_+count_)%rows_.size()]={first,first+count,at,pcm};++count_;
        last_queued_at_=at;last_end_=first+count;
    }
    void discard_committed(std::uint64_t frontier) noexcept {
        while(count_ && row(0).end<=frontier){begin_=(begin_+1)%rows_.size();--count_;}
        if(!count_)begin_=0;
    }
    Pending pending(std::uint64_t frontier,std::uint64_t ingress)const {
        if(frontier>ingress)throw Error("committed frontier exceeds capture ingress");
        Pending out;
        for(std::size_t i=0;i<count_;++i) {
            const auto& f=row(i);
            const auto first=std::max(frontier,f.first),end=std::min(ingress,f.end);
            if(first>=end || !f.pcm)continue;
            if(end-first>std::numeric_limits<std::uint64_t>::max()-out.pcm_samples)
                throw Error("deferred input backlog counter exhausted");
            out.pcm_samples+=end-first;
            if(!out.oldest || f.queued_at<*out.oldest)out.oldest=f.queued_at;
        }
        return out;
    }
private:
    const Fence& row(std::size_t i)const noexcept{return rows_[(begin_+i)%rows_.size()];}
    std::vector<Fence> rows_;
    std::size_t begin_=0,count_=0;
    std::optional<Clock::time_point> last_queued_at_;
    std::optional<std::uint64_t> last_end_;
};
}
