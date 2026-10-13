#pragma once
#include "pattern_correlator_batch.hpp"
#include <bit>
#include <atomic>
#include <cstdlib>
#include <string_view>
#include <optional>
namespace datapump::modem::detail {
inline bool scheduled_outer_public_replay_requested() {
    static const bool enabled=[] {const auto* p=std::getenv("DATAPUMP_SCHED_OUTER_PUBLIC_REPLAY");return p&&std::string_view(p)=="1";}();
    return enabled;
}
inline constexpr std::size_t scheduled_outer_public_replay_frame=2048;

struct ScheduledOuterPublicCounts {std::uint64_t pieces=0,equal_pieces=0,samples=0,events=0,bound_fallbacks=0;};
// Explicit PROCESS-wide diagnostics, summed once per completed common-family
// call. No mandatory receiver/Work/Statistics fields and no per-sample atomic.
inline std::array<std::atomic<std::uint64_t>,5> scheduled_outer_public_totals{};
inline ScheduledOuterPublicCounts scheduled_outer_public_counts() {
    return {scheduled_outer_public_totals[0].load(),scheduled_outer_public_totals[1].load(),
        scheduled_outer_public_totals[2].load(),scheduled_outer_public_totals[3].load(),scheduled_outer_public_totals[4].load()};
}
struct ScheduledOuterPublicScope {
    ScheduledOuterPublicCounts count;
    ~ScheduledOuterPublicScope() {
        if(!scheduled_outer_public_replay_requested())return;
        const std::array<std::uint64_t,5> value{count.pieces,count.equal_pieces,count.samples,count.events,count.bound_fallbacks};
        for(std::size_t i=0;i<5;++i)scheduled_outer_public_totals[i].fetch_add(value[i],std::memory_order_relaxed);
    }
};

struct ScheduledOuterPublicState {
    std::array<std::complex<double>,17> pulse;
    std::uint64_t first,last;bool initialized;
    explicit ScheduledOuterPublicState(const CorrelationOuterEvidence& x):pulse(x.pulse),first(x.first),last(x.last),initialized(x.initialized){}
    void copy_to(CorrelationOuterEvidence& x)const {x.pulse=pulse;x.first=first;x.last=last;x.initialized=initialized;}
    static void copy_fields(const CorrelationOuterEvidence& a,CorrelationOuterEvidence& b) {
        b.pulse=a.pulse;b.first=a.first;b.last=a.last;b.initialized=a.initialized;
    }
    static bool equal(const CorrelationOuterEvidence& a,const CorrelationOuterEvidence& b) {
        if(a.first!=b.first||a.last!=b.last||a.initialized!=b.initialized)return false;
        for(std::size_t i=0;i<17;++i) {
            if(std::bit_cast<std::uint64_t>(a.pulse[i].real())!=std::bit_cast<std::uint64_t>(b.pulse[i].real())||
               std::bit_cast<std::uint64_t>(a.pulse[i].imag())!=std::bit_cast<std::uint64_t>(b.pulse[i].imag()))return false;
        }
        return true;
    }
};
// One certificate per ORIGINAL raw piece, never retained across targets/pieces.
// The existing one-frequency common proof supplies identical increments and
// PRIVATE callback context. Only the measured PUBLIC ring is compared here.
struct ScheduledOuterPublicLockstep {
    CorrelationOuterEvidence *leader=nullptr,*follower=nullptr;
    std::uint64_t first_chip=0,samples=0,events=0;
    bool checked=false,active=false;
    bool bind(CorrelationOuterEvidence* a,CorrelationOuterEvidence* b,std::uint64_t chip,bool eligible) {
        if(!eligible||!a||!b||a==b){active=false;return false;}
        if(checked) {
            if(a!=leader||b!=follower||chip!=first_chip)active=false;
            return active;
        }
        checked=true;leader=a;follower=b;first_chip=chip;
        active=ScheduledOuterPublicState::equal(*a,*b);return active;
    }
};
class ScheduledOuterPublicReplay {
    struct Event {std::uint64_t index;double real,imag;}; // POD: no unused slab clearing
    std::optional<ScheduledOuterPublicState> initial_;
    std::array<Event,17> event_;std::size_t count_=0;
public:
    explicit ScheduledOuterPublicReplay(const CorrelationOuterEvidence& leader,bool save_initial=true) {
        if(save_initial)initial_.emplace(leader);
    }
    template<class Pulse> static bool may_flush(const CorrelationOuterEvidence& a,std::span<const Pulse> pulses) {
        if(pulses.empty())return false;
        const auto first=a.initialized?a.first:(pulses.front().index>16?pulses.front().index-16:0);
        return pulses.back().index-first>=17; // only after bounded() succeeds
    }
    template<class Pulse> static bool bounded(const CorrelationOuterEvidence& a,std::span<const Pulse> pulses) {
        if(pulses.empty())return true;
        for(std::size_t i=1;i<pulses.size();++i)if(pulses[i].index<pulses[i-1].index)return false;
        const auto first=a.initialized?a.first:(pulses.front().index>16?pulses.front().index-16:0);
        if(pulses.front().index<first)return false;
        const auto distance=pulses.back().index-first;
        return distance<17 || distance-16<=17;
    }
    template<class Pulse,class Coefficients>
    void leader(CorrelationOuterEvidence& a,std::span<const Pulse> pulses,std::complex<double> projection,Coefficients coefficients) {
        for(const auto& pulse:pulses)a.add(pulse.index,pulse.weight*projection,[&](std::uint64_t index) {
            // Original callback precedes original measured read/clear. Record
            // only after it returns, so leader exceptions match ORIGINAL state.
            const auto values=coefficients(index);
            if(count_>=event_.size())throw Error("outer replay violated prechecked event bound");
            event_[count_++]={index,a.pulse[index%17].real(),a.pulse[index%17].imag()};return values;
        });
    }
    template<class Pulse,class Coefficients>
    void follower(CorrelationOuterEvidence& b,const CorrelationOuterEvidence& a,
                  std::span<const Pulse> pulses,std::complex<double> projection,Coefficients coefficients)const {
        for(std::size_t k=0;k<count_;++k) {
            b.first=event_[k].index; // also original virtual first for an uninitialized ring
            b.pulse[b.first%17]={event_[k].real,event_[k].imag};
            try {b.flush(coefficients);}catch(...) {
                // Preserve EXACT public state at the failed original callback.
                // Private dot/variance already contain precisely prior events.
                restore_throw_prefix(b,pulses,projection,k);throw;
            }
        }
        ScheduledOuterPublicState::copy_fields(a,b); // NEVER copy private dot/variance/count
    }
    std::size_t events()const noexcept{return count_;}
private:
    template<class Pulse>
#if defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__GNUC__)||defined(__clang__)
    __attribute__((noinline))
#endif
    void restore_throw_prefix(CorrelationOuterEvidence& target,
            std::span<const Pulse> pulses,std::complex<double> projection,std::size_t failed)const {
        if(!initial_)std::terminate(); // caller may omit snapshot ONLY for a preproved zero-event row
        auto public_state=*initial_;std::size_t callbacks=0;
        for(const auto& pulse:pulses) {
            if(!public_state.initialized) {
                public_state.first=pulse.index>16?pulse.index-16:0;
                public_state.last=pulse.index;public_state.initialized=true;
            }
            while(pulse.index-public_state.first>=17) {
                if(callbacks++==failed){public_state.copy_to(target);return;}
                public_state.pulse[public_state.first%17]={};++public_state.first;
            }
            public_state.last=std::max(public_state.last,pulse.index);
            public_state.pulse[pulse.index%17]+=pulse.weight*projection;
        }
        std::terminate(); // failed callback was recorded in this exact ORIGINAL row
    }
};
static_assert(sizeof(ScheduledOuterPublicReplay)+2*sizeof(CorrelationFit)+sizeof(CorrelationProjection)+256<=scheduled_outer_public_replay_frame);
} // namespace datapump::modem::detail
