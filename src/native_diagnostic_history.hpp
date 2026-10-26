#pragma once
#include "datapump/pattern_receiver.hpp"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace datapump::modem {class StreamingReceiver;}
namespace datapump::modem::detail {
// Private publication storage. Logical occupancy follows the original C-entry
// FIFO even though only its last H entries are materialized. Insertions remain
// monotonic across rollback, so occupancy must not be inferred from that counter.
template<class Entry> class NativeDiagnosticHistoryTail {
public:
    NativeDiagnosticHistoryTail(std::size_t capacity,std::size_t limit)
        :capacity_(capacity),limit_(limit) {
        if(!limit || limit>capacity)throw std::invalid_argument("invalid diagnostic history tail");
    }
    void append(std::vector<Entry>& history,const Entry& item) {
        if(limit_) {
            if(history.size()==limit_)history.erase(history.begin());
            history.push_back(item);
        }
        logical_size_=std::min(capacity_,logical_size_+1);
    }
    // Helper-only diagnostic compaction. No acquisition peak or logical FIFO
    // entry is removed; an active rollback obligation cannot be rebound.
    bool rebind_materialized_limit(std::size_t limit)noexcept {
        if(limit>limit_ || journal_.capacity() || checkpoint_size_)return false;
        limit_=limit;return true;
    }
    void checkpoint(const std::vector<Entry>& history) {
        // Allocate before publishing a new checkpoint. No persistent per-owner
        // rollback array is retained after its cold-replay obligation ends.
        std::vector<Entry> saved(history);
        journal_.swap(saved);checkpoint_size_=logical_size_;
    }
    void rollback(std::vector<Entry>& history,std::uint64_t appended) {
        // This reproduces ORIGINAL FIFO eviction, not ideal snapshot restore:
        // C=2048,L=2048,D=2000 leaves only the last 48 old records.
        const auto remaining=appended>=capacity_?0:
            std::min(checkpoint_size_,capacity_-static_cast<std::size_t>(appended));
        const auto count=std::min(limit_,remaining);
        history.assign(journal_.end()-static_cast<std::ptrdiff_t>(count),journal_.end());
        logical_size_=remaining;release_checkpoint();
    }
    void release_checkpoint()noexcept {
        std::vector<Entry>().swap(journal_);checkpoint_size_=0;
    }
    std::size_t logical_size()const noexcept{return logical_size_;}
    std::size_t limit()const noexcept{return limit_;}
    std::size_t checkpoint_bytes()const noexcept{return journal_.capacity()*sizeof(Entry);}
    std::size_t working_bytes()const noexcept{return sizeof(*this)+checkpoint_bytes();}
private:
    std::size_t capacity_=0,limit_=0,logical_size_=0,checkpoint_size_=0;
    std::vector<Entry> journal_;
};

struct NativeDiagnosticHistoryRequirements {
    // Additional live allocation while constructor-owned C entries still exist.
    std::size_t binding_growth_bytes=0,binding_peak_bytes=0;
    // Actual steady backing, plus separately reserved cold-checkpoint headroom.
    std::size_t bound_working_bytes=0,checkpoint_growth_bytes=0,bound_reserved_bytes=0;
};
struct NativeDiagnosticHistoryAccounting {
    bool bound=false;
    std::size_t logical_size=0,limit=0,history_capacity_bytes=0,checkpoint_bytes=0,control_bytes=0;
};
// Joined receiver-worker boundary only. The receiver must be native and newly
// constructed, before any input. Failure/exception leaves it unbound and intact.
// The caller must account for the constructor allocation AND binding_growth_bytes
// simultaneously before granting the allocation. No public setting is introduced.
struct NativeDiagnosticHistoryAccess {
    static std::optional<NativeDiagnosticHistoryRequirements> requirements(const PatternReceiver&,std::size_t limit);
    static bool bind(PatternReceiver&,std::size_t limit,std::size_t available_growth_bytes);
    static NativeDiagnosticHistoryAccounting accounting(const PatternReceiver&)noexcept;
    static std::optional<NativeDiagnosticHistoryRequirements> requirements(const StreamingReceiver&,std::size_t limit);
    static bool bind(StreamingReceiver&,std::size_t limit,std::size_t available_growth_bytes);
    static NativeDiagnosticHistoryAccounting accounting(const StreamingReceiver&)noexcept;
};
}
