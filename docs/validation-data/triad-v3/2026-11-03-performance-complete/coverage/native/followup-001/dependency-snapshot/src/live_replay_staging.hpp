#pragma once
#include "live_receiver_layout.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <type_traits>

namespace datapump::live::detail {
// One shared reservation, not a per-reception copy of the replay history.
// All access (including source-thread writes) is serialized by Session::mutex.
class ReplayStaging {
public:
    struct Checkpoint {
        std::size_t frame=0,text_size=0,missing_symbols=0;
        std::uint64_t sequence=0,received_bits=0;
        double frequency_hz=0,virtual_seconds=0;
        std::optional<double> pattern_score;
        Checkpoint(const SignalUpdate& e,std::size_t at):frame(at),text_size(e.text.size()),
            missing_symbols(e.missing_symbols),sequence(e.sequence),received_bits(e.received_bits),
            frequency_hz(e.frequency_hz),virtual_seconds(e.virtual_seconds),pattern_score(e.pattern_score){}
    };
    struct Timeline {
        std::vector<Checkpoint> checkpoints;
        std::vector<std::uint8_t> prefix;
        std::size_t prefix_bits=0;
        bool extends(std::string_view text)const noexcept {
            if(text.size()<prefix_bits)return false;
            for(std::size_t i=0;i<prefix_bits;++i)
                if(static_cast<unsigned>(text[i]-'0')!=((prefix[i/8]>>(i%8))&1U))return false;
            return true;
        }
    };
    struct Entry {
        SignalUpdate event;
        std::unique_ptr<transfer::Received> received;
        std::size_t frame=0;
        std::unique_ptr<Timeline> timeline;
        bool terminal() const {return event.validated || event.complete;}
        bool recovering() const {
            return event.recovery_progress.state!=transfer::RecoveryState::none ||
                (received && (received->recovery || received->recovery_progress.state!=transfer::RecoveryState::none));
        }
        std::size_t observations()const noexcept{return timeline?timeline->checkpoints.size():1;}
        Entry snapshot(std::size_t index)const {
            const auto& point=timeline->checkpoints.at(index);
            Entry result;result.event=event;result.frame=point.frame;
            result.event.text=std::string(point.text_size,'0');
            for(std::size_t i=0;i<point.text_size;++i)
                result.event.text[i]=static_cast<char>('0'+((timeline->prefix[i/8]>>(i%8))&1U));
            result.event.sequence=point.sequence;result.event.received_bits=point.received_bits;
            result.event.frequency_hz=point.frequency_hz;result.event.virtual_seconds=point.virtual_seconds;
            result.event.missing_symbols=point.missing_symbols;result.event.pattern_score=point.pattern_score;
            return result;
        }
    };
private:
    static_assert(std::is_nothrow_move_constructible_v<Entry> && std::is_nothrow_move_assignable_v<Entry> &&
                  std::is_nothrow_move_constructible_v<transfer::Received>);
    // Pointer slots have fixed, fully charged capacity. No vector growth or
    // allocator-dependent list-node estimate is hidden from the byte quota.
    std::array<std::unique_ptr<Entry>,replay_frames+maximum_events> entries_{};
    struct Presented {std::uint64_t id=0,revision=0;};
    std::array<Presented,maximum_events> presented_{};
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
        auto bytes=add(sizeof(Entry),add(strings(e.event),e.received?add(sizeof(transfer::Received),result_metadata(*e.received)):0));
        if(e.timeline)bytes=add(bytes,add(sizeof(Timeline),add(
            e.timeline->checkpoints.capacity()*sizeof(Checkpoint),e.timeline->prefix.capacity())));
        return bytes;
    }
    static std::size_t publication(const Entry& e) {
        if(!e.timeline)return sizeof(Entry);
        // snapshot's local/return object can coexist with drain's destination
        // when named return value optimization is unavailable. Their dynamic
        // buffers move, but all three transient shells still need headroom.
        return add(3*sizeof(Entry),add(strings(e.event),e.timeline->prefix_bits));
    }
    std::size_t resident_bytes()const {
        auto bytes=sizeof(*this)+4*sizeof(void*);
        for(const auto& e:entries_)if(e)bytes=add(bytes,metadata(*e));
        return bytes;
    }
    static bool compact(const Entry& e) {
        return !e.terminal() && e.event.binary && e.event.text.size()<=replay_text_limit &&
            std::all_of(e.event.text.begin(),e.event.text.end(),[](char c){return c=='0'||c=='1';});
    }
    static bool same_prototype(const SignalUpdate& a,const SignalUpdate& b) {
        const auto scalar=[](double x,double y) {
            return std::bit_cast<std::uint64_t>(x)==std::bit_cast<std::uint64_t>(y);
        };
        const auto optional_scalar=[&](const auto& x,const auto& y) {
            return x.has_value()==y.has_value() && (!x || scalar(*x,*y));
        };
        const auto accuracy=[](const auto& x,const auto& y) {
            return x.has_value()==y.has_value() && (!x ||
                std::tie(x->received_data_bits,x->corrected_data_bits,x->missing_data_bits)==
                std::tie(y->received_data_bits,y->corrected_data_bits,y->missing_data_bits));
        };
        const auto fec=[](const auto& x,const auto& y) {
            return std::tie(x.received_bits,x.corrected_bits,x.missing_bits,x.corrected_bytes,x.erased_bytes,x.repaired_bytes)==
                std::tie(y.received_bits,y.corrected_bits,y.missing_bits,y.corrected_bytes,y.erased_bytes,y.repaired_bytes);
        };
        return a.id==b.id && a.revision==b.revision && a.binary==b.binary &&
            a.validated==b.validated && a.complete==b.complete && a.reception_id==b.reception_id &&
            scalar(a.snr_db,b.snr_db) && a.received_bytes==b.received_bytes && a.expected_bytes==b.expected_bytes &&
            optional_scalar(a.preamble_received_percent,b.preamble_received_percent) && accuracy(a.pre_fec_accuracy,b.pre_fec_accuracy) &&
            a.expected_bits==b.expected_bits && fec(a.fec_stats.data,b.fec_stats.data) &&
            fec(a.fec_stats.integrity,b.fec_stats.integrity) && fec(a.fec_stats.parity,b.fec_stats.parity) &&
            a.raw_bits==b.raw_bits && a.superseded_ids==b.superseded_ids && a.helper_carrier==b.helper_carrier &&
            std::tie(a.recovery_progress.state,a.recovery_progress.attempts,a.recovery_progress.total,a.recovery_progress.elapsed)==
                std::tie(b.recovery_progress.state,b.recovery_progress.attempts,b.recovery_progress.total,b.recovery_progress.elapsed);
    }
    static bool alias(const SignalUpdate& replacement,std::uint64_t id) {
        return std::find(replacement.superseded_ids.begin(),replacement.superseded_ids.end(),id)!=replacement.superseded_ids.end();
    }
    static bool obsolete(const SignalUpdate& old,const SignalUpdate& replacement) {
        return alias(replacement,old.id) || (old.id==replacement.id && old.revision<replacement.revision);
    }
    static bool completed_by(const Entry& pending,const Entry& done) {
        return alias(done.event,pending.event.id) ||
            (done.event.id==pending.event.id && done.event.revision>=pending.event.revision);
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
        auto temporary=std::size_t{0};
        for(const auto& e:entries_)if(e)temporary=std::max(temporary,publication(*e));
        return add(resident_bytes(),temporary);
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
        if(received) {
            const auto incoming=add(metadata(candidate),add(sizeof(transfer::Received),result_metadata(*received)));
            if(add(resident_bytes(),add(incoming,sizeof(Entry)))>budget_)
                throw Error("simulation receive staging exceeds the DSP event capacity");
            candidate.received=std::make_unique<transfer::Received>(std::move(*received));
        }
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
        const auto replaces=[&](const Entry& e,std::size_t at) {
            if(obsolete(e.event,candidate.event)) {
                // Earlier pending frames record accepted progress before this
                // interpretation changed. Withdraw obsolete future results,
                // not that history. A pending replacement may coalesce its own
                // frame; a terminal still needs that frame's pending prefix.
                return e.terminal() || at>frame || (!candidate.terminal() && at==frame);
            }
            return e.event.id==candidate.event.id && e.event.revision==candidate.event.revision &&
                ((e.terminal() && candidate.terminal()) || (!e.terminal() && !candidate.terminal() && at==frame));
        };
        std::array<std::uint64_t,replay_frames+maximum_events> removed_frames{};
        std::array<bool,replay_frames+maximum_events> remove{};
        Entry* merge=nullptr;
        for(std::size_t i=0;i<entries_.size();++i)if(entries_[i]) {
            const auto& e=*entries_[i];
            if(e.timeline) {
                auto remaining=std::size_t{0};
                for(const auto& point:e.timeline->checkpoints) {
                    if(replaces(e,point.frame))removed_frames[i]|=std::uint64_t{1}<<point.frame;
                    else ++remaining;
                }
                remove[i]=!remaining;
                if(!merge && compact(candidate) && same_prototype(e.event,candidate.event) &&
                   e.timeline->extends(candidate.event.text))merge=entries_[i].get();
            } else remove[i]=replaces(e,e.frame);
        }
        auto construction_peak=add(resident_bytes(),add(metadata(candidate),sizeof(Entry)));
        if(compact(candidate)) {
            const auto points=merge?merge->timeline->checkpoints.size()+1:1;
            const auto packed=(candidate.event.text.size()+7)/8;
            const auto scratch=add(sizeof(Timeline),add(points*sizeof(Checkpoint),packed));
            if(add(resident_bytes(),add(add(metadata(candidate),scratch),sizeof(Entry)))>budget_)
                throw Error("simulation receive staging exceeds the DSP event capacity");
            candidate.timeline=std::make_unique<Timeline>();
            if(merge) {
                candidate.timeline->checkpoints.reserve(merge->timeline->checkpoints.size()+1);
                for(const auto& point:merge->timeline->checkpoints)
                    if(!replaces(*merge,point.frame))candidate.timeline->checkpoints.push_back(point);
            }
            candidate.timeline->checkpoints.emplace_back(candidate.event,frame);
            candidate.frame=candidate.timeline->checkpoints.front().frame;
            candidate.timeline->prefix.resize(packed);
            candidate.timeline->prefix_bits=candidate.event.text.size();
            for(std::size_t i=0;i<candidate.event.text.size();++i)
                candidate.timeline->prefix[i/8]|=static_cast<std::uint8_t>((candidate.event.text[i]-'0')<<(i%8));
            construction_peak=add(resident_bytes(),add(metadata(candidate),sizeof(Entry)));
            if(construction_peak>budget_)
                throw Error("simulation receive staging exceeds the DSP event capacity");
            std::string{}.swap(candidate.event.text);
        }
        std::array<const Entry*,replay_frames+maximum_events+1> projected{};
        std::size_t projected_count=0;
        std::size_t bytes=sizeof(*this)+4*sizeof(void*),content=0,pending=0,terminal=0,jobs=0,reserved=0,temporary=0;
        const auto count=[&](const Entry& e,const transfer::Received* result) {
            projected[projected_count++]=&e;
            bytes=add(bytes,metadata(e));
            temporary=std::max(temporary,publication(e));
            if(result && !e.received)bytes=add(bytes,add(sizeof(transfer::Received),result_metadata(*result)));
            (e.terminal()?terminal:pending)++;
            if(result) {
                content=add(content,result->content.message.data.capacity());
                if(result->recovery) {++jobs;reserved=add(reserved,add(result->recovery->workspace_bound(),add(recovery_base_,add(strings(e.event),add(result_metadata(*result),result->content.message.data.capacity())))));}
            }
        };
        for(std::size_t i=0;i<entries_.size();++i)
            if(entries_[i] && !remove[i] && entries_[i].get()!=merge)
                count(*entries_[i],entries_[i]->received?&*entries_[i]->received:nullptr);
        count(candidate,candidate.received?&*candidate.received:preserve && preserve->received?&*preserve->received:nullptr);
        // Independent receptions can share every replay frame. Share the
        // existing pointer slots, while reserving one eventual terminal for
        // each unfinished ID rather than assuming one pending entry per frame.
        std::array<std::uint64_t,maximum_events> pending_ids{};
        std::array<bool,maximum_events> unfinished{};
        std::size_t pending_ids_count=0;
        for(std::size_t i=0;i<projected_count;++i) {
            const auto& e=*projected[i];
            if(e.terminal())continue;
            auto id=std::find(pending_ids.begin(),pending_ids.begin()+pending_ids_count,e.event.id);
            if(id==pending_ids.begin()+pending_ids_count) {
                if(pending_ids_count==pending_ids.size())
                    throw Error("simulation receive staging exceeds the DSP event capacity");
                pending_ids[pending_ids_count++]=e.event.id;
            }
            const auto index=static_cast<std::size_t>(id-pending_ids.begin());
            const bool completed=std::any_of(projected.begin(),projected.begin()+projected_count,[&](const auto* done) {
                return done->terminal() && done->event.revision>=e.event.revision &&
                    (done->event.id==e.event.id || alias(done->event,e.event.id));
            });
            unfinished[index]=unfinished[index] || !completed;
        }
        const auto future_terminals=static_cast<std::size_t>(std::count(unfinished.begin(),unfinished.end(),true));
        construction_peak=std::max(construction_peak,add(resident_bytes(),add(metadata(candidate),sizeof(Entry))));
        if(terminal+future_terminals>maximum_events ||
           pending+terminal+future_terminals>entries_.size() ||
           add(bytes,temporary)>budget_ || construction_peak>budget_)
            throw Error("simulation receive staging exceeds the DSP event capacity");
        if(content>content_limit)throw Error("incoming content exceeds remaining receive cache capacity");
        if(jobs>recovery_slots || reserved>recovery_bytes)
            throw Error("simulation receive staging exceeds the recovery capacity");
        // Allocation failure also leaves the prior staging intact. Everything
        // after allocation uses only nonthrowing moves and erasure.
        auto entry=std::make_unique<Entry>(std::move(candidate));
        if(preserve && !entry->received)entry->received=std::move(preserve->received);
        for(std::size_t i=0;i<entries_.size();++i)if(entries_[i]) {
            if(remove[i] || entries_[i].get()==merge)entries_[i].reset();
            else if(entries_[i]->timeline && removed_frames[i]) {
                std::erase_if(entries_[i]->timeline->checkpoints,[&](const auto& point) {
                    return removed_frames[i]&(std::uint64_t{1}<<point.frame);
                });
                entries_[i]->frame=entries_[i]->timeline->checkpoints.front().frame;
            }
        }
        const auto slot=std::find(entries_.begin(),entries_.end(),nullptr);
        const auto* accepted=&entry->event;
        *slot=std::move(entry);
        return accepted;
    }
    void discard_obsolete(const SignalUpdate& replacement) {
        for(auto& e:entries_)if(e && obsolete(e->event,replacement))e.reset();
    }
    void clear_recoveries() {
        // First withdraw the recovering terminal's pending revisions/aliases.
        // A later independent revision of that ID must remain present.
        for(auto& e:entries_)if(e && !e->terminal() && std::any_of(entries_.begin(),entries_.end(),[&](const auto& done) {
            return done && done->recovering() &&
                completed_by(*e,*done);
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
            return done && done->terminal() &&
                completed_by(*e,*done);
        }))e.reset();
        for(;;) {
            auto next=entries_.end();
            std::size_t checkpoint=0;std::uint64_t sequence=UINT64_MAX;
            for(auto i=entries_.begin();i!=entries_.end();++i)if(*i) {
                if((*i)->timeline) {
                    const auto& points=(*i)->timeline->checkpoints;
                    for(std::size_t p=0;p<points.size();++p)
                        if(points[p].frame<due && (next==entries_.end() || points[p].sequence<sequence)) {
                            next=i;checkpoint=p;sequence=points[p].sequence;
                        }
                } else if(((*i)->terminal()?finished:(*i)->frame<due) &&
                    (next==entries_.end() || (*i)->event.sequence<sequence)) {
                    next=i;sequence=(*i)->event.sequence;
                }
            }
            if(next==entries_.end())return;
            // Finish every allocating reconstruction before removing its dated
            // checkpoint. Failure leaves the original timeline intact.
            Entry entry;
            if((*next)->timeline) {
                entry=(*next)->snapshot(checkpoint);
                if(add(resident_bytes(),metadata(entry))>budget_)
                    throw Error("simulation receive publication exceeds its reserved workspace");
                auto& points=(*next)->timeline->checkpoints;
                points.erase(points.begin()+checkpoint);
                if(points.empty())next->reset();else (*next)->frame=points.front().frame;
            } else {entry=std::move(**next);next->reset();}
            if(!entry.terminal()) {
                auto p=std::find_if(presented_.begin(),presented_.end(),[&](const auto& item){return item.id==entry.event.id;});
                if(p==presented_.end())p=std::find_if(presented_.begin(),presented_.end(),[](const auto& item){return !item.id;});
                // Admission bounds distinct pending IDs by maximum_events;
                // no source writes occur after its ownership handoff.
                if(p!=presented_.end())*p={entry.event.id,entry.event.revision};
            }
            publish(std::move(entry));
        }
    }
};
}
