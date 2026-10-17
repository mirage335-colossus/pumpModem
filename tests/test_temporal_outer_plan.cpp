// Exact public Plan rows feed the ORIGINAL private outer guard callbacks.
#define DATAPUMP_ORDERED_PULSE_CONTRACT_OFF 1
#include "scheduled_temporal_templates.hpp"
#include "scheduled_shared_temporal_plans.hpp"
#include "pattern_correlator_batch.hpp"
#include <iostream>
#include <vector>
using namespace datapump;
using namespace datapump::modem;
using namespace datapump::modem::detail;
using Batch=ScheduledTemporalTemplates;
using Value=std::array<std::complex<double>,2>;
static void check(bool ok,const char* why){if(!ok)throw Error(why);}
static bool bits(double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);}
static bool bits(std::complex<double> a,std::complex<double> b){return bits(a.real(),b.real())&&bits(a.imag(),b.imag());}
static Config configuration() {
    Config c;c.sample_rate=6000;c.carrier_hz=1500;c.bandwidth_hz=8;c.integration_seconds=16;
    c.pattern_symbols=true;c.constellation_bits=1;c.spreading_factor=64;c.scramble=c.dsss=true;c.dsss_factor=100;
    c.outer_dsss_version=OuterDsssVersion::interleaved_v2;c.data_key.emplace(Bytes(32,0x67));
    for(std::size_t i=0;i<c.spreading_seed.size();++i){c.spreading_seed[i]=3*i+7;c.dsss_seed[i]=5*i+11;}
    c.stream_epoch=1799999967;return c;
}
static void equal(const CorrelationOuterEvidence& a,const CorrelationOuterEvidence& b) {
    check(a.pulse==b.pulse&&a.dot==b.dot&&a.variance==b.variance&&a.contributing_terms==b.contributing_terms&&
          a.first==b.first&&a.last==b.last&&a.initialized==b.initialized,"ordered private outer ring bits/state changed");
    for(unsigned bit=0;bit<2;++bit)check(bits(a.score(bit),b.score(bit))&&a.can_admit(bit,17)==b.can_admit(bit,17),
          "outer score/admission changed");
}
// Storage 0=owned fallback,1=local pool,2=shared pool. Precision 0=FP64,
// 1=diagnostic native,2=zero-radius original refinement. Outer is exact in all.
static void rows(const Config& c,unsigned storage,unsigned precision,double start,bool retain=true) {
    PatternCode code(c,c.stream_epoch),reference(c,c.stream_epoch);
    auto batch=std::make_unique<Batch>();auto pool=std::unique_ptr<Batch::SharedPlanCache>{};
    if(storage==1)batch->allocate_plan_cache();
    if(storage==2){pool=std::make_unique<Batch::SharedPlanCache>();batch->bind_shared_plan_cache(pool.get());}
    if(precision)batch->allocate_fp32(precision==2?0.:1e-5);
    const std::uint64_t phase=317,first_chip=3*code.chips_per_symbol(),block=4096;
    batch->begin(17,block);batch->start(phase,first_chip,block);
    std::array<double,64> within{};std::size_t count=0;
    for(;count<within.size();++count) {
        const auto at=static_cast<double>(static_cast<long double>(start)+count*(1.L+3e-6L));
        if(at>=code.symbol_samples())break;within[count]=at;
        if(count%7)check(batch->append(count,at),"selected row exporter overflow");
    }
    batch->end(block+count);check(batch->prepare(code,c,{},retain),"outer Plan preparation unavailable");
    check(batch->outer_plan_retained()==retain,"retain flag did not control optional Plan lifetime");
    if(pool)check(pool->pinned()==retain,"shared pin disagrees with retained Plan");
    code.set_stream_phase_samples(phase);code.prepare_symbol(first_chip);
    reference.set_stream_phase_samples(phase);reference.prepare_symbol(first_chip); // ORIGINAL caller restoration
    CorrelationOuterEvidence actual,expected;std::vector<std::uint64_t> actual_private,expected_private;
    for(std::size_t n=0;n<count;++n) {
        Value value;const auto found=batch->raw(17,block,phase,first_chip,block+n,n,within[n],value);
        check(found==(n%7!=0),"selected/current-carry raw identity changed");if(!found)continue;
        const auto direct=reference.shaped_values(first_chip,within[n]);
        if(precision!=1)for(unsigned bit=0;bit<2;++bit)check(bits(value[bit],direct[bit]),"original raw basis changed");
        std::vector<std::pair<std::uint64_t,double>> cached,original;
        const auto collect=[&](std::uint64_t i,double w){cached.emplace_back(i,w);};
        // Bad identity must neither emit a callback nor consume a valid ticket.
        check(!batch->outer_pulses(18,block,phase,first_chip,block+n,n,within[n],collect),"wrong hypothesis accepted");
        check(!batch->outer_pulses(17,block+1,phase,first_chip,block+n,n,within[n],collect),"wrong block accepted");
        check(!batch->outer_pulses(17,block,phase+1,first_chip,block+n,n,within[n],collect),"wrong private phase accepted");
        check(!batch->outer_pulses(17,block,phase,first_chip+code.chips_per_symbol(),block+n,n,within[n],collect),"current/carry target accepted");
        check(!batch->outer_pulses(17,block,phase,first_chip,block+n+1,n,within[n],collect),"wrong observed sample accepted");
        check(!batch->outer_pulses(17,block,phase,first_chip,block+n,n,std::nextafter(within[n],0.),collect),"changed exact source bits accepted");
        check(cached.empty(),"failed identity emitted partial callbacks");
        check(batch->outer_pulses(17,block,phase,first_chip,block+n,n,within[n],collect)==retain,"prepared outer row unavailable");
        check(!batch->outer_pulses(17,block,phase,first_chip,block+n,n,within[n],collect),"outer row chronology replayed twice");
        pattern_pulse_each(within[n],code.symbol_samples(),code.chip_samples(),
            [&](std::uint64_t i,double w){original.emplace_back(i,w);});
        if(retain) {
            check(cached.size()==original.size(),"pulse callback count changed");
            for(std::size_t p=0;p<cached.size();++p)check(cached[p].first==original[p].first&&bits(cached[p].second,original[p].second),
                "translated/partial/nonnominal original pulse label/weight/order bits changed");
        }else cached=original; // ORIGINAL caller fallback
        const auto gain=.73,xc=.6*std::sin(.27*n),xs=.2*std::cos(.17*n);
        for(const auto& [i,w]:cached)actual.add(i,gain*w*std::complex<double>{xc,xs},[&](std::uint64_t j){
            actual_private.push_back(first_chip+j);return code.values(first_chip+j);});
        for(const auto& [i,w]:original)expected.add(i,gain*w*std::complex<double>{xc,xs},[&](std::uint64_t j){
            expected_private.push_back(first_chip+j);return reference.values(first_chip+j);});
        equal(actual,expected);check(actual_private==expected_private,"older ring private callback chronology changed");
    }
    if(precision==1)check(batch->fp32_counters().native_tiles>0,"native outer fixture did not execute native route");
    if(precision==2)check(batch->fp32_counters().refined_tiles>0,"zero-radius fixture did not execute original refinement");
    actual.finish([&](std::uint64_t j){actual_private.push_back(first_chip+j);return code.values(first_chip+j);});
    expected.finish([&](std::uint64_t j){expected_private.push_back(first_chip+j);return reference.values(first_chip+j);});
    equal(actual,expected);check(actual_private==expected_private,"complete-pulse final private flush changed");
    // Detach before public eviction, preserving raw output but dropping outer access.
    const auto n=std::size_t{1};Value saved,detached;
    check(batch->raw(17,block,phase,first_chip,block+n,n,within[n],saved),"last raw unavailable");
    batch->release_outer_plan();check(!batch->outer_plan_retained(),"explicit detach retained Plan");
    if(pool)check(!pool->pinned()&&pool->try_clear(),"detached shared row could not be evicted");
    batch->drop_plan_cache();
    check(batch->raw(17,block,phase,first_chip,block+n,n,within[n],detached),"detached raw output was discarded");
    check(bits(saved[0],detached[0])&&bits(saved[1],detached[1]),"row eviction changed owned raw output");
    unsigned calls=0;check(!batch->outer_pulses(17,block,phase,first_chip,block+n,n,within[n],
        [&](auto,auto){++calls;} )&&calls==0,"released row emitted callback");
    batch->bind_shared_plan_cache(nullptr); // independent pool must outlive its binding
}
static void lifetimes(const Config& c) {
    PatternCode code(c,c.stream_epoch);auto pool=std::make_unique<Batch::SharedPlanCache>();auto batch=std::make_unique<Batch>();
    batch->bind_shared_plan_cache(pool.get());
    const auto prepare=[&](std::stop_token stop={}) {
        batch->begin(1,0);batch->start(0,0,0);for(std::size_t n=0;n<16;++n)check(batch->append(n,135.25+n),"lifetime exporter failed");
        batch->end(16);check(batch->prepare(code,c,stop,true),"lifetime prepare failed");check(pool->pinned(),"row was not pinned");
    };
    prepare();batch->begin(2,0);check(!pool->pinned(),"begin leaked pin");
    prepare();batch->start(1,0,0);check(!pool->pinned(),"start leaked pin");
    prepare();batch->drop_plan_cache();check(!pool->pinned(),"drop_plan_cache leaked shared pin");
    prepare();batch->drop_fp32();check(!pool->pinned(),"drop_fp32 leaked shared pin");
    prepare();batch->bind_shared_plan_cache(pool.get());check(!pool->pinned(),"same-pointer join rebind leaked pin");
    prepare();batch->bind_shared_plan_cache(nullptr);check(!pool->pinned(),"null join rebind leaked pin");
    batch->bind_shared_plan_cache(pool.get());prepare();batch.reset();check(!pool->pinned(),"Temporal destructor leaked pin");
    batch=std::make_unique<Batch>();batch->bind_shared_plan_cache(pool.get());prepare();
    std::stop_source cancel;cancel.request_stop();bool stopped=false;
    try{batch->prepare(code,c,cancel.get_token(),true);}catch(const Error&){stopped=true;}
    check(stopped&&!pool->pinned()&&!batch->outer_plan_retained(),"cancelled reprepare leaked old/new pin");
    batch->start(0,0,0);batch->append(0,135.25);batch->end(1);
    check(!batch->prepare(code,c,{},true)&&!pool->pinned()&&!batch->outer_plan_retained(),"undersized failure retained Plan");
    batch->bind_shared_plan_cache(nullptr);
    auto owner=SharedTemporalPlans::create(1,SharedTemporalPlans::charge_for(1));auto borrow=owner->try_borrow(1);
    batch->bind_shared_plan_cache(borrow.slot(0));
    batch->begin(1,0);batch->start(0,0,0);for(unsigned n=0;n<16;++n)batch->append(n,135.25+n);batch->end(16);
    check(batch->prepare(code,c,{},true)&&borrow.slot(0)->pinned(),"shared owner retained row missing");
    batch->bind_shared_plan_cache(nullptr);borrow.reset();check(owner->idle(),"joined cleanup did not return owner to idle");
}
int main() {
    if(!Batch::supported()){std::cerr<<"exact temporal execution unavailable; outer optimized coverage omitted\n";return 77;}
    const auto c=configuration();
    for(unsigned storage=0;storage<3;++storage)for(const double start:{std::nextafter(405.,0.),135.5,690.5,32768.,95968.25})
        rows(c,storage,0,start);
    rows(c,2,0,135.5,false); // default transient helper/API and disabled outer path
    rows(c,2,1,690.5);rows(c,2,2,690.5); // exact public weights remain original under both numeric routes
    for(const double bandwidth:{12.,30.}) {
        auto small_chip_config=c;small_chip_config.bandwidth_hz=bandwidth;PatternCode geometry(small_chip_config,small_chip_config.stream_epoch);
        check(geometry.chip_samples()==(bandwidth==12.?10U:4U),"small-chip fixture geometry changed");
        rows(small_chip_config,2,0,.25);rows(small_chip_config,2,0,32768.); // unnormalized edge and translated public Plan
    }
    auto partial=c;partial.integration_seconds=std::nextafter(16.0005,0.);PatternCode p(partial,partial.stream_epoch);
    check(p.symbol_samples()%p.chip_samples()!=0,"partial-tail fixture lost extent");
    rows(partial,2,0,p.symbol_samples()-31.75);lifetimes(c);
    std::cout<<"retained outer original pulse/private ring/current-carry/partial/mask/native/failure/join lifetimes passed; temporal_bytes="
             <<sizeof(Batch)<<";local_lease="<<sizeof(Batch::PlanCache::Lease)<<";shared_lease="<<sizeof(Batch::SharedPlanCache::Lease)<<'\n';
}
