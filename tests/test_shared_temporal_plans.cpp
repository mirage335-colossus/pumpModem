#include "scheduled_shared_temporal_plans.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>
using namespace datapump::modem;
using namespace datapump::modem::detail;
static void check(bool ok){if(!ok)throw std::runtime_error("shared temporal owner oracle failure");}
struct TinyPlan {
    std::uint64_t symbol_samples=0,chip_samples=0;
    std::size_t samples=0;bool valid=false;
    bool well_formed()const{return valid && samples && symbol_samples && chip_samples;}
};
using Cache=ScheduledTemporalPlanCacheT<TinyPlan,8,4>;
using Owner=ScheduledSharedTemporalPlansT<Cache,2>;
static_assert(!std::is_copy_constructible_v<Owner> && !std::is_move_constructible_v<Owner>);
static_assert(!std::is_copy_constructible_v<Owner::Borrow> && std::is_move_constructible_v<Owner::Borrow>);
static_assert(ScheduledTemporalPlanCacheT<TinyPlan,8,64>::capacity==64);
int main(){
 const auto charge=Owner::charge_for(2);
 check(charge==sizeof(Owner)+2*(sizeof(Cache)+Cache::lookup_callee_allowance));
 check(Owner::charge_for(0)==std::numeric_limits<std::size_t>::max());
 check(Owner::charge_for(3)==std::numeric_limits<std::size_t>::max());
 check(!Owner::create(2,charge-1)); // exact one-byte-short grant fails before any allocation
 auto owner=Owner::create(2,charge);check(owner && owner->working_bytes()==charge && owner->idle());
 auto first=owner->try_borrow(2);check(first && !owner->idle() && first.workers()==2);
 check(!owner->try_borrow(1) && first.slot(0) && first.slot(1) && !first.slot(2));
 check(first.slot(0)!=first.slot(1));
 const std::array<double,4> key{135.25,135.75,136.25,136.75};unsigned builds=0;
 const auto builder=[&](TinyPlan& p,auto x,std::uint64_t n,std::uint64_t c,std::stop_token){
  ++builds;p={n,c,x.size(),true};return true;};
 auto row=first.slot(1)->acquire(key,96000,15,builder);check(row.status==Cache::Status::built);
 const auto* address=&row.lease.plan();row.lease.reset();
 auto moved=std::move(first);check(!first && moved && !owner->try_borrow(1));moved.reset();check(owner->idle());
 // A different receiver push reuses only the SAME worker index. Another worker
 // has a separate row owner even for identical public coordinates.
 auto second=owner->try_borrow(2);
 auto hit=second.slot(1)->acquire(key,96000,15,builder);
 check(hit.status==Cache::Status::hit && &hit.lease.plan()==address && builds==1);hit.lease.reset();
 auto private_slot=second.slot(0)->acquire(key,96000,15,builder);
 check(private_slot.status==Cache::Status::built && builds==2);private_slot.lease.reset();
 std::stop_source stop;stop.request_stop();bool cancelled=false;
 try {second.slot(1)->acquire(key,96000,15,builder,stop.get_token());}catch(const datapump::Error&){cancelled=true;}
 check(cancelled && !second.slot(1)->pinned());second.reset();check(owner->idle());
 const auto stats=owner->statistics();check(stats.requests==4 && stats.hits==1 && stats.builds==2 && stats.cancelled==1);
 check(!owner->try_borrow(3));owner.reset();
 // Real consumer lifetime: root's original scalar private-output oracle remains
 // mandatory. Here public rows are retired immediately after prepare returns.
 Config c;c.sample_rate=6000;c.carrier_hz=1500;c.bandwidth_hz=8;c.integration_seconds=16;
 c.pattern_symbols=true;c.constellation_bits=1;c.spreading_factor=64;c.scramble=c.dsss=true;c.dsss_factor=100;
 c.outer_dsss_version=OuterDsssVersion::interleaved_v2;c.data_key.emplace(datapump::Bytes(32,0x67));
 c.stream_epoch=1799999967;
 PatternCode code(c,c.stream_epoch),reference(c,c.stream_epoch);
 auto batch=std::make_unique<ScheduledTemporalTemplates>();
 auto shared=SharedTemporalPlans::create(1,SharedTemporalPlans::charge_for(1));
 auto binding=shared->try_borrow(1);batch->bind_shared_plan_cache(binding.slot(0));
 batch->begin(7,4096);batch->start(37,0,4096);
 for(std::size_t i=0;i<64;++i)check(batch->append(i,135.25+.5*i));batch->end(4608);
 const bool ready=batch->prepare(code,c);
 batch->bind_shared_plan_cache(nullptr);binding.reset();check(shared->idle());shared.reset();
 if(ScheduledTemporalTemplates::supported()) {
  check(ready);reference.set_stream_phase_samples(37);reference.prepare_symbol(0);
  for(std::size_t i=0;i<64;++i){
   std::array<std::complex<double>,2> actual;check(batch->raw(7,4096,37,0,4096+i,i,135.25+.5*i,actual));
   const auto expected=reference.shaped_values(0,135.25+.5*i);
   for(std::size_t bit=0;bit<2;++bit){
    check(std::bit_cast<std::uint64_t>(actual[bit].real())==std::bit_cast<std::uint64_t>(expected[bit].real()));
    check(std::bit_cast<std::uint64_t>(actual[bit].imag())==std::bit_cast<std::uint64_t>(expected[bit].imag()));
   }
  }
 }else check(!ready);
 std::cout<<"shared temporal exact charge/exclusive worker slots/sequential reuse/cancellation/detached consumer passed\n";
}
