#pragma once
#include "datapump/pattern_receiver.hpp"
#include <stdexcept>
#include <utility>
namespace datapump::modem { class StreamingReceiver; }
namespace datapump::modem::detail {
// Caller-owned, fixed-capacity backing. Adopt and lease only at joined receiver
// boundaries. This object must outlive every lease; it never retains a receiver.
class NativePeakWorkspace {
public:
    NativePeakWorkspace()=default;
    NativePeakWorkspace(const NativePeakWorkspace&)=delete;
    NativePeakWorkspace& operator=(const NativePeakWorkspace&)=delete;
    bool adopt(std::vector<PatternEvidence>& peaks,std::size_t candidate_limit) {
        if(leased_)throw std::logic_error("native peak workspace is currently leased");
        if(owned_) {
            if(storage_.capacity()<candidate_limit)return false;
            peaks.clear();std::vector<PatternEvidence>().swap(peaks);return true;
        }
        if(peaks.capacity()<candidate_limit)return false;
        peaks.clear();storage_.swap(peaks);owned_=true;return true;
    }
    std::size_t capacity_bytes()const noexcept{return storage_.capacity()*sizeof(PatternEvidence);}
    bool owns_storage()const noexcept{return owned_;}
    bool leased()const noexcept{return leased_;}
private:
    friend class NativePeakLease;
    std::vector<PatternEvidence> storage_;
    bool owned_=false,leased_=false;
};
// Explicit transfer BEFORE the caller grants the receiver's active byte loan.
// While active, receiver.working_bytes() counts the transferred array and the
// arena has zero capacity. Restore before caller accounting or modem reset.
// No public configuration/receiver layout or persistent attachment is added.
class NativePeakLease {
public:
    NativePeakLease(NativePeakWorkspace* workspace,PatternReceiver& receiver);
    NativePeakLease(NativePeakWorkspace* workspace,StreamingReceiver& receiver);
    static bool adopt(NativePeakWorkspace&,PatternReceiver&);
    static bool adopt(NativePeakWorkspace&,StreamingReceiver&);
    NativePeakLease(NativePeakWorkspace* workspace,std::vector<PatternEvidence>& peaks,
                    std::size_t candidate_limit):workspace_(workspace),peaks_(&peaks) {
        if(!workspace_)return;
        if(workspace_->leased_)throw std::logic_error("native peak workspace already has a borrower");
        // An unprepared/oversized/owning receiver preserves its original path.
        if(!workspace_->owned_ || workspace_->storage_.capacity()<candidate_limit || peaks.capacity())return;
        transferred_bytes_=workspace_->capacity_bytes();workspace_->leased_=true;
        workspace_->storage_.swap(peaks);active_=true;
    }
    NativePeakLease(const NativePeakLease&)=delete;
    NativePeakLease& operator=(const NativePeakLease&)=delete;
    ~NativePeakLease(){if(active_){peaks_->clear();workspace_->storage_.swap(*peaks_);workspace_->leased_=false;}}
    bool active()const noexcept{return active_;}
    std::size_t transferred_bytes()const noexcept{return transferred_bytes_;}
private:
    NativePeakWorkspace* workspace_=nullptr;
    std::vector<PatternEvidence>* peaks_=nullptr;
    std::size_t transferred_bytes_=0;
    bool active_=false;
};
}
