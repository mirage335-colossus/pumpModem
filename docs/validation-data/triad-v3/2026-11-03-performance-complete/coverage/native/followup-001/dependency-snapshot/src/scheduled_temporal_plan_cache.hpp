#pragma once
#include "scheduled_work_profile.hpp"
#include "datapump/pattern_code.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <type_traits>
#include <span>
#include <stop_token>
#include <utility>

namespace datapump::modem::detail {
// Worker-owned PUBLIC Plan storage. It is deliberately not thread-safe and
// cannot be attached concurrently to other workers/banks without a new owner.
// Plan and key storage never move; callers borrow only through move-only Lease.
template<class Plan,std::size_t MaximumSamples,std::size_t Entries=4>
class ScheduledTemporalPlanCacheT {
    static_assert(MaximumSamples>0 && Entries>0 && Entries<=256);
    enum class State:unsigned char {empty,building,ready};
    struct Row {
        Plan plan{};
        std::array<double,MaximumSamples> coordinates{};
        std::uint64_t symbol=0,chip=0,fingerprint=0,stamp=0;
        std::size_t count=0;
        std::uint32_t pins=0;
        State state=State::empty;
    };
    std::array<Row,Entries> rows_{};
    std::uint64_t clock_=0;
    // Payload generations are physically paired with original row slots.
    // Invalidation precedes every Plan mutation; a current immutable Lease
    // therefore certifies owner/slot/generation without a second key lookup.
    enum class PayloadState:unsigned char {empty,building,ready};
    struct PayloadBase {
        const void* kind;
        explicit PayloadBase(const void* type):kind(type){}
        virtual ~PayloadBase()=default;
        virtual std::size_t bytes()const noexcept=0;
        virtual void invalidate(std::size_t index)noexcept=0;
    };
    template<class Payload> inline static unsigned char payload_kind_=0;
    template<class Payload> struct PayloadArena final:PayloadBase {
        struct Slot {Payload value{};PayloadState state=PayloadState::empty;};
        std::array<Slot,Entries> slots{};
        PayloadArena():PayloadBase(&payload_kind_<Payload>){}
        std::size_t bytes()const noexcept override{return sizeof(PayloadArena);}
        void invalidate(std::size_t index)noexcept override {
            if(slots[index].state==PayloadState::building)std::terminate();
            slots[index].state=PayloadState::empty;
        }
    };
    std::unique_ptr<PayloadBase> payload_; // absent unless separately granted
    template<class Payload> PayloadArena<Payload>* payload_arena()const noexcept {
        return payload_ && payload_->kind==&payload_kind_<Payload>?
            static_cast<PayloadArena<Payload>*>(payload_.get()):nullptr;
    }
    static void cancelled(std::stop_token stop) {
        if(stop.stop_requested())throw Error("temporal Plan cache cancelled");
    }
    std::uint64_t tick()noexcept {
        // Replacement age is advisory; wrap can change LRU quality only.
        if(++clock_==0){for(auto& row:rows_)row.stamp=0;clock_=1;}
        return clock_;
    }
    void unpin(std::size_t index)noexcept {
        auto& row=rows_[index];
        if(row.state!=State::ready || !row.pins)std::terminate();
        --row.pins;
    }
public:
    using PlanType=Plan;
    static constexpr std::size_t capacity=Entries,maximum_samples=MaximumSamples;
    // Additional scalar acquisition/lease/guard frame. Original Plan builder's
    // first[MaximumSamples] frame remains in original Temporal callee reserve.
    // This is a reservation proposal, subject to root compiler qualification.
    static constexpr std::size_t lookup_callee_allowance=512;
    struct Statistics {
        std::uint64_t requests=0,hits=0,misses=0,builds=0,evictions=0,unavailable=0;
        std::uint64_t rejected=0,cancelled=0,exceptions=0,hash_words=0,compared_words=0;
        Statistics& operator+=(const Statistics& b)noexcept {
            requests+=b.requests;hits+=b.hits;misses+=b.misses;builds+=b.builds;evictions+=b.evictions;
            unavailable+=b.unavailable;rejected+=b.rejected;cancelled+=b.cancelled;exceptions+=b.exceptions;
            hash_words+=b.hash_words;compared_words+=b.compared_words;return *this;
        }
    } statistics;
    enum class PayloadStatus {unavailable,hit,built,rejected,busy};
    template<class Payload> struct PayloadResult {
        const Payload* value=nullptr;PayloadStatus status=PayloadStatus::unavailable;
    };
    class Lease {
        friend class ScheduledTemporalPlanCacheT;
        ScheduledTemporalPlanCacheT* owner_=nullptr;std::size_t index_=0;
        Lease(ScheduledTemporalPlanCacheT* owner,std::size_t index):owner_(owner),index_(index){}
    public:
        Lease()=default;Lease(const Lease&)=delete;Lease& operator=(const Lease&)=delete;
        Lease(Lease&& b)noexcept:owner_(std::exchange(b.owner_,nullptr)),index_(b.index_){}
        Lease& operator=(Lease&& b)noexcept {
            if(this!=&b){reset();owner_=std::exchange(b.owner_,nullptr);index_=b.index_;}return *this;
        }
        ~Lease(){reset();}
        void reset()noexcept {if(owner_){owner_->unpin(index_);owner_=nullptr;}}
        explicit operator bool()const noexcept{return owner_!=nullptr;}
        // Structural certificate for the exact leased Tile API. READY is
        // published only after full well_formed+N/C/count validation. Trusted
        // ORIGINAL builders must not retain mutable row aliases after return.
        const Plan* validated_plan()const noexcept {
            if(!owner_ || index_>=Entries)return nullptr;
            const auto& row=owner_->rows_[index_];
            return row.state==State::ready && row.pins?&row.plan:nullptr;
        }
        // Internal bounded numerical producer only. Caller must keep this
        // lease alive and must not release/drop its owner from inside build.
        // Publication follows builder success and final cancellation check.
        template<class Payload,class Builder>
        PayloadResult<Payload> get_or_build_payload(Builder&& build,std::stop_token stop={})const {
            const auto* exact=validated_plan();if(!exact)return {};
            auto* arena=owner_->template payload_arena<Payload>();if(!arena)return {};
            if(stop.stop_requested())cancelled(stop);
            auto& slot=arena->slots[index_];
            if(slot.state==PayloadState::ready)return {&slot.value,PayloadStatus::hit};
            if(slot.state==PayloadState::building)return {nullptr,PayloadStatus::busy};
            slot.state=PayloadState::building;
            try {
                const bool ready=build(slot.value,*exact,stop);
                if(stop.stop_requested())cancelled(stop);
                slot.state=ready?PayloadState::ready:PayloadState::empty;
                return {ready?&slot.value:nullptr,ready?PayloadStatus::built:PayloadStatus::rejected};
            } catch(...) {slot.state=PayloadState::empty;throw;}
        }
        const Plan& plan()const {
            if(!owner_)throw Error("empty temporal Plan lease");return owner_->rows_[index_].plan;
        }
    };
    enum class Status {hit,built,unavailable,rejected};
    struct Result {Lease lease;Status status=Status::unavailable;};
    ScheduledTemporalPlanCacheT()=default;
    ScheduledTemporalPlanCacheT(const ScheduledTemporalPlanCacheT&)=delete;
    ScheduledTemporalPlanCacheT& operator=(const ScheduledTemporalPlanCacheT&)=delete;
    ScheduledTemporalPlanCacheT(ScheduledTemporalPlanCacheT&&)=delete;
    ScheduledTemporalPlanCacheT& operator=(ScheduledTemporalPlanCacheT&&)=delete;
    ~ScheduledTemporalPlanCacheT(){if(pinned())std::terminate();}
    bool pinned()const noexcept {for(const auto& row:rows_)if(row.pins || row.state==State::building)return true;return false;}
    std::size_t ready_entries()const noexcept {
        std::size_t n=0;for(const auto& row:rows_)n+=row.state==State::ready;return n;
    }
    template<class Payload> static constexpr std::size_t payload_charge() {
        static_assert(std::is_trivially_copyable_v<Payload> && std::is_trivially_destructible_v<Payload>,
            "payload must be bounded self-contained numerical storage");
        return sizeof(PayloadArena<Payload>);
    }
    template<class Payload> bool allocate_payload() {
        (void)payload_charge<Payload>();
        if(pinned())return false;
        if(payload_)return payload_arena<Payload>()!=nullptr; // no type replacement
        payload_=std::make_unique<PayloadArena<Payload>>();return true;
    }
    std::size_t payload_bytes()const noexcept{return payload_?payload_->bytes():0;}
    bool try_drop_payload()noexcept {
        if(pinned())return false;payload_.reset();return true;
    }
    bool try_clear()noexcept {
        if(pinned())return false;
        for(std::size_t index=0;index<Entries;++index) {
            if(payload_)payload_->invalidate(index);rows_[index].state=State::empty;
        }
        return true;
    }
    // Builder is trusted ORIGINAL Plan::build_original, invoked on the row's
    // owned exact key snapshot: bool(Plan&,span<const double>,N,C,stop_token).
    // It must initialize Plan fully, never mutate any other cache row or emit
    // detector/crypto/physical state. There is NO Plan copy/temporary here.
    template<class Builder>
    Result acquire(std::span<const double> coordinates,std::uint64_t symbol,std::uint64_t chip,
                   Builder&& build,std::stop_token stop={}) {
        ::datapump::detail::cpu_profile::Scope cpu_lookup(::datapump::detail::cpu_profile::Stage::temporal_lookup,true);
        ++statistics.requests;
        if(stop.stop_requested()){++statistics.cancelled;cancelled(stop);}
        if(coordinates.empty() || coordinates.size()>MaximumSamples || !symbol || !chip) {
            ++statistics.rejected;return {{},Status::rejected};
        }
        std::uint64_t hash=0x9e3779b97f4a7c15ULL^symbol^std::rotl(chip,19)^coordinates.size();
        for(std::size_t i=0;i<coordinates.size();++i) {
            if((i&63U)==0 && stop.stop_requested()){++statistics.cancelled;cancelled(stop);}
            if(!std::isfinite(coordinates[i])){++statistics.rejected;return {{},Status::rejected};}
            hash^=std::bit_cast<std::uint64_t>(coordinates[i])+0x9e3779b97f4a7c15ULL+(hash<<6)+(hash>>2);
            ++statistics.hash_words;
        }
        std::size_t victim=Entries;bool busy_equal=false;
        for(std::size_t index=0;index<Entries;++index) {
            if((index&31U)==0 && stop.stop_requested()){++statistics.cancelled;cancelled(stop);}
            auto& row=rows_[index];
            bool equal=row.state!=State::empty && row.symbol==symbol && row.chip==chip &&
                row.count==coordinates.size() && row.fingerprint==hash;
            for(std::size_t i=0;equal && i<coordinates.size();++i) {
                if((i&63U)==0 && stop.stop_requested()){++statistics.cancelled;cancelled(stop);}
                ++statistics.compared_words;
                equal=std::bit_cast<std::uint64_t>(row.coordinates[i])==std::bit_cast<std::uint64_t>(coordinates[i]);
            }
            if(equal && row.state==State::ready) {
                if(row.pins==std::numeric_limits<std::uint32_t>::max()) {
                    ++statistics.unavailable;return {{},Status::unavailable};
                }
                if(stop.stop_requested()){++statistics.cancelled;cancelled(stop);}
                ++row.pins;row.stamp=tick();++statistics.hits;return {Lease(this,index),Status::hit};
            }
            busy_equal|=equal && row.state==State::building;
            if(row.pins || row.state==State::building)continue;
            if(victim==Entries || row.state==State::empty ||
               (rows_[victim].state!=State::empty && row.stamp<rows_[victim].stamp))victim=index;
        }
        // No blocking/waiting and no evicting active leases/builders. Caller
        // can use its original owned1Plan fallback for unavailable only.
        if(busy_equal || victim==Entries){++statistics.unavailable;return {{},Status::unavailable};}
        // No pin exists for victim. Clear the paired precision generation
        // BEFORE the original row enters BUILDING or any Plan/key byte changes.
        if(payload_)payload_->invalidate(victim);
        auto& row=rows_[victim];if(row.state==State::ready)++statistics.evictions;
        ++statistics.misses;row.state=State::building;row.pins=0;
        row.symbol=symbol;row.chip=chip;row.count=coordinates.size();row.fingerprint=hash;
        for(std::size_t i=0;i<coordinates.size();++i)row.coordinates[i]=coordinates[i];
        try {
            if(!build(row.plan,std::span<const double>{row.coordinates.data(),row.count},symbol,chip,stop)) {
                row.state=State::empty;++statistics.rejected;return {{},Status::rejected};
            }
            if(stop.stop_requested())cancelled(stop);
            bool structurally_valid=false;
            {
                ::datapump::detail::cpu_profile::Scope cpu_validate(::datapump::detail::cpu_profile::Stage::temporal_validate,true);
                structurally_valid=row.plan.well_formed() && row.plan.samples==row.count &&
                    row.plan.symbol_samples==symbol && row.plan.chip_samples==chip;
            }
            if(!structurally_valid) {
                row.state=State::empty;++statistics.rejected;return {{},Status::rejected};
            }
            if(stop.stop_requested())cancelled(stop);
            row.state=State::ready;row.pins=1;row.stamp=tick();++statistics.builds;
            return {Lease(this,victim),Status::built};
        } catch(...) {
            row.state=State::empty;row.pins=0;
            if(stop.stop_requested())++statistics.cancelled;else ++statistics.exceptions;
            throw;
        }
    }
};
}
