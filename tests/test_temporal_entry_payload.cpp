#define DATAPUMP_ORDERED_PULSE_CONTRACT_OFF 1
#include "scheduled_shared_temporal_plans.hpp"
#include "datapump/crypto.hpp"
#include "datapump/sched_hop.hpp"
#include <bit>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>
using namespace datapump;
using namespace datapump::modem;
using namespace datapump::modem::detail;
static void check(bool value,const char* message){if(!value)throw Error(message);}
struct TinyPlan {
    std::uint64_t symbol_samples=0,chip_samples=0,marker=0;std::size_t samples=0;bool valid=false;
    bool well_formed()const{return valid&&samples&&symbol_samples&&chip_samples;}
};
struct Payload {std::uint64_t marker=0;};
struct OtherPayload {std::uint64_t marker=0;};
using Cache=ScheduledTemporalPlanCacheT<TinyPlan,8,1>;
static_assert(std::is_trivially_copyable_v<Payload>);
static void cache_generations() {
    Cache cache;unsigned builds=0,packs=0;
    const std::array<double,2>a{135.5,136.5},b{137.5,138.5};
    const auto original=[&](TinyPlan& p,auto x,std::uint64_t n,std::uint64_t c,std::stop_token){
        p={n,c,++builds,x.size(),true};return true;};
    const auto pack=[&](Payload& p,const TinyPlan& exact,std::stop_token){++packs;p.marker=exact.marker;return true;};
    typename Cache::Lease empty;
    check(empty.get_or_build_payload<Payload>(pack).status==Cache::PayloadStatus::unavailable,"empty lease exposed payload");
    check(cache.payload_bytes()==0&&cache.allocate_payload<Payload>(),"optional payload allocation failed");
    check(cache.payload_bytes()==Cache::payload_charge<Payload>(),"actual arena bytes not charged");
    check(!cache.allocate_payload<OtherPayload>(),"wrong payload type replaced arena");
    auto A=cache.acquire(a,96000,15,original);const auto* plan_address=A.lease.validated_plan();
    const auto P=A.lease.get_or_build_payload<Payload>(pack);const auto* payload_address=P.value;
    check(P.status==Cache::PayloadStatus::built&&P.value->marker==1,"initial payload build missing");
    const auto hit=A.lease.get_or_build_payload<Payload>(pack);
    check(hit.status==Cache::PayloadStatus::hit&&hit.value==P.value&&packs==1,"same generation repacked");
    check(A.lease.get_or_build_payload<OtherPayload>([](auto&,auto&,auto){return true;}).status==Cache::PayloadStatus::unavailable,"wrong typed lease alias");
    check(!cache.try_clear()&&!cache.try_drop_payload()&&!cache.allocate_payload<Payload>(),"pinned owner mutation permitted");
    auto blocked=cache.acquire(b,96000,15,original);check(!blocked.lease&&packs==1,"active generation evicted");
    A.lease.reset();auto B=cache.acquire(b,96000,15,original);
    check(B.lease.validated_plan()==plan_address,"single physical row unexpectedly moved");
    const auto second=B.lease.get_or_build_payload<Payload>(pack);
    check(second.status==Cache::PayloadStatus::built&&second.value==payload_address&&second.value->marker==2&&packs==2,
          "physical row reused stale payload generation");
    B.lease.reset();check(cache.try_clear()&&cache.ready_entries()==0,"clear failed");
    auto C=cache.acquire(a,96000,15,original);
    const auto third=C.lease.get_or_build_payload<Payload>(pack);
    check(third.status==Cache::PayloadStatus::built&&packs==3&&third.value->marker==3,"try_clear retained payload");
    C.lease.reset();const auto rows=cache.ready_entries();
    check(cache.try_drop_payload()&&cache.payload_bytes()==0&&cache.ready_entries()==rows,"payload pressure drop changed exact rows");
    auto exact_hit=cache.acquire(a,96000,15,original);
    check(exact_hit.status==Cache::Status::hit&&builds==3&&exact_hit.lease.get_or_build_payload<Payload>(pack).status==Cache::PayloadStatus::unavailable,
          "payload drop rebuilt or lost exact public Plan");
    exact_hit.lease.reset();check(cache.allocate_payload<Payload>(),"arena rebuild failed");
    auto again=cache.acquire(a,96000,15,original);
    check(again.lease.get_or_build_payload<Payload>(pack).status==Cache::PayloadStatus::built&&packs==4,"new arena reused absent generation");
}
static void failed_publications() {
    Cache cache;check(cache.allocate_payload<Payload>(),"failure test allocation");
    const std::array<double,1>key{135.5};
    auto original=[](TinyPlan& p,auto x,std::uint64_t n,std::uint64_t c,std::stop_token){p={n,c,7,x.size(),true};return true;};
    auto acquired=cache.acquire(key,96000,15,original);auto& lease=acquired.lease;
    check(lease.get_or_build_payload<Payload>([](auto& p,auto&,auto){p.marker=99;return false;}).status==Cache::PayloadStatus::rejected,
          "false producer published payload");
    bool threw=false;try{lease.get_or_build_payload<Payload>([](auto&,auto&,auto)->bool{throw std::runtime_error("injected producer failure");});}
    catch(const std::runtime_error&){threw=true;}check(threw,"producer throw swallowed");
    std::stop_source cancelled;bool stopped=false;
    try{lease.get_or_build_payload<Payload>([&](auto& p,auto&,auto){p.marker=88;cancelled.request_stop();return true;},cancelled.get_token());}
    catch(const Error&){stopped=true;}check(stopped,"final cancellation published ready payload");
    unsigned nested=0;
    const auto ready=lease.get_or_build_payload<Payload>([&](auto& p,const auto& exact,auto){
        const auto reentry=lease.get_or_build_payload<Payload>([&](auto&,auto&,auto){++nested;return true;});
        check(reentry.status==Cache::PayloadStatus::busy&&!reentry.value&&nested==0,"reentrant builder mutated BUILDING");
        p.marker=exact.marker;return true;});
    check(ready.status==Cache::PayloadStatus::built&&ready.value->marker==7,"failed producer left sticky state");
    stopped=false;try{lease.get_or_build_payload<Payload>([](auto&,auto&,auto){return true;},cancelled.get_token());}
    catch(const Error&){stopped=true;}check(stopped,"ready payload ignored requested cancellation");
    check(lease.get_or_build_payload<Payload>([](auto&,auto&,auto){return false;}).status==Cache::PayloadStatus::hit,"cancelled hit lost valid generation");
}
static void shared_owner() {
    using Pool=ScheduledSharedTemporalPlansT<Cache,2>;
    const auto base=Pool::charge_for(2),extra=Pool::payload_charge_for<Payload>(2);
    auto owner=Pool::create(2,base);check(bool(owner)&&owner->working_bytes()==base,"exact owner grant changed");
    check(!owner->allocate_payloads<Payload>(extra-1)&&owner->payload_bytes()==0,"partial budget allocated payload");
    check(owner->allocate_payloads<Payload>(extra)&&owner->working_bytes()==base+extra,"shared optional arena accounting");
    check(owner->allocate_payloads<Payload>(0)&&owner->working_bytes()==base+extra,"repeat grant double charged");
    const std::array<double,1>key{135.5};unsigned builds=0,packs=0;
    const auto original=[&](TinyPlan& p,auto x,std::uint64_t n,std::uint64_t c,std::stop_token){p={n,c,++builds,x.size(),true};return true;};
    const auto pack=[&](Payload& p,const TinyPlan& plan,std::stop_token){++packs;p.marker=plan.marker;return true;};
    {
        auto first=owner->try_borrow(2);check(bool(first)&&!owner->try_drop_payloads()&&!owner->allocate_payloads<Payload>(extra),"borrowed owner changed arena");
        auto a=first.slot(0)->acquire(key,96000,15,original);
        check(a.lease.get_or_build_payload<Payload>(pack).status==Cache::PayloadStatus::built,"first receiver payload missing");
        a.lease.reset();first.reset();
    }
    {
        auto next=owner->try_borrow(2);auto a=next.slot(0)->acquire(key,96000,15,original);
        check(a.status==Cache::Status::hit&&a.lease.get_or_build_payload<Payload>(pack).status==Cache::PayloadStatus::hit&&builds==1&&packs==1,
              "sequential bank repacked same public generation");a.lease.reset();next.reset();
    }
    check(owner->try_drop_payloads()&&owner->working_bytes()==base,"shared arena pressure release charge");
    {
        auto borrow=owner->try_borrow(2);
        check(borrow.slot(0)->ready_entries()==1&&borrow.slot(1)->allocate_payload<OtherPayload>(),"drop changed exact Plan or wrongtype setup");
        borrow.reset();
    }
    const auto preexisting=owner->payload_bytes();
    check(!owner->allocate_payloads<Payload>(extra)&&owner->payload_bytes()==preexisting,"wrongtype all-shard rollback lost/added arena");
    check(owner->try_drop_payloads()&&owner->working_bytes()==base,"wrongtype payload cleanup");
    check(owner->allocate_payloads<Payload>(extra),"shared payload regrant failed");
    auto rebound=owner->try_borrow(2);auto exact=rebound.slot(0)->acquire(key,96000,15,original);
    check(exact.status==Cache::Status::hit&&builds==1&&
          exact.lease.get_or_build_payload<Payload>(pack).status==Cache::PayloadStatus::built&&packs==2,
          "shared payload drop/regrant bypassed fresh build or rebuilt exact row");
    exact.lease.reset();rebound.reset();
}
static Config configuration(unsigned private_seed=0) {
    Config c;c.sample_rate=6000;c.carrier_hz=1500;c.bandwidth_hz=8;c.integration_seconds=16;
    c.pattern_symbols=true;c.constellation_bits=1;c.spreading_factor=64;c.scramble=c.dsss=true;c.dsss_factor=100;
    c.outer_dsss_version=OuterDsssVersion::interleaved_v2;c.data_key.emplace(Bytes(32,static_cast<std::uint8_t>(0x67+private_seed)));
    for(std::size_t i=0;i<c.spreading_seed.size();++i){c.spreading_seed[i]=3*i+7+private_seed;c.dsss_seed[i]=5*i+11+private_seed;}
    c.stream_epoch=1799999967;c.scheduled_hopping=std::make_shared<SchedHopPlan>(SchedHopPlan{10,1});return c;
}
static bool native_sharing() {
    using Batch=ScheduledTemporalTemplates;using Tile=Batch::Tile;using Facade=Batch::Fp32Execution;
    using Native=Facade::Native;using Public=ScheduledTemporalPlanCacheT<Tile::Plan,512,1>;
    static_assert(std::is_trivially_copyable_v<Native::PackedPlan> && std::is_trivially_destructible_v<Native::PackedPlan>);
    if(!Facade::arithmetic_supported())return false;
    auto pool=std::make_unique<Public>();check(pool->allocate_payload<Native::PackedPlan>(),"native optional arena grant");
    std::array<double,64>key{};for(std::size_t i=0;i<key.size();++i)key[i]=135.5+i;
    Tile::Statistics original_numeric;
    const auto original=[&](auto& plan,auto at,std::uint64_t n,std::uint64_t c,std::stop_token stop){return plan.build_original(at,n,c,original_numeric,stop);};
    std::array<std::array<std::complex<double>,2>,64> previous{};bool private_changed=false;
    for(unsigned private_seed=0;private_seed<2;++private_seed) {
        auto c=configuration(private_seed);PatternCode code(c,c.stream_epoch),reference(c,c.stream_epoch);
        auto tile=std::make_unique<Tile>();auto facade=std::make_unique<Facade>(*tile);
        auto acquired=pool->acquire(key,code.symbol_samples(),code.chip_samples(),original);
        check(bool(acquired.lease),"native exact public lease unavailable");
        const auto first_local=acquired.lease.plan().first_local;
        const Tile::Lane lane{&code,&c,0,0,first_local};
        facade->begin_request(key,code.symbol_samples(),code.chip_samples());
        check(facade->gather_evaluate_leased<512,1>({&lane,1},std::move(acquired.lease),Tile::Backend::scalar),"native payload execution failed");
        check(!pool->pinned(),"native facade leaked public lease");
        reference.set_stream_phase_samples(0);reference.prepare_symbol(0);
        for(std::size_t row=0;row<key.size();++row) {
            Native::Value value;check(facade->raw(row,value),"native packed output missing");
            const auto exact=reference.shaped_values(0,key[row]);
            for(unsigned bit=0;bit<2;++bit)check(std::abs(value[bit].real()-exact[bit].real())<=1e-5&&
                std::abs(value[bit].imag()-exact[bit].imag())<=1e-5,"shared public payload exceeded same component radius");
            if(private_seed==0)previous[row]=value;else private_changed|=previous[row]!=value;
        }
        const auto counts=facade->counters();
        check(counts.native_tiles==1&&counts.numeric.coefficient_narrowings>0&&counts.numeric.prepares==0,
              "private original gather/native coefficient preparation not executed");
        check(tile->statistics.value_pairs>0,"each candidate omitted original private gather");
        if(private_seed==0)check(counts.entry_builds==1&&counts.numeric.weight_narrowings>0,"first native pack missing");
        else check(counts.entry_hits==1&&counts.numeric.weight_narrowings==0,"second private candidate repacked shared public weights");
        facade->end_request();
    }
    check(private_changed,"private candidate coefficients were reused");
    // Untrusted public pack still verifies derived cached counts. No validation
    // shortcut is introduced by replacing its redundant post-check popcount.
    auto invalid=std::make_unique<Tile::Plan>();Tile::Statistics numeric;
    check(invalid->build_original(key,96000,15,numeric),"invalid count setup Plan failed");
    invalid->rows[0].nonzero_count^=1;
    auto packed=std::make_unique<Native::PackedPlan>();Native::Statistics stats;
    check(!packed->pack(*invalid,stats)&&!packed->valid,"untrusted cached nonzero count accepted");
    return true;
}
int main() {
    cache_generations();failed_publications();shared_owner();const bool native=native_sharing();
    std::cout<<"entry payload generation/type/pin/reentry/cancel/rollback/accounting oracle passed; native="<<native
             <<" tiny_arena_bytes="<<Cache::payload_charge<Payload>()
             <<" real_packed_plan_bytes="<<sizeof(ScheduledTemporalFp32Execution::Native::PackedPlan)
             <<" shared_workers="<<SharedTemporalPlans::maximum_workers
             <<" shared_entries="<<ScheduledTemporalTemplates::SharedPlanCache::capacity
             <<" per_worker_payload="<<ScheduledTemporalTemplates::SharedPlanCache::payload_charge<ScheduledTemporalFp32Execution::Native::PackedPlan>()<<'\n';
    if(!native){std::cerr<<"native arithmetic portion omitted under executing ISA/fenv; generic lifetime/accounting passed\n";return 77;}
}
