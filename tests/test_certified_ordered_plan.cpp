#include "scheduled_ordered_pulse_candidates.hpp"
#include "scheduled_temporal_plan_cache.hpp"
#include <memory>
#include <stdexcept>
#include <iostream>
using namespace datapump;
using namespace datapump::modem;
using namespace datapump::modem::detail;
using Tile=ScheduledOrderedPulseCandidatesT<1,64,32,false>;
using Cache=ScheduledTemporalPlanCacheT<Tile::Plan,64,4>;
template<class T,class Argument>
concept accepts_certificate=requires(T& tile,Argument&& arg,std::span<const typename T::Lane> lanes){
 tile.template gather_evaluate_leased<64,4>(lanes,std::forward<Argument>(arg),T::Backend::scalar);
};
static_assert(accepts_certificate<Tile,Cache::Lease>);
static_assert(!accepts_certificate<Tile,Tile::Plan&>);
static_assert(!accepts_certificate<Tile,const Cache::Lease&>);
static_assert(!accepts_certificate<ScheduledOrderedPulseCandidatesT<1,64,32>,Cache::Lease>);
static void check(bool ok){if(!ok)throw std::runtime_error("certified Plan oracle failure");}
static bool bits(std::complex<double> a,std::complex<double> b){
 return std::bit_cast<std::uint64_t>(a.real())==std::bit_cast<std::uint64_t>(b.real())&&
        std::bit_cast<std::uint64_t>(a.imag())==std::bit_cast<std::uint64_t>(b.imag());
}
int main(){
 Config c;c.sample_rate=6000;c.carrier_hz=1500;c.bandwidth_hz=8;c.integration_seconds=16;
 c.pattern_symbols=true;c.constellation_bits=1;c.spreading_factor=64;c.scramble=c.dsss=true;c.dsss_factor=100;
 c.outer_dsss_version=OuterDsssVersion::interleaved_v2;c.data_key.emplace(Bytes(32,0x67));c.stream_epoch=1799999967;
 PatternCode code(c,c.stream_epoch),reference(c,c.stream_epoch);
 auto cache=std::make_unique<Cache>();auto tile=std::make_unique<Tile>();Tile::Statistics numeric;
 if(!Tile::arithmetic_supported()) {
  Tile::Lane unavailable{&code,&c,0,0,0};Tile::Plan untrusted;
  check(!tile->gather_evaluate_leased<64,4>({&unavailable,1},Cache::Lease{},Tile::Backend::scalar));
  check(!tile->gather_borrowed({&unavailable,1},untrusted));
  std::cout<<"native exact arithmetic unavailable; certificate/fallback rejection asserted; native oracle omitted\n";
  return 0;
 }
 const std::array<double,16> key{135.25,135.75,136.25,136.75,137.25,137.75,138.25,138.75,
                               139.25,139.75,140.25,140.75,141.25,141.75,142.25,142.75};
 const auto builder=[&](Tile::Plan& p,std::span<const double> at,std::uint64_t n,std::uint64_t chip,std::stop_token stop){
  return p.build_original(at,n,chip,numeric,stop);};
 auto acquired=cache->acquire(key,code.symbol_samples(),code.chip_samples(),builder);
 check(bool(acquired.lease)&&acquired.lease.validated_plan()==&acquired.lease.plan());
 Tile::Lane binding{&code,&c,0,0,acquired.lease.plan().first_local};
 check(tile->gather_evaluate_leased<64,4>({&binding,1},std::move(acquired.lease),Tile::Backend::scalar));
 check(!acquired.lease&&!cache->pinned()&&tile->samples()==key.size());
 check(cache->try_clear());reference.prepare_symbol(0);
 for(std::size_t n=0;n<key.size();++n){const auto a=tile->raw(n,0),b=reference.shaped_values(0,key[n]);check(bits(a[0],b[0])&&bits(a[1],b[1]));}
 // Empty or moved-from Lease cannot grant a certificate, even after a prior success.
 check(!tile->gather_evaluate_leased<64,4>({&binding,1},Cache::Lease{},Tile::Backend::scalar));check(tile->samples()==0);
 acquired=cache->acquire(key,code.symbol_samples(),code.chip_samples(),builder);auto held=std::move(acquired.lease);
 check(!acquired.lease.validated_plan()&&held.validated_plan());
 check(!tile->gather_evaluate_leased<64,4>({&binding,1},std::move(acquired.lease),Tile::Backend::scalar));held.reset();check(!cache->pinned());
 // A live certified public Plan does not bypass any private binding checks.
 acquired=cache->acquire(key,code.symbol_samples(),code.chip_samples(),builder);auto wrong=binding;wrong.original_config=nullptr;
 check(!tile->gather_evaluate_leased<64,4>({&wrong,1},std::move(acquired.lease),Tile::Backend::scalar));check(!cache->pinned()&&tile->samples()==0);
 acquired=cache->acquire(key,code.symbol_samples(),code.chip_samples(),builder);std::stop_source stop;stop.request_stop();bool cancelled=false;
 try{tile->gather_evaluate_leased<64,4>({&binding,1},std::move(acquired.lease),Tile::Backend::scalar,stop.get_token());}
 catch(const Error&){cancelled=true;}check(cancelled&&!cache->pinned()&&tile->samples()==0);
 // Malformed publication cannot produce a usable certificate; ordinary APIs
 // independently reject the same malformed structure and keep all old checks.
 check(cache->try_clear());
 acquired=cache->acquire(key,code.symbol_samples(),code.chip_samples(),[&](auto& p,auto at,std::uint64_t n,std::uint64_t chip,std::stop_token token){
  if(!builder(p,at,n,chip,token))return false;p.rows[0].taps=19;return true;});
 check(acquired.status==Cache::Status::rejected&&!acquired.lease&&!acquired.lease.validated_plan()&&!cache->pinned());
 Tile::Plan malformed;check(builder(malformed,key,code.symbol_samples(),code.chip_samples(),{}));malformed.rows[0].taps=19;
 check(!tile->gather_borrowed({&binding,1},malformed)&&tile->samples()==0);
 using Owned=ScheduledOrderedPulseCandidatesT<1,64,32>;Owned owned;Owned::Plan own_bad;Owned::Statistics own_stats;
 check(own_bad.build_original(key,code.symbol_samples(),code.chip_samples(),own_stats));own_bad.rows[0].weight[0]=std::numeric_limits<double>::quiet_NaN();
 Owned::Lane own_binding{&code,&c,0,0,own_bad.first_local};check(!owned.gather({&own_binding,1},own_bad));
 // Unsupported arithmetic still rejects before private reads even with a lease.
 acquired=cache->acquire(key,code.symbol_samples(),code.chip_samples(),builder);const auto rounding=std::fegetround();
 if(std::fesetround(FE_DOWNWARD)==0){check(!tile->gather_evaluate_leased<64,4>({&binding,1},std::move(acquired.lease),Tile::Backend::scalar));check(!cache->pinned());}
 check(std::fesetround(rounding)==0);
 std::cout<<"certified Plan exact outputs/ownership/private gates/malformed/fenv/cancel passed\n";
}
