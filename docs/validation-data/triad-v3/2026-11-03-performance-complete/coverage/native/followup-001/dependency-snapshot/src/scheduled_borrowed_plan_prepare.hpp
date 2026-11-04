#pragma once
#include "scheduled_temporal_plan_cache.hpp"
#include <type_traits>
namespace datapump::modem::detail {
// Integration seam for shared_semantics BorrowedTile. Caller retains its ORIGINAL
// physical row/selected mapping and PRIVATE binding; only public Plan is shared.
struct ScheduledBorrowedPlanResult {
    bool ready=false,pooled=false,plan_reused=false,fallback_rebuilt=false;
};
template<class Tile,class Cache>
ScheduledBorrowedPlanResult scheduled_prepare_borrowed_plan(
        Tile& tile,Cache* optional_pool,typename Tile::Plan& owned_fallback,
        bool fallback_key_equal,std::span<const double> exact_key_coordinates,
        std::uint64_t symbol,std::uint64_t chip,std::uint64_t local_translation,
        typename Tile::Lane private_binding,typename Tile::Statistics& numeric,
        std::stop_token stop={},typename Cache::Lease* retained=nullptr) {
    static_assert(std::is_same_v<typename Cache::PlanType,typename Tile::Plan>);
    constexpr auto backend=[] {
        if constexpr(requires { Tile::Backend::automatic; })return Tile::Backend::automatic;
        else return Tile::Backend::scalar;
    }();
    ScheduledBorrowedPlanResult result;tile.reset_binding();tile.statistics={};
    // Preserve old transient behavior by default. A retained owner is always
    // cleared AFTER numeric detachment, before acquisition can evict its row.
    if(retained)retained->reset();
    if(!Tile::arithmetic_supported())return result;
    typename Cache::Result acquired;
    if(optional_pool) {
        acquired=optional_pool->acquire(exact_key_coordinates,symbol,chip,
            [&](auto& plan,auto key,std::uint64_t n,std::uint64_t c,std::stop_token cancellation) {
                return plan.build_original(key,n,c,numeric,cancellation);
            },stop);
        // Exact builder rejection (e.g. coefficient union capacity) already
        // proves this SAME key unsupported. Do not build it twice; caller's
        // ORIGINAL raw fallback remains, with no detector mutation.
        if(acquired.status==Cache::Status::rejected)return result;
    }
    const auto* plan=acquired.lease?&acquired.lease.plan():&owned_fallback;
    result.pooled=bool(acquired.lease);
    result.plan_reused=acquired.lease?acquired.status==Cache::Status::hit:fallback_key_equal;
    if(!acquired.lease && !fallback_key_equal) {
        if(!owned_fallback.build_original(exact_key_coordinates,symbol,chip,numeric,stop))return result;
        result.fallback_rebuilt=true;
    }
    if(local_translation>std::numeric_limits<std::uint64_t>::max()-plan->first_local)return result;
    private_binding.first_local=plan->first_local+local_translation;
    // Guard declared AFTER acquired: every failure/exception clears Tile's
    // borrowed pointer BEFORE its local Lease unpins. Success preserves only
    // output/scalar sample metadata, then also detaches before Lease release.
    struct BindingGuard {
        Tile& tile;bool preserve_output=false;
        ~BindingGuard()noexcept {
            if(preserve_output)tile.detach_evaluated_plan();else tile.reset_binding();
        }
    } clear{tile};
    if(acquired.lease) {
        if(retained)
            result.ready=tile.template gather_evaluate_pinned<Cache::maximum_samples,Cache::capacity>(
                {&private_binding,1},acquired.lease,backend,stop);
        else
            result.ready=tile.template gather_evaluate_leased<Cache::maximum_samples,Cache::capacity>(
                {&private_binding,1},std::move(acquired.lease),backend,stop);
    } else
        result.ready=tile.gather_borrowed({&private_binding,1},*plan,stop) &&
            tile.evaluate(backend,stop);
    clear.preserve_output=result.ready;
    // Pinned execution has already detached its numeric pointer. Move only
    // after complete success; failed/exceptional acquisitions unpin locally.
    if(result.ready && retained && acquired.lease)*retained=std::move(acquired.lease);
    return result;
}
}
