#pragma once
#include "scheduled_fp32_pulse_tile.hpp"
#include "scheduled_temporal_plan_cache.hpp"
#include <cstdlib>
#include <string_view>

namespace datapump::modem::detail {
// Optional per-Temporal heap payload. The existing exact public Plan helper,
// original private gather, physical exporter and every wider consumer stay put.
class ScheduledTemporalFp32Execution {
public:
    using Tile=ScheduledOrderedPulseCandidatesT<1,512,96,false>;
    using Native=ScheduledFp32PulseTile<512,96>;
    using Plan=Tile::Plan;using Lane=Tile::Lane;using Backend=Tile::Backend;
    using Statistics=Tile::Statistics;
    struct Counters {
        std::uint64_t attempts=0,native_tiles=0,refined_tiles=0,refined_rows=0;
        std::uint64_t consumed_native=0,consumed_refined=0,packed_hits=0,packed_misses=0;
        std::uint64_t entry_hits=0,entry_builds=0,entry_rejected=0,entry_busy=0,entry_unavailable=0;
        Native::Statistics numeric;
    };
    Statistics statistics;
    explicit ScheduledTemporalFp32Execution(Tile& original,double limit=1e-5)
        :original_(original),limit_(limit){}
    static bool arithmetic_supported(){return Native::supported();}
    static bool payload_requested() {
        static const bool enabled=[] {
            const auto* packed=std::getenv("DATAPUMP_SCHED_TEMPORAL_FP32_PACKED_CACHE");
            const auto* native=std::getenv("DATAPUMP_SCHED_TEMPORAL_FP32");
            return packed && std::string_view(packed)=="1" && native &&
                (std::string_view(native)=="1" || std::string_view(native)=="refine");
        }();return enabled;
    }
    bool local_payload_attempted()const noexcept{return local_payload_attempted_;}
    void mark_local_payload_attempted()noexcept{local_payload_attempted_=true;}
    void begin_request(std::span<const double> coordinates,std::uint64_t symbol,std::uint64_t chip) {
        reset_request();requested_=coordinates;requested_symbol_=symbol;requested_chip_=chip;
        original_.statistics={};statistics={};
    }
    void end_request()noexcept {requested_={};}
    void reset_request()noexcept {reset_binding();attempted_=false;requested_={};}
    // The helper's failure guard may reset a binding after an attempted native
    // prepare; counters/attempt disposition survive, borrowed Plan pointers do not.
    void reset_binding()noexcept {
        original_.reset_binding();native_.reset();pending_=nullptr;active_packed_=nullptr;route_=Route::none;prepared_native_=false;
    }
    void detach_evaluated_plan()noexcept {
        pending_=nullptr;active_packed_=nullptr;
        if(route_==Route::native){native_.detach_evaluated_plan();original_.reset_binding();}
        else if(route_==Route::refined){original_.detach_evaluated_plan();native_.reset();}
        else reset_binding();
    }
    bool gather_borrowed(std::span<const Lane> lanes,const Plan& plan,std::stop_token stop={}) {
        return gather_impl(lanes,plan,false,stop,[]{return PackedEntry{};});
    }
    template<std::size_t PlanSamples,std::size_t Entries>
    bool gather_evaluate_leased(std::span<const Lane> lanes,
        typename ScheduledTemporalPlanCacheT<Plan,PlanSamples,Entries>::Lease lease,
        Backend backend,std::stop_token stop={}) {
        return gather_evaluate_pinned<PlanSamples,Entries>(lanes,lease,backend,stop);
    }
    // The caller may retain this ORIGINAL FP64 public row for outer guards.
    // Native/FP64 numeric execution remains detached before this returns.
    template<std::size_t PlanSamples,std::size_t Entries>
    bool gather_evaluate_pinned(std::span<const Lane> lanes,
        const typename ScheduledTemporalPlanCacheT<Plan,PlanSamples,Entries>::Lease& lease,
        Backend backend,std::stop_token stop={}) {
        reset_binding();const auto* plan=lease.validated_plan();if(!plan)return false;
        // Caller holds the immutable pin throughout this synchronous call.
        // No original/native borrowed pointer survives return or exception;
        // later public outer callbacks may retain the caller's pin.
        struct Guard {
            ScheduledTemporalFp32Execution& owner;bool success=false;
            ~Guard()noexcept {if(success)owner.detach_evaluated_plan();else owner.reset_binding();}
        } clear{*this};
        using Cache=ScheduledTemporalPlanCacheT<Plan,PlanSamples,Entries>;
        const auto entry=[&] {
            const auto result=lease.template get_or_build_payload<typename Native::PackedPlan>(
                [&](auto& packed,const Plan& exact,std::stop_token cancellation) {
                    return packed.pack(exact,native_.statistics,cancellation);
                },stop);
            switch(result.status) {
                case Cache::PayloadStatus::hit:++counters_.entry_hits;break;
                case Cache::PayloadStatus::built:++counters_.entry_builds;break;
                case Cache::PayloadStatus::rejected:++counters_.entry_rejected;break;
                case Cache::PayloadStatus::busy:++counters_.entry_busy;break;
                case Cache::PayloadStatus::unavailable:++counters_.entry_unavailable;break;
            }
            return PackedEntry{result.value,result.status==Cache::PayloadStatus::hit,
                               result.status!=Cache::PayloadStatus::unavailable};
        };
        clear.success=gather_impl(lanes,*plan,true,stop,entry) && evaluate(backend,stop);
        return clear.success;
    }
    bool evaluate(Backend backend,std::stop_token stop={}) {
        if(!pending_)return false;
        const auto samples=pending_->samples;
        if(prepared_native_) {
            ::datapump::detail::cpu_profile::Scope cpu_native_evaluate(::datapump::detail::cpu_profile::Stage::temporal_evaluate,true);
            if(native_.evaluate(stop)) {
                route_=Route::native;++counters_.native_tiles;
                statistics.output_pairs+=samples;statistics.shape_terms+=active_packed_->shape_terms;return true;
            }
        }
        // Same gathered original doubles and still-pinned original Plan. There
        // is no second private values()/prepare_symbol pass or hidden FP64 loop
        // on an accepted native tile. All original rounding stays in evaluate.
        const bool ready=original_.evaluate(backend,stop);statistics=original_.statistics;
        if(ready){route_=Route::refined;++counters_.refined_tiles;counters_.refined_rows+=samples;}
        return ready;
    }
    bool raw(std::size_t sample,std::array<std::complex<double>,2>& value) {
        if(route_==Route::native) {
            if(!native_.raw(sample,value))return false;
            ++counters_.consumed_native;return true;
        }
        if(route_==Route::refined){value=original_.raw(sample,0);++counters_.consumed_refined;return true;}
        return false;
    }
    bool attempted()const noexcept{return attempted_;}
    Counters counters()const {auto result=counters_;result.numeric=native_.statistics;return result;}
private:
    enum class Route {none,native,refined};
    struct PackedEntry {const typename Native::PackedPlan* value=nullptr;bool hit=false,available=false;};
    bool same_key()const {
        if(!packed_.valid || requested_.size()!=cached_count_ || requested_symbol_!=cached_symbol_ ||
           requested_chip_!=cached_chip_)return false;
        for(std::size_t i=0;i<cached_count_;++i)
            if(std::bit_cast<std::uint64_t>(requested_[i])!=std::bit_cast<std::uint64_t>(cached_coordinates_[i]))return false;
        return true;
    }
    template<class EntryLookup>
    bool gather_impl(std::span<const Lane> lanes,const Plan& plan,bool validated,std::stop_token stop,EntryLookup&& lookup) {
        reset_binding();attempted_=true;++counters_.attempts;
        // INTERNAL helper precondition: Plan is the original builder's result
        // for this exact ordered key; arbitrary external key/Plan pairs are not
        // a cache certificate. Cheap geometry guards retain that association.
        if(lanes.size()!=1 || requested_.size()!=plan.samples || requested_.empty() ||
           requested_symbol_!=plan.symbol_samples || requested_chip_!=plan.chip_samples)return false;
        // This exact existing gather is the sole private source lookup. The
        // prevalidated branch is reachable ONLY under a validated live Lease.
        const bool gathered=original_.gather_impl(lanes,plan,true,stop,validated);
        statistics=original_.statistics;if(!gathered)return false;
        pending_=&plan;
        ::datapump::detail::cpu_profile::Scope cpu_native_prepare(::datapump::detail::cpu_profile::Stage::temporal_gather,true);
        // A live original Lease proves this paired payload's exact owner,
        // row and generation. No secondary key copy/hash/memcmp on this route.
        const auto entry=lookup();
        if(entry.value) {
            active_packed_=entry.value;
            if(entry.hit)++counters_.packed_hits;else ++counters_.packed_misses;
        } else if(!entry.available) {
            // Absent optional arena/owned fallback retains preceding behavior.
            bool packed=same_key();
            if(packed)++counters_.packed_hits;
            else {
                ++counters_.packed_misses;packed=packed_.pack(plan,native_.statistics,stop);
                if(packed) {
                    cached_count_=requested_.size();cached_symbol_=requested_symbol_;cached_chip_=requested_chip_;
                    for(std::size_t i=0;i<cached_count_;++i)cached_coordinates_[i]=requested_[i];
                }
            }
            if(packed)active_packed_=&packed_;
        }
        if(active_packed_) {
            for(std::size_t p=0;p<plan.coefficients;++p)if(plan.active_taps.test(p))
                for(unsigned component=0;component<4;++component)
                    coefficient_[p][component]=original_.coefficient_[component][p][0];
            prepared_native_=native_.prepare(*active_packed_,coefficient_,{limit_},stop);
        }
        return true; // local rejection refines the ORIGINAL gathered output
    }
    Tile& original_;double limit_;
    typename Native::PackedPlan packed_;
    typename Native::CoefficientInput coefficient_{};
    Native native_;
    Counters counters_;
    std::array<double,512> cached_coordinates_{};
    std::span<const double> requested_;
    std::size_t cached_count_=0;
    std::uint64_t requested_symbol_=0,requested_chip_=0,cached_symbol_=0,cached_chip_=0;
    const Plan* pending_=nullptr;
    const typename Native::PackedPlan* active_packed_=nullptr;
    Route route_=Route::none;
    bool prepared_native_=false,attempted_=false,local_payload_attempted_=false;
};
} // namespace datapump::modem::detail
