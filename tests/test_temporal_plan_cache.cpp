#include "scheduled_temporal_plan_cache.hpp"
#include "scheduled_ordered_pulse_candidates.hpp"
#include "scheduled_temporal_templates.hpp"
#include <memory>
#include <iostream>
#include <stdexcept>
#include <type_traits>
using namespace datapump::modem;
using namespace datapump::modem::detail;
static void check(bool x){if(!x)throw std::runtime_error("public temporal Plan cache oracle failure");}
struct TinyPlan {
    std::uint64_t symbol_samples=0,chip_samples=0,first_local=0,marker=0;
    std::size_t samples=0;bool valid=false;
    bool well_formed()const{return valid&&samples&&symbol_samples&&chip_samples;}
};
using TinyCache=ScheduledTemporalPlanCacheT<TinyPlan,8,4>;
static_assert(!std::is_copy_constructible_v<TinyCache> && !std::is_move_constructible_v<TinyCache>);
static_assert(!std::is_copy_constructible_v<TinyCache::Lease> && std::is_move_constructible_v<TinyCache::Lease>);
static void large_pinned_rows() {
 using Large=ScheduledTemporalPlanCacheT<TinyPlan,8,128>;
 auto cache=std::make_unique<Large>();std::array<Large::Lease,128> leases;
 const auto build=[](TinyPlan& p,std::span<const double> x,std::uint64_t n,std::uint64_t c,std::stop_token){p.symbol_samples=n;p.chip_samples=c;p.samples=x.size();p.valid=true;return true;};
 for(std::size_t i=0;i<leases.size();++i){const std::array<double,1>x{double(i)};auto r=cache->acquire(x,96000,15,build);check(r.status==Large::Status::built);leases[i]=std::move(r.lease);}
 const std::array<double,1> next{129.};auto blocked=cache->acquire(next,96000,15,build);
 check(!blocked.lease && blocked.status==Large::Status::unavailable && cache->ready_entries()==128);
 leases[127].reset();auto replaced=cache->acquire(next,96000,15,build);check(bool(replaced.lease));
 for(auto& lease:leases)lease.reset();replaced.lease.reset();check(cache->try_clear());
 std::stop_source stopped;stopped.request_stop();bool caught=false;
 try{cache->acquire(next,96000,15,build,stopped.get_token());}catch(const datapump::Error&){caught=true;}
 check(caught && !cache->pinned());
}
int main(){
 large_pinned_rows();
 TinyCache cache;unsigned builds=0;
 const auto builder=[&](TinyPlan& p,std::span<const double> x,std::uint64_t n,std::uint64_t c,std::stop_token){
  p.symbol_samples=n;p.chip_samples=c;p.samples=x.size();p.marker=++builds;p.valid=true;return true;};
 const std::array<double,4> a{135.5,136.5,137.5,138.5},b{136.,137.,138.,139.},
  c{137.,138.,139.,140.},d{138.,139.,140.,141.},e{139.,140.,141.,142.};
 auto A=cache.acquire(a,96000,15,builder);auto address=&A.lease.plan();
 auto duplicate=cache.acquire(a,96000,15,builder);check(duplicate.status==TinyCache::Status::hit && &duplicate.lease.plan()==address&&builds==1);
 // Independent physical scatter/selected masks never enter the public key.
 auto B=cache.acquire(b,96000,15,builder),C=cache.acquire(c,96000,15,builder),D=cache.acquire(d,96000,15,builder);
 check(cache.ready_entries()==4&&cache.pinned()&&!cache.try_clear());
 auto blocked=cache.acquire(e,96000,15,builder);check(!blocked.lease&&blocked.status==TinyCache::Status::unavailable&&builds==4);
 A.lease.reset();check(cache.pinned());duplicate.lease.reset();
 auto E=cache.acquire(e,96000,15,builder);check(E.status==TinyCache::Status::built&&builds==5);
 check(B.lease.plan().marker==2&&C.lease.plan().marker==3&&D.lease.plan().marker==4);
 auto moved=std::move(B.lease);check(!B.lease&&bool(moved));moved.reset();C.lease.reset();D.lease.reset();E.lease.reset();
 check(!cache.pinned()&&cache.try_clear()&&cache.ready_entries()==0);
 auto rejected=cache.acquire(a,96000,15,[](auto&,auto,std::uint64_t,std::uint64_t,std::stop_token){return false;});
 check(!rejected.lease&&rejected.status==TinyCache::Status::rejected&&cache.ready_entries()==0);
 std::stop_source cancelled;bool stopped=false;
 try {cache.acquire(a,96000,15,[&](TinyPlan& p,auto x,std::uint64_t n,std::uint64_t chip,std::stop_token){
  p={n,chip,0,99,x.size(),true};cancelled.request_stop();return true;},cancelled.get_token());}
 catch(const datapump::Error&){stopped=true;}
 check(stopped&&!cache.pinned()&&cache.ready_entries()==0);
 auto after_cancel=cache.acquire(a,96000,15,builder);check(after_cancel.status==TinyCache::Status::built);after_cancel.lease.reset();
 auto different=a;different[0]=std::nextafter(different[0],0.);
 auto bits=cache.acquire(different,96000,15,builder);check(bits.status==TinyCache::Status::built);bits.lease.reset();
 auto symbol=cache.acquire(a,96003,15,builder);check(symbol.status==TinyCache::Status::built);symbol.lease.reset();
 auto chip=cache.acquire(a,96000,10,builder);check(chip.status==TinyCache::Status::built);chip.lease.reset();
 // Real ORIGINAL Plan build, bounded object allocated in caller-owned heap.
 using Tile=ScheduledOrderedPulseCandidatesT<1,512,96,false>;using Plan=Tile::Plan;
 using Cache=ScheduledTemporalPlanCacheT<Plan,512,4>;
 auto pool=std::make_unique<Cache>();auto reference=std::make_unique<Plan>();Tile::Statistics numeric;
 std::array<double,64> coordinate{};for(std::size_t i=0;i<coordinate.size();++i)coordinate[i]=135.123456789+i*1.000003;
 const auto original=[&](Plan& p,auto x,std::uint64_t n,std::uint64_t c,std::stop_token stop){return p.build_original(x,n,c,numeric,stop);};
 auto plan=pool->acquire(coordinate,96000,15,original);check(bool(plan.lease));
 Tile::Statistics direct_numeric;check(reference->build_original(coordinate,96000,15,direct_numeric));
 check(plan.lease.plan().same_weights(*reference));const auto original_rows=numeric.weight_rows;
 auto hit=pool->acquire(coordinate,96000,15,original);check(hit.status==Cache::Status::hit&&numeric.weight_rows==original_rows);
 check(&hit.lease.plan()==&plan.lease.plan());hit.lease.reset();plan.lease.reset();check(!pool->pinned());
 // Original partial-tail/local-edge semantics stay in full key and builder.
 for(std::size_t i=0;i<coordinate.size();++i)coordinate[i]=95955.+i*.5;
 auto partial=pool->acquire(coordinate,96003,15,original);check(bool(partial.lease));partial.lease.reset();
 check(pool->try_clear());
 std::cout<<"Plan cache leases/exactkeys/originalrows passed;4rows="<<sizeof(Cache)
          <<" lookupframe="<<Cache::lookup_callee_allowance
          <<" Plan="<<sizeof(Plan)<<" Temporal="<<sizeof(ScheduledTemporalTemplates)
          <<" charge="<<ScheduledTemporalTemplates::plan_cache_charge
          <<"8rows="<<sizeof(ScheduledTemporalPlanCacheT<Plan,512,8>)<<'\n';
}
