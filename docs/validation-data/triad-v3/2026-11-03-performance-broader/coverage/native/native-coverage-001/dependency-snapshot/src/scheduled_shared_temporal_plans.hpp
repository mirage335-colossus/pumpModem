#pragma once
#include "scheduled_temporal_templates.hpp"
#include <array>
#include <exception>
#include <limits>
#include <memory>
#include <utility>

namespace datapump::modem::detail {
// Optional Live-owned PUBLIC Plan shards. Sequential receiver pushes may reuse
// slot i; within a push the existing joined parallel_search contract exclusively
// assigns worker i. This is NOT a concurrently shared cache or global singleton.
template<class Cache,std::size_t MaximumWorkers=64>
class ScheduledSharedTemporalPlansT {
    static_assert(MaximumWorkers>0 && MaximumWorkers<=64);
    std::array<std::unique_ptr<Cache>,MaximumWorkers> shards_{};
    std::size_t workers_=0;
    bool borrowed_=false; // touched only at joined coordinator boundaries
    explicit ScheduledSharedTemporalPlansT(std::size_t workers):workers_(workers) {
        for(std::size_t i=0;i<workers_;++i)shards_[i]=std::make_unique<Cache>();
    }
public:
    static constexpr std::size_t maximum_workers=MaximumWorkers;
    static constexpr std::size_t shard_charge=sizeof(Cache)+Cache::lookup_callee_allowance;
    class Borrow {
        friend class ScheduledSharedTemporalPlansT;
        ScheduledSharedTemporalPlansT* owner_=nullptr;std::size_t workers_=0;
        Borrow(ScheduledSharedTemporalPlansT* owner,std::size_t workers):owner_(owner),workers_(workers){}
    public:
        Borrow()=default;Borrow(const Borrow&)=delete;Borrow& operator=(const Borrow&)=delete;
        Borrow(Borrow&& other)noexcept:owner_(std::exchange(other.owner_,nullptr)),workers_(other.workers_){}
        Borrow& operator=(Borrow&& other)noexcept {
            if(this!=&other){reset();owner_=std::exchange(other.owner_,nullptr);workers_=other.workers_;}return *this;
        }
        ~Borrow(){reset();}
        explicit operator bool()const noexcept{return owner_!=nullptr;}
        std::size_t workers()const noexcept{return owner_?workers_:0;}
        Cache* slot(std::size_t worker)const noexcept {
            return owner_ && worker<workers_?owner_->shards_[worker].get():nullptr;
        }
        void reset()noexcept {
            if(!owner_)return;
            // Every feed has joined and unbound its Temporal objects. Numeric
            // output detaches after prepare; outer guards can retain the row
            // through raw consumption, but must release it before this barrier.
            for(std::size_t i=0;i<owner_->workers_;++i)if(owner_->shards_[i]->pinned())std::terminate();
            owner_->borrowed_=false;owner_=nullptr;workers_=0;
        }
    };
    ScheduledSharedTemporalPlansT(const ScheduledSharedTemporalPlansT&)=delete;
    ScheduledSharedTemporalPlansT& operator=(const ScheduledSharedTemporalPlansT&)=delete;
    ~ScheduledSharedTemporalPlansT(){if(!idle())std::terminate();}
    static std::size_t charge_for(std::size_t workers)noexcept {
        constexpr auto maximum=std::numeric_limits<std::size_t>::max();
        if(!workers || workers>MaximumWorkers || workers>(maximum-sizeof(ScheduledSharedTemporalPlansT))/shard_charge)return maximum;
        return sizeof(ScheduledSharedTemporalPlansT)+workers*shard_charge;
    }
    // Phase 2 only: caller has first queried every receiver, retained all
    // original workers/caches, and granted this exact additive reservation.
    // No owner/cache allocation precedes the grant. bad_alloc rolls back all
    // local allocations; caller publishes pointer+charge together afterwards.
    static std::unique_ptr<ScheduledSharedTemporalPlansT> create(std::size_t workers,std::size_t allowance) {
        const auto charge=charge_for(workers);
        if(charge==std::numeric_limits<std::size_t>::max() || charge>allowance)return {};
        return std::unique_ptr<ScheduledSharedTemporalPlansT>(new ScheduledSharedTemporalPlansT(workers));
    }
    std::size_t workers()const noexcept{return workers_;}
    std::size_t payload_bytes()const noexcept {
        std::size_t result=0;for(std::size_t i=0;i<workers_;++i)result+=shards_[i]->payload_bytes();return result;
    }
    std::size_t working_bytes()const noexcept{return charge_for(workers_)+payload_bytes();}
    template<class Payload> static std::size_t payload_charge_for(std::size_t workers)noexcept {
        constexpr auto per=Cache::template payload_charge<Payload>();
        constexpr auto maximum=std::numeric_limits<std::size_t>::max();
        return workers && workers<=MaximumWorkers && workers<=maximum/per?workers*per:maximum;
    }
    // Independent optional phase AFTER the original exact owner grant. No
    // worker allocation: each numerical producer merely uses its shard slot.
    template<class Payload> bool allocate_payloads(std::size_t allowance) {
        if(!idle())return false;
        constexpr auto per=Cache::template payload_charge<Payload>();std::size_t extra=0;
        for(std::size_t i=0;i<workers_;++i)if(!shards_[i]->payload_bytes()) {
            if(per>allowance-extra)return false;extra+=per;
        }
        std::array<bool,MaximumWorkers> created{};
        try {
            for(std::size_t i=0;i<workers_;++i) {
                const auto before=shards_[i]->payload_bytes();
                if(!shards_[i]->template allocate_payload<Payload>()) {
                    for(std::size_t j=0;j<workers_;++j)if(created[j])shards_[j]->try_drop_payload();return false;
                }
                created[i]=!before && shards_[i]->payload_bytes();
            }
        } catch(...) {
            for(std::size_t i=0;i<workers_;++i)if(created[i])shards_[i]->try_drop_payload();throw;
        }
        return true;
    }
    // Pressure-only tier: preserve every exact public row/worker. Existing
    // cold-profile/full-owner eviction policy is deliberately unchanged.
    bool try_drop_payloads()noexcept {
        if(!idle())return false;
        for(std::size_t i=0;i<workers_;++i)if(!shards_[i]->try_drop_payload())std::terminate();
        return true;
    }
    bool idle()const noexcept {
        if(borrowed_)return false;
        for(std::size_t i=0;i<workers_;++i)if(shards_[i]->pinned())return false;
        return true;
    }
    Borrow try_borrow(std::size_t workers)noexcept {
        if(!workers || workers>workers_ || !idle())return {};
        borrowed_=true;return Borrow(this,workers);
    }
    typename Cache::Statistics statistics()const noexcept {
        // Only inspect at a joined coordinator boundary.
        typename Cache::Statistics result;
        for(std::size_t i=0;i<workers_;++i)result+=shards_[i]->statistics;
        return result;
    }
};
using SharedTemporalPlans=ScheduledSharedTemporalPlansT<ScheduledTemporalTemplates::SharedPlanCache>;
} // namespace datapump::modem::detail
