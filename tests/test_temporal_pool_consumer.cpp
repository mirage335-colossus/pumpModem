// Standalone public/private/scatter oracle; root compiles/runs under its lease.
#include "scheduled_temporal_templates.hpp"
#include <iostream>
#include <stdexcept>
using namespace datapump;
using namespace datapump::modem;
using namespace datapump::modem::detail;
static void check(bool ok){if(!ok)throw std::runtime_error("temporal pool consumer oracle failure");}
static bool bits(std::complex<double> a,std::complex<double> b){
 return std::bit_cast<std::uint64_t>(a.real())==std::bit_cast<std::uint64_t>(b.real())&&
        std::bit_cast<std::uint64_t>(a.imag())==std::bit_cast<std::uint64_t>(b.imag());
}
int main(){
 Config c;c.sample_rate=6000;c.carrier_hz=1500;c.bandwidth_hz=8;c.integration_seconds=16;
 c.pattern_symbols=true;c.constellation_bits=1;c.spreading_factor=64;c.scramble=c.dsss=true;c.dsss_factor=100;
 c.outer_dsss_version=OuterDsssVersion::interleaved_v2;c.data_key.emplace(Bytes(32,0x67));
 for(std::size_t i=0;i<c.spreading_seed.size();++i){c.spreading_seed[i]=3*i+7;c.dsss_seed[i]=5*i+11;}
 c.stream_epoch=1799999967;
 PatternCode code(c,c.stream_epoch),original(c,c.stream_epoch);
 auto batch=std::make_unique<ScheduledTemporalTemplates>();batch->allocate_plan_cache();
 check(code.chip_samples()==15&&batch->plan_cache_bytes()==ScheduledTemporalTemplates::plan_cache_charge);
 std::array<std::complex<double>,2> saved{};
 for(std::uint64_t lane=0;lane<12;++lane){
  const auto shift=555*lane,phase=37*lane,first=(lane%4)*code.chips_per_symbol();
  // Three public coordinate keys are interleaved across independent private
  // bindings. Physical selected masks differ despite equal public vectors.
  const double residual=.25*(1+lane%3);const auto physical_offset=static_cast<std::size_t>(lane%8);
  batch->begin(lane,4096);batch->start(phase,first,4096);
  for(std::size_t n=0;n<64;++n)check(batch->append(physical_offset+n,135.+residual+shift+.5*n));
  batch->end(4608);
  if(!ScheduledTemporalTemplates::Tile::arithmetic_supported()) {
   check(!batch->prepare(code,c));
   std::array<std::complex<double>,2> unavailable;
   check(!batch->raw(lane,4096,phase,first,4096+physical_offset,physical_offset,
                     135.+residual+shift,unavailable));
   std::cout<<"native exact arithmetic unavailable; original fallback asserted; native oracle omitted\n";
   return 0;
  }
  check(batch->prepare(code,c));
  original.set_stream_phase_samples(phase);original.prepare_symbol(first);
  for(std::size_t n=0;n<64;++n){
   const auto at=135.+residual+shift+.5*n;std::array<std::complex<double>,2> actual;
   check(batch->raw(lane,4096,phase,first,4096+physical_offset+n,physical_offset+n,at,actual));
   const auto expected=original.shaped_values(first,at);
   check(bits(actual[0],expected[0])&&bits(actual[1],expected[1]));
   if(lane==11&&n==0)saved=actual;
  }
  std::array<std::complex<double>,2> unused;
  check(!batch->raw(lane+1,4096,phase,first,4096+physical_offset,physical_offset,135.+residual+shift,unused));
 }
 const auto counters=batch->plan_cache_statistics();
 check(counters.requests==12&&counters.builds==3&&counters.hits==9&&counters.evictions==0);
 // Public storage may disappear BEFORE the original raw consumer finishes.
 batch->drop_plan_cache();check(batch->plan_cache_bytes()==0);
 std::array<std::complex<double>,2> detached;
 check(batch->raw(11,4096,407,3*code.chips_per_symbol(),4099,3,135.75+555*11,detached));
 check(bits(detached[0],saved[0])&&bits(detached[1],saved[1]));
 const auto retired=batch->plan_cache_statistics();check(retired.requests==12&&retired.hits==9);
 std::cout<<"temporal pool exact private outputs/different masks/detached eviction passed;charge="
          <<ScheduledTemporalTemplates::plan_cache_charge<<'\n';
}
