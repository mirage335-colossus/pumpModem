#include "scheduled_borrowed_plan_prepare.hpp"
#include <stdexcept>
#include <iostream>
using namespace datapump::modem::detail;
static void check(bool ok){if(!ok)throw std::runtime_error("borrowed Plan lifetime oracle failure");}
struct MockTile {
    struct Statistics {unsigned builds=0;};
    struct Plan {
        std::uint64_t symbol_samples=0,chip_samples=0,first_local=0;
        std::size_t samples=0;bool valid=false;
        bool build_original(std::span<const double> key,std::uint64_t n,std::uint64_t c,
                            Statistics& statistics,std::stop_token) {
            ++statistics.builds;symbol_samples=n;chip_samples=c;samples=key.size();first_local=9;
            valid=!key.empty()&&key[0]>=0;return valid;
        }
        bool well_formed()const{return valid&&samples;}
    };
    struct Lane {std::uint64_t first_local=0;};
    enum class Backend {scalar};
    Statistics statistics;
    const Plan* borrowed=nullptr;bool evaluated=false,fail=false,throw_evaluate=false;
    unsigned outputs=0;std::size_t evaluated_samples=0;
    static bool arithmetic_supported(){return true;}
    void reset_binding()noexcept{borrowed=nullptr;evaluated=false;evaluated_samples=0;}
    void detach_evaluated_plan()noexcept{borrowed=nullptr;}
    bool gather_borrowed(std::span<const Lane> lanes,const Plan& plan,std::stop_token) {
        check(lanes.size()==1&&plan.valid);borrowed=&plan;return true;
    }
    template<std::size_t Samples,std::size_t Entries>
    bool gather_evaluate_leased(std::span<const Lane> lanes,
            typename ScheduledTemporalPlanCacheT<Plan,Samples,Entries>::Lease lease,
            Backend backend,std::stop_token stop={}) {
        return gather_evaluate_pinned<Samples,Entries>(lanes,lease,backend,stop);
    }
    template<std::size_t Samples,std::size_t Entries>
    bool gather_evaluate_pinned(std::span<const Lane> lanes,
            const typename ScheduledTemporalPlanCacheT<Plan,Samples,Entries>::Lease& lease,
            Backend backend,std::stop_token stop={}) {
        reset_binding();const auto* plan=lease.validated_plan();if(!plan)return false;
        struct Guard {MockTile& tile;bool success=false;~Guard(){if(success)tile.detach_evaluated_plan();else tile.reset_binding();}} clear{*this};
        clear.success=gather_borrowed(lanes,*plan,stop)&&evaluate(backend,stop);return clear.success;
    }
    bool evaluate(Backend,std::stop_token) {
        check(borrowed);
        if(throw_evaluate)throw std::runtime_error("injected evaluation failure");
        if(fail)return false;
        evaluated=true;evaluated_samples=borrowed->samples;outputs=42;return true;
    }
};
int main(){
    using Cache=ScheduledTemporalPlanCacheT<MockTile::Plan,8,4>;
    Cache cache;MockTile tile;MockTile::Plan fallback;MockTile::Statistics numeric;
    const std::array<double,4> key{135.25,136.25,137.25,138.25};
    const auto prepare=[&](Cache* pool,bool equal){return scheduled_prepare_borrowed_plan(tile,pool,
        fallback,equal,key,96000,15,19,MockTile::Lane{},numeric);};
    auto result=prepare(&cache,false);
    check(result.ready&&result.pooled&&!result.plan_reused&&!tile.borrowed&&!cache.pinned());
    check(tile.evaluated&&tile.outputs==42&&tile.evaluated_samples==4);
    check(cache.try_clear()); // Detached source outputs remain valid after public row eviction.
    check(tile.evaluated&&tile.outputs==42&&tile.evaluated_samples==4);
    result=prepare(&cache,false);result=prepare(&cache,false);
    check(result.ready&&result.plan_reused&&!tile.borrowed&&!cache.pinned());
    tile.fail=true;result=prepare(&cache,false);
    check(!result.ready&&!tile.borrowed&&!cache.pinned()&&!tile.evaluated);tile.fail=false;
    tile.throw_evaluate=true;bool threw=false;
    try{prepare(&cache,false);}catch(const std::runtime_error&){threw=true;}
    check(threw&&!tile.borrowed&&!cache.pinned());tile.throw_evaluate=false;
    result=prepare(nullptr,false);check(result.ready&&result.fallback_rebuilt&&!result.pooled);
    result=prepare(nullptr,true);check(result.ready&&result.plan_reused&&!result.fallback_rebuilt);
    // Retaining the same validated immutable row changes only pin lifetime.
    Cache::Lease retained;
    const auto pinned_prepare=[&] {return scheduled_prepare_borrowed_plan(tile,&cache,
        fallback,false,key,96000,15,19,MockTile::Lane{},numeric,{},&retained);};
    result=pinned_prepare();check(result.ready&&retained&&cache.pinned()&&!tile.borrowed);
    check(!cache.try_clear()&&tile.evaluated&&tile.outputs==42);retained.reset();check(cache.try_clear());
    tile.fail=true;result=pinned_prepare();check(!result.ready&&!retained&&!cache.pinned()&&!tile.borrowed);tile.fail=false;
    tile.throw_evaluate=true;threw=false;
    try{pinned_prepare();}catch(const std::runtime_error&){threw=true;}
    check(threw&&!retained&&!cache.pinned()&&!tile.borrowed);tile.throw_evaluate=false;
    result=pinned_prepare();check(result.ready&&retained);
    result=scheduled_prepare_borrowed_plan(tile,static_cast<Cache*>(nullptr),fallback,true,
        key,96000,15,19,MockTile::Lane{},numeric,{},&retained);
    check(result.ready&&!retained&&!cache.pinned()&&!tile.borrowed);
    // A rejected public build does not gather, double-build or retain old stats/binding.
    const std::array<double,4> rejected{-1,0,1,2};tile.statistics.builds=999;
    result=scheduled_prepare_borrowed_plan(tile,&cache,fallback,false,rejected,
        96000,15,0,MockTile::Lane{},numeric);
    check(!result.ready&&!tile.borrowed&&!cache.pinned()&&tile.statistics.builds==0);
    std::cout<<"borrowed Plan success/failure/exception detach and fallback passed\n";
}
