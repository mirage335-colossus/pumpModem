#pragma once
#include "live_receiver_layout.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <type_traits>

namespace datapump::live::detail {
// One shared reservation, not a per-reception copy of the replay history.
// All access (including source-thread writes) is serialized by Session::mutex.
class ReplayStaging {
public:
    struct Entry {
        SignalUpdate event;
        std::unique_ptr<transfer::Received> received;
        std::size_t frame=0;
        bool terminal() const {return event.validated || event.complete;}
        bool recovering() const {
            return event.recovery_progress.state!=transfer::RecoveryState::none ||
                (received && (received->recovery || received->recovery_progress.state!=transfer::RecoveryState::none));
        }
    };
private:
    static_assert(std::is_nothrow_move_constructible_v<Entry> && std::is_nothrow_move_assignable_v<Entry> &&
                  std::is_nothrow_move_constructible_v<transfer::Received>);
    // Pointer slots have fixed, fully charged capacity. No vector growth or
    // allocator-dependent list-node estimate is hidden from the byte quota.
    std::array<std::unique_ptr<Entry>,replay_frames+maximum_events> entries_{};
    struct Presented {std::uint64_t id=0,revision=0;};
    std::array<Presented,replay_frames> presented_{};
    std::size_t budget_=0,frames_=0,recovery_base_=0;
    static std::size_t add(std::size_t a,std::size_t b) {
        return b>std::numeric_limits<std::size_t>::max()-a?std::numeric_limits<std::size_t>::max():a+b;
    }
    static std::size_t strings(const SignalUpdate& e) {
        return add(add(e.text.capacity(),e.raw_bits.capacity()),
            add(e.reception_id.capacity(),e.superseded_ids.capacity()*sizeof(std::uint64_t)));
    }
    static std::size_t result_metadata(const transfer::Received& r) {
        const auto& m=r.content.message;
        auto n=add(r.raw_bits.capacity(),r.error.capacity());
        for(const auto* s:{&m.filename,&m.callsign,&m.grid})n=add(n,s->capacity());
        n=add(n,r.diagnostics.waveform.capacity()*sizeof(float));
        return add(n,r.diagnostics.constellation.capacity()*sizeof(std::complex<double>));
    }
    static std::size_t metadata(const Entry& e) {
        // Pending observations do not each reserve a whole Received object.
        // Charge the result object/handles only for entries that own one, then
        // every retained dynamic capacity in addition.
        return add(sizeof(Entry),add(strings(e.event),e.received?add(sizeof(transfer::Received),result_metadata(*e.received)):0));
    }
    static bool alias(const SignalUpdate& replacement,std::uint64_t id) {
        return std::find(replacement.superseded_ids.begin(),replacement.superseded_ids.end(),id)!=replacement.superseded_ids.end();
    }
    static bool obsolete(const SignalUpdate& old,const SignalUpdate& replacement) {
        return alias(replacement,old.id) || (old.id==replacement.id && old.revision<replacement.revision);
    }
    std::size_t recovery_retained(const Entry& e) const {
        return add(recovery_base_,add(strings(e.event),add(result_metadata(*e.received),e.received->content.message.data.capacity())));
    }
    std::size_t recovery_reservation(const Entry& e) const {
        return add(e.received->recovery->workspace_bound(),recovery_retained(e));
    }
public:
    ReplayStaging(std::size_t budget,std::size_t frames,std::size_t recovery_base=4096)
        :budget_(budget),frames_(frames),recovery_base_(recovery_base) {
        if(!frames || frames>replay_frames || metadata_bytes()>budget)
            throw Error("DSP workspace cannot hold simulation receive staging");
    }
    const auto& entries() const {return entries_;}
    std::size_t metadata_bytes() const {
        // Include the shared ownership control block's bounded bookkeeping.
        std::size_t n=sizeof(*this)+4*sizeof(void*);
        for(const auto& e:entries_)if(e)n=add(n,metadata(*e));
        return n;
    }
    std::size_t content_bytes() const {
        std::size_t n=0;
        for(const auto& e:entries_)if(e && e->received)n=add(n,e->received->content.message.data.capacity());
        return n;
    }
    std::size_t recovery_count() const {
        return std::count_if(entries_.begin(),entries_.end(),[](const auto& e){return e && e->received && e->received->recovery;});
    }
    std::size_t recovery_reserved_bytes() const {
        std::size_t n=0;
        // Keep the existing coordinator's opaque source/context reservation.
        // Event/result capacities belong to this DSP pool until handoff.
        for(const auto& e:entries_)if(e && e->received && e->received->recovery)
            n=add(n,recovery_reservation(*e));
        return n;
    }
    std::size_t recovery_working_bytes() const {
        std::size_t n=0;
        for(const auto& e:entries_)if(e && e->received && e->received->recovery)
            n=add(n,add(e->received->recovery->working_bytes(),recovery_retained(*e)));
        return n;
    }
    // Admission is checked before changing any previously staged entry. A
    // same-frame observation may coalesce only with its own ID/revision.
    const SignalUpdate* stage(SignalUpdate event,std::size_t frame,std::optional<transfer::Received> received,
               std::size_t content_limit,std::size_t recovery_slots,std::size_t recovery_bytes) {
        Entry candidate{std::move(event),{},frame};
        if(received)candidate.received=std::make_unique<transfer::Received>(std::move(*received));
        if(frame>=frames_ || (candidate.received && !candidate.terminal()))
            throw Error("invalid simulation receive staging transaction");
        Entry* preserve=nullptr;
        for(const auto& e:entries_)if(e && e->event.id==candidate.event.id) {
            if(e->event.revision>candidate.event.revision)return nullptr;
            if(e->event.revision==candidate.event.revision && e->terminal()) {
                if(!candidate.terminal())return nullptr;
                if(!candidate.received)preserve=e.get();
            }
        }
        const auto replaces=[&](const Entry& e) {
            return obsolete(e.event,candidate.event) ||
                (e.event.id==candidate.event.id && e.event.revision==candidate.event.revision &&
                 ((e.terminal() && candidate.terminal()) || (!e.terminal() && !candidate.terminal() && e.frame==frame)));
        };
        std::size_t bytes=sizeof(*this)+4*sizeof(void*),content=0,pending=0,terminal=0,jobs=0,reserved=0;
        const auto count=[&](const Entry& e,const transfer::Received* result) {
            bytes=add(bytes,add(sizeof(Entry),add(strings(e.event),result?add(sizeof(transfer::Received),result_metadata(*result)):0)));
            (e.terminal()?terminal:pending)++;
            if(result) {
                content=add(content,result->content.message.data.capacity());
                if(result->recovery) {++jobs;reserved=add(reserved,add(result->recovery->workspace_bound(),add(recovery_base_,add(strings(e.event),add(result_metadata(*result),result->content.message.data.capacity())))));}
            }
        };
        for(const auto& e:entries_)if(e && !replaces(*e))count(*e,e->received?&*e->received:nullptr);
        count(candidate,candidate.received?&*candidate.received:preserve && preserve->received?&*preserve->received:nullptr);
        if(pending>frames_ || terminal>maximum_events || bytes>budget_)
            throw Error("simulation receive staging exceeds the DSP event capacity");
        if(content>content_limit)throw Error("incoming content exceeds remaining receive cache capacity");
        if(jobs>recovery_slots || reserved>recovery_bytes)
            throw Error("simulation receive staging exceeds the recovery capacity");
        // Allocation failure also leaves the prior staging intact. Everything
        // after allocation uses only nonthrowing moves and erasure.
        std::array<bool,replay_frames+maximum_events> remove{};
        for(std::size_t i=0;i<entries_.size();++i)remove[i]=entries_[i] && replaces(*entries_[i]);
        auto entry=std::make_unique<Entry>(std::move(candidate));
        if(preserve && !entry->received)entry->received=std::move(preserve->received);
        for(std::size_t i=0;i<entries_.size();++i)if(remove[i])entries_[i].reset();
        const auto slot=std::find(entries_.begin(),entries_.end(),nullptr);
        const auto* accepted=&entry->event;
        *slot=std::move(entry);
        return accepted;
    }
    void discard_obsolete(const SignalUpdate& replacement) {
        for(auto& e:entries_)if(e && obsolete(e->event,replacement))e.reset();
    }
    void clear_recoveries() {
        // First withdraw the recovering terminal's own pending revisions. A
        // later independent revision of that ID must remain present.
        for(auto& e:entries_)if(e && !e->terminal() && std::any_of(entries_.begin(),entries_.end(),[&](const auto& done) {
            return done && done->recovering() && done->event.id==e->event.id && done->event.revision>=e->event.revision;
        }))e.reset();
        for(auto& e:entries_)if(e && e->recovering())e.reset();
    }
    bool owns_pending(const SignalUpdate& event) const {
        return std::any_of(presented_.begin(),presented_.end(),[&](const auto& p) {
            return p.id==event.id && p.revision>=event.revision;
        }) || std::any_of(entries_.begin(),entries_.end(),[&](const auto& e) {
            return e && e->event.id==event.id && e->event.revision>=event.revision;
        });
    }
    void clear() {for(auto& e:entries_)e.reset();presented_={};}
    template<class Publish> void drain(std::size_t due,bool finished,Publish publish) {
        if(finished)for(auto& e:entries_)if(e && !e->terminal() && std::any_of(entries_.begin(),entries_.end(),[&](const auto& done) {
            return done && done->terminal() && done->event.id==e->event.id && done->event.revision>=e->event.revision;
        }))e.reset();
        for(;;) {
            auto next=entries_.end();
            for(auto i=entries_.begin();i!=entries_.end();++i)if(*i &&
                ((*i)->terminal()?finished:(*i)->frame<due) &&
                (next==entries_.end() || (*i)->event.sequence<(*next)->event.sequence))next=i;
            if(next==entries_.end())return;
            auto entry=std::move(*next); // release all accounting before handoff
            if(!entry->terminal()) {
                auto p=std::find_if(presented_.begin(),presented_.end(),[&](const auto& item){return item.id==entry->event.id;});
                if(p==presented_.end())p=std::find_if(presented_.begin(),presented_.end(),[](const auto& item){return !item.id;});
                // At most frames_ pending entries existed before replay; no
                // source writes occur after its ownership handoff.
                if(p!=presented_.end())*p={entry->event.id,entry->event.revision};
            }
            publish(std::move(*entry));
        }
    }
};
}
