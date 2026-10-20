#pragma once
#include "datapump/pattern_receiver.hpp"
#include "datapump/execution.hpp"
#include <exception>
#include <limits>
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
    friend class NativePeakConstruction;
    std::vector<PatternEvidence> storage_;
    bool owned_=false,leased_=false;
};
// Internal redirection only: the caller restores its ordinary grant and retries
// the unchanged factory outside the construction scope. A correlator cannot
// spend the native array loan, or inherit search normalized under that loan.
class NativePeakConstructionFallback final:public std::exception {
public:
    explicit NativePeakConstructionFallback(std::size_t additional=0)noexcept:additional_required_bytes(additional){}
    // Exact extra bytes above the failed native construction loan, never a grant.
    const std::size_t additional_required_bytes;
    const char* what()const noexcept override{return "native peak construction requires ordinary fallback";}
};

// Explicit Live-private construction loan. The array leaves the bank BEFORE
// its grant is computed. Pending handoff is keyed by execution context, so a
// cooperative task sharing this OS thread cannot consume another task's loan.
// Claim occurs at the PatternReceiver facade before Impl/member construction;
// no pending TLS capability remains during template work or worker dispatch.
class NativePeakConstruction {
public:
    NativePeakConstruction(NativePeakWorkspace* workspace,std::size_t candidate_limit,
                           std::size_t* bank_working_bytes=nullptr)
        :workspace_(workspace),ledger_(bank_working_bytes),context_(execution::current_context()) {
        for(auto* p=pending_;p;p=p->next_)if(p->context_==context_)
            throw std::logic_error("native peak construction already pending in this task");
        if(workspace_) {
            if(workspace_->leased_)throw std::logic_error("native peak construction overlaps a borrower");
            if(workspace_->owned_ && candidate_limit && workspace_->storage_.capacity()>=candidate_limit) {
                bytes_=workspace_->capacity_bytes();
                if(ledger_ && *ledger_<bytes_)throw std::logic_error("native peak construction charge exceeds bank");
                carrier_.swap(workspace_->storage_);workspace_->leased_=true;active_=true;
                if(ledger_)*ledger_-=bytes_;
            }
        }
        next_=pending_;pending_=this;
    }
    NativePeakConstruction(const NativePeakConstruction&)=delete;
    NativePeakConstruction& operator=(const NativePeakConstruction&)=delete;
    ~NativePeakConstruction()noexcept {unlink();restore();}
    std::size_t transferred_bytes()const noexcept{return bytes_;}
    bool active()const noexcept{return active_;}
    bool claimed()const noexcept{return claimed_;}
    void set_grant_increase(std::size_t increase) {
        if(claimed_ || increase>bytes_)throw std::logic_error("invalid native peak construction grant");
        grant_increase_=increase;
    }
    std::size_t grant_increase()const noexcept{return grant_increase_;}
    static NativePeakConstruction* claim()noexcept {
        if(!pending_)return nullptr;
        const auto current=execution::current_context();
        auto** link=&pending_;
        while(*link) {
            auto* p=*link;
            if(p->context_==current){*link=p->next_;p->next_=nullptr;p->claimed_=true;return p;}
            link=&p->next_;
        }
        return nullptr;
    }
    class ReceiverScope {
    public:
        ReceiverScope(NativePeakConstruction* owner,std::vector<PatternEvidence>& peaks,
                      std::size_t candidate_limit):owner_(owner&&owner->active_?owner:nullptr) {
            if(!owner_)return;
            if(owner_->borrower_ || peaks.capacity())throw std::logic_error("native construction peak target is not empty");
            // Constructor sizing charges exactly C entries. An oversized whole
            // array is not the same operand/reservation; retry ordinary storage.
            if(!candidate_limit || owner_->carrier_.capacity()!=candidate_limit) {
                owner_->restore();owner_=nullptr;throw NativePeakConstructionFallback{};
            }
            owner_->carrier_.swap(peaks);owner_->borrower_=&peaks;
        }
        ReceiverScope(const ReceiverScope&)=delete;
        ReceiverScope& operator=(const ReceiverScope&)=delete;
        ~ReceiverScope()noexcept {if(owner_)owner_->restore();}
        void require_native(std::size_t additional_required_bytes=0) {
            if(!owner_)return;
            owner_->restore();owner_=nullptr;throw NativePeakConstructionFallback{additional_required_bytes};
        }
    private:
        NativePeakConstruction* owner_=nullptr;
    };
private:
    void unlink()noexcept {
        auto** link=&pending_;
        while(*link){if(*link==this){*link=next_;next_=nullptr;return;}link=&(*link)->next_;}
    }
    void restore()noexcept {
        if(!active_)return;
        if(borrower_){borrower_->clear();carrier_.swap(*borrower_);borrower_=nullptr;}
        if(carrier_.capacity()*sizeof(PatternEvidence)!=bytes_)std::terminate();
        carrier_.clear();workspace_->storage_.swap(carrier_);workspace_->leased_=false;
        if(ledger_) {
            if(bytes_>std::numeric_limits<std::size_t>::max()-*ledger_)std::terminate();
            *ledger_+=bytes_;
        }
        active_=false;
    }
    NativePeakWorkspace* workspace_=nullptr;
    std::vector<PatternEvidence> carrier_;
    std::vector<PatternEvidence>* borrower_=nullptr;
    std::size_t* ledger_=nullptr;
    execution::Context context_=0;
    std::size_t bytes_=0,grant_increase_=0;
    bool active_=false,claimed_=false;
    NativePeakConstruction* next_=nullptr;
    inline static thread_local NativePeakConstruction* pending_=nullptr;
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
