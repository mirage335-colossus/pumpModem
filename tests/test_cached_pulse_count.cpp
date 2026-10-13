#include "scheduled_ordered_pulse_candidates.hpp"
#include "scheduled_temporal_plan_cache.hpp"
#include "datapump/sched_hop.hpp"
#include <cstddef>
#include <iostream>
#include <memory>
#include <type_traits>
using namespace datapump;
using namespace datapump::modem;
using namespace datapump::modem::detail;
using Tile=ScheduledOrderedPulseCandidatesT<1,512,96,false>;
using Cache=ScheduledTemporalPlanCacheT<Tile::Plan,512,4>;
struct OriginalRowLayout {
 std::array<double,Tile::maximum_taps> weight{};
 std::uint32_t nonzero=0;
 std::size_t first_offset=0,taps=0;
};
struct OriginalPlanLayout {
 std::array<OriginalRowLayout,Tile::maximum_samples> rows{};
 std::uint64_t first_local=0,symbol_samples=0,chip_samples=0;
 std::size_t samples=0,coefficients=0;
 std::bitset<Tile::maximum_coefficients> active_taps;
 bool valid=false;
};
static_assert(std::is_standard_layout_v<Tile::PulseRow>);
static_assert(alignof(Tile::PulseRow)==alignof(OriginalRowLayout));
static_assert(alignof(Tile::Plan)==alignof(OriginalPlanLayout));
static_assert(sizeof(std::size_t)!=8 || sizeof(Tile::PulseRow)==sizeof(OriginalRowLayout));
static_assert(sizeof(std::size_t)!=8 || sizeof(Tile::Plan)==sizeof(OriginalPlanLayout));
static_assert(sizeof(std::size_t)!=8 || offsetof(Tile::PulseRow,first_offset)==offsetof(OriginalRowLayout,first_offset));
static_assert(sizeof(std::size_t)!=8 || offsetof(Tile::PulseRow,taps)==offsetof(OriginalRowLayout,taps));
static void check(bool ok){if(!ok)throw std::runtime_error("cached pulse count oracle failure");}
static bool bits(std::complex<double> a,std::complex<double> b){
 return std::bit_cast<std::uint64_t>(a.real())==std::bit_cast<std::uint64_t>(b.real()) &&
        std::bit_cast<std::uint64_t>(a.imag())==std::bit_cast<std::uint64_t>(b.imag());
}
static Config config(bool partial){
 Config c;c.sample_rate=6000;c.carrier_hz=1500;c.bandwidth_hz=8;
 c.integration_seconds=partial?16.0005:16.;
 c.pattern_symbols=true;c.constellation_bits=1;c.spreading_factor=64;
 c.scramble=c.dsss=true;c.dsss_factor=100;c.outer_dsss_version=OuterDsssVersion::interleaved_v2;
 c.data_key.emplace(Bytes(32,0x67));c.stream_epoch=1799999967;c.scheduled_hopping=std::make_shared<SchedHopPlan>(SchedHopPlan{10,1});
 for(std::size_t i=0;i<c.spreading_seed.size();++i){c.spreading_seed[i]=3*i+7;c.dsss_seed[i]=5*i+11;}
 return c;
}
int main(){
 const bool native=Tile::arithmetic_supported();std::uint64_t checked=0,native_rows=0,native_terms=0;
 for(bool partial:{false,true})for(std::size_t count:{4U,32U,128U,512U})for(double rate:{1.,1.+3e-6})for(unsigned region=0;region<3;++region){
  const auto c=config(partial);PatternCode code(c,c.stream_epoch),reference(c,c.stream_epoch);
  const auto first=region==0?.25:region==1?135.5:static_cast<double>(code.symbol_samples())-count*rate-.25;
  std::array<double,512> coordinates{};for(std::size_t n=0;n<count;++n)coordinates[n]=first+n*rate;
  auto plan=std::make_unique<Tile::Plan>();Tile::Statistics stats;
  check(plan->build_original({coordinates.data(),count},code.symbol_samples(),code.chip_samples(),stats));
  check(plan->well_formed());std::uint64_t expected=0;
  for(std::size_t n=0;n<count;++n){
   const auto old=std::popcount(plan->rows[n].nonzero);check(plan->rows[n].nonzero_count==old);expected+=old;
  }
  // Cached metadata cannot grant equality or public/borrowed trust if stale.
  auto bad=std::make_unique<Tile::Plan>(*plan);++bad->rows[0].nonzero_count;
  check(!bad->well_formed() && !plan->same_weights(*bad));check(plan->same_weights(*plan));
  auto cache=std::make_unique<Cache>();
  auto acquired=cache->acquire({coordinates.data(),count},code.symbol_samples(),code.chip_samples(),
   [&](auto& destination,auto,std::uint64_t,std::uint64_t,std::stop_token){destination=*bad;return true;});
  check(acquired.status==Cache::Status::rejected && !acquired.lease && !cache->pinned());
  auto tile=std::make_unique<Tile>();Tile::Lane lane{&code,&c,17,0,plan->first_local};
  check(!tile->gather_borrowed({&lane,1},*bad) && tile->samples()==0);
  // All-zero/public edge rows must retain exact zero derived metadata.
  Tile::Plan zero;const std::array<double,4> outside{-1000.,-999.,-998.,-997.};Tile::Statistics zstats;
  check(zero.build_original(outside,code.symbol_samples(),code.chip_samples(),zstats) && zero.well_formed());
  check(zero.coefficients==0 && zero.rows[0].nonzero_count==0);
  zero.rows[0].nonzero_count=1;check(!zero.well_formed());
  ++checked;
  if(!native)continue;
  code.set_stream_phase_samples(17);reference.set_stream_phase_samples(17);reference.prepare_symbol(0);
  check(tile->gather_borrowed({&lane,1},*plan));check(tile->evaluate(Tile::Backend::scalar));
  check(tile->statistics.output_pairs==count && tile->statistics.shape_terms==expected);
  for(std::size_t n=0;n<count;++n){
   const auto actual=tile->raw(n,0),original=reference.shaped_values(0,coordinates[n]);
   check(bits(actual[0],original[0]) && bits(actual[1],original[1]));
  }
  native_rows+=count;native_terms+=expected;
 }
 // Owned public API has its own Plan type and must reject stale metadata too.
 using Owned=ScheduledOrderedPulseCandidatesT<1,16,32>;Owned owned;Owned::Plan bad;Owned::Statistics stats;
 const auto c=config(false);PatternCode code(c,c.stream_epoch);const std::array<double,4> at{135.5,136.5,137.5,138.5};
 check(bad.build_original(at,code.symbol_samples(),code.chip_samples(),stats));++bad.rows[0].nonzero_count;
 Owned::Lane lane{&code,&c,0,0,bad.first_local};check(!owned.gather({&lane,1},bad));
 std::cout<<"cases="<<checked<<" native_rows="<<native_rows<<" original_shape_terms="<<native_terms
          <<" native="<<native<<" row_bytes="<<sizeof(Tile::PulseRow)<<" original_row_bytes="<<sizeof(OriginalRowLayout)
          <<" plan_bytes="<<sizeof(Tile::Plan)<<" original_plan_bytes="<<sizeof(OriginalPlanLayout)
          <<" row_alignment="<<alignof(Tile::PulseRow)<<" plan_alignment="<<alignof(Tile::Plan)<<'\n';
}
