#include "../src/pattern_shared_source_curve.hpp"
#include "../src/pattern_projection_cache.hpp"
#include "datapump/pattern_correlator.hpp"
#include "datapump/crypto.hpp"
#include "datapump/sched_hop.hpp"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <condition_variable>
using namespace datapump;
using namespace datapump::modem;
using namespace datapump::modem::detail;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F>void rejects(F&& f,const char* message){try{f();}catch(const Error&){return;}throw std::runtime_error(message);}
using Pool=SharedSourceCurves<double,2>;
Config config(unsigned factor=1) {
    Config c;c.sample_rate=6000;c.bandwidth_hz=2;c.integration_seconds=16;
    c.scramble=true;c.dsss_factor=factor;c.outer_dsss_version=OuterDsssVersion::interleaved_v2;
    Bytes root(32);for(unsigned i=0;i<root.size();++i)root[i]=static_cast<std::uint8_t>(i+53);
    c.data_key=Crypto(root);
    const auto p=c.data_key->stream(StreamPurpose::Scrambler,0,0,32);
    const auto d=c.data_key->stream(StreamPurpose::Dsss,0,0,32);
    std::copy(p.begin(),p.end(),c.spreading_seed.begin());std::copy(d.begin(),d.end(),c.dsss_seed.begin());
    return c;
}
void fill(Pool::Knots& knots,double marker) {
    for(std::size_t k=0;k<knots.size();++k)for(unsigned b=0;b<2;++b)
        knots[k][b]={marker+static_cast<double>(k),marker+3*b};
}
void check_value(const Pool::Lease& lease,double marker) {
    check(bool(lease),"missing retained curve lease");
    check(lease.knots()[0][0]==std::complex<double>{marker,marker} &&
          lease.knots()[256][1]==std::complex<double>{marker+256,marker+3},"pinned private curve was changed");
}
struct Ledger {
    std::size_t charged=0,peak=0,releases=0;
    CurveAccounting callbacks() {
        return {this,[](void* v,std::size_t bytes){auto& l=*static_cast<Ledger*>(v);l.charged+=bytes;l.peak=std::max(l.peak,l.charged);return true;},
            [](void* v,std::size_t bytes){auto& l=*static_cast<Ledger*>(v);check(l.charged>=bytes,"ledger underflow");l.charged-=bytes;++l.releases;}};
    }
};
void test_identity_and_cross_parent() {
    for(const auto factor:{1U,10U,100U,1000U})for(const bool scheduled:{false,true}) {
        auto c=config(factor);
        if(scheduled)c.scheduled_hopping=std::make_shared<SchedHopPlan>(SchedHopPlan{10,1});
        std::uint64_t epoch=100,phase=0;
        if(scheduled) {
            const SchedHopSourceMap map(*c.scheduled_hopping,*c.data_key,c.sample_rate);
            const auto physical=map.source_to_physical({100*map.source_samples_per_group(),0});
            epoch=physical.sample/c.sample_rate;phase=physical.sample%c.sample_rate;
        }
        c.stream_phase_samples=phase;
        const auto family=source_family(c,7);
        const auto resolved=symbol_stream_address(c,epoch,phase,1);
        const auto a=source_key_from_config(family,c,epoch,phase,1,10);
        auto later=c;later.stream_phase_samples=resolved.sample_in_second;
        const auto b=source_key_from_config(family,later,resolved.epoch,resolved.sample_in_second,0,10);
        check(a==b,"different parent labels failed to resolve the same private symbol");
        PatternCode first(c,epoch),second(later,resolved.epoch);
        first.prepare_symbol(first.chips_per_symbol());second.prepare_symbol(0);
        Pool pool(128*1024);unsigned builds=0;
        auto row=pool.get(a,[&](auto& knots){++builds;for(std::size_t k=0;k<knots.size();++k)
            knots[k]=first.shaped_values(first.chips_per_symbol(),static_cast<double>((10.L+k/256.L)*first.chip_samples()));});
        auto hit=pool.get(b,[&](auto&){++builds;throw Error("cross-parent cache miss");});
        check(builds==1 && pool.statistics().hits==1,"cross-parent exact curve was regenerated");
        for(std::size_t k=0;k<row.knots().size();++k) {
            const auto expected=second.shaped_values(0,static_cast<double>((10.L+k/256.L)*second.chip_samples()));
            check(row.knots()[k]==expected && hit.knots()[k]==expected,"canonical shared curve changed private0/1 samples");
        }
        auto independent=b;independent.family.key_namespace=8;
        check(!(independent==b),"selected keys shared one namespace");
        independent=b;independent.family.pattern_root[0]^=1;
        check(!(independent==b),"changed purpose root matched a private template");
        independent=b;++independent.ordinal;check(!(independent==b),"next private bit reused the preceding row");
        independent=b;++independent.epoch;check(!(independent==b),"changed epoch reused the preceding row");
        independent=b;++independent.local_chip;check(!(independent==b),"changed source chip reused the preceding row");
        independent=b;++independent.family.symbol_samples;check(!(independent==b),"final-partial geometry reused full-symbol row");
        independent=b;++independent.family.dsss_factor;check(!(independent==b),"DSSS factor reused another row");
        independent=b;independent.operand_bits=32;check(!(independent==b),"arithmetic representations shared one identity");
        rejects([&]{(void)pool.get(independent,[](auto&){});},"native operand mismatch was accepted");
        std::array<std::complex<double>,2> sample;
        const auto chip=first.chip_samples();
        check(!Pool::evaluate(row,(10.L+127.25L/256)*chip,sample) &&
              !Pool::evaluate(row,(10.L+128.25L/256)*chip,sample),"finite support discontinuity was interpolated");
        check(Pool::evaluate(row,(10.L+40.25L/256)*chip,sample),"eligible interior curve could not be read");
    }
    auto c=config();const auto f=source_family(c,1);auto d=c;d.dsss_seed[0]^=1;
    d=c;d.carrier_hz=1600;
    check(source_family(d,1)==f,"physical carrier was incorrectly included in source-only identity");
    for(const auto precision:{SearchArithmetic::default_mode,SearchArithmetic::fp32,SearchArithmetic::fp64}) {
        d=c;d.search_arithmetic=precision;
        check(source_family(d,1)==f,"requested arithmetic changed actual FP64 source identity");
        check(curve_sharing_allowed(d,true)==(precision!=SearchArithmetic::fp64) &&
              !curve_sharing_allowed(d,false),"FP64-force or parent fallback admitted interpolated shared curves");
    }
    d=c;d.dsss_seed[0]^=1;
    // DSSS-Off has no active DSSS root; changing an irrelevant seed is safe.
    check(source_family(d,1)==f,"unused DSSS seed incorrectly changed source identity");
    d.scramble=false;check(!(source_family(d,1)==f),"public/private domains were merged");
    rejects([&]{(void)source_key_from_config(f,d,100,0,0,1);},"stale source family was not rejected");
    c.bandwidth_hz=1200;c.integration_seconds=.4;
    auto plan=std::make_shared<FhssPlan>();plan->channels=2;plan->shifts[1]=1000;plan->dwell_seconds=.4;
    plan->power_ramps=plan->matched_gaps=true;c.fake_hopping=plan;
    const auto gate_family=source_family(c,1);
    check(gate_family.coefficient_omission,"diagnostic coefficient gate absent from family identity");
    auto changed=gate_family;changed.omission_lag+=1;
    check(!(changed==gate_family),"matched FHSS lag was omitted from identity");
    changed=gate_family;changed.omission_radius+=1;
    check(!(changed==gate_family),"matched FHSS coefficient radius was omitted from identity");
    auto phase_a=source_key(gate_family,{100,0,1},10),phase_b=source_key(gate_family,{100,0,2},10);
    check(!(phase_a==phase_b),"coefficient-omission sample phase was omitted");
    phase_a=source_key(f,{100,0,1},10);phase_b=source_key(f,{100,0,2},10);
    check(phase_a==phase_b,"post-shaper-only physical phase prevented valid source reuse");
}
void test_leases_eviction_and_cancellation() {
    auto c=config();const auto family=source_family(c,4);const auto a=source_key(family,{100,0,0},10);
    auto b=a;++b.local_chip;auto third=b;++third.local_chip;
    Ledger ledger;
    {
        Pool pool(128*1024,ledger.callbacks());check(pool.capacity()==2,"cache capacity changed");
        check(pool.working_bytes()==sizeof(pool)+ledger.charged && pool.working_bytes()<=128*1024,"shared allocation was not charged once");
        auto first=pool.get(a,[](auto& row){fill(row,1);});auto second=pool.get(b,[](auto& row){fill(row,2);});
        auto unavailable=pool.get(third,[](auto&){throw Error("pinned row overwritten");});
        check(!unavailable && pool.statistics().exhaustions==1,"pinned cache exhaustion did not fall back");
        check_value(first,1);check_value(second,2);check(!pool.try_reclaim(),"trim freed a pinned curve");
        first.reset();auto inserted=pool.get(third,[](auto& row){fill(row,3);});check_value(inserted,3);check_value(second,2);
        first=std::move(inserted);check_value(first,3);check(!inserted,"lease move failed");
        first=std::move(first);check_value(first,3);
        second=std::move(first);check_value(second,3);check(!first,"move assignment retained two lease owners");
        second.reset();
        auto failed=a;failed.ordinal=9;
        rejects([&]{(void)pool.get(failed,[](auto& row){fill(row,99);throw Error("injected partial fill");});},"partial construction unexpectedly succeeded");
        std::stop_source stop;
        rejects([&]{(void)pool.get(failed,[&](auto& row){fill(row,88);stop.request_stop();},stop.get_token());},"cancelled private row published");
        auto retry=pool.get(failed,[](auto& row){fill(row,4);});check_value(retry,4);
        check(pool.statistics().failed_builds==2,"failed row construction was not recorded");retry.reset();
        check(pool.try_reclaim() && ledger.charged==0 && ledger.releases==1,"unleased optional cache did not return its reservation");
    }
    check(ledger.charged==0,"cache destructor retained unleased storage");
    Pool::Lease survivor;
    {
        Pool pool(128*1024,ledger.callbacks());survivor=pool.get(a,[](auto& row){fill(row,5);});
        pool.retire();check(!pool.get(a,[](auto&){}),"retired key generation admitted a new cache read");
    }
    check(ledger.charged>0,"retired pinned backing was prematurely uncharged");check_value(survivor,5);
    survivor.reset();check(ledger.charged==0 && ledger.releases==2,"last lease did not cleanse/free/uncharge backing");
    using FloatPool=SharedSourceCurves<float,2>;
    FloatPool native_float(128*1024);auto float_key=a;float_key.operand_bits=32;
    auto float_row=native_float.get(float_key,[](auto& knots){for(auto& pair:knots)for(auto& value:pair)value={1.25F,-2.5F};});
    std::array<std::complex<double>,2> converted;
    check(FloatPool::evaluate(float_row,10.L*family.chip_samples,converted) &&
          converted[0]==std::complex<double>{1.25,-2.5},"native FP32 curve storage/evaluation was not executed");
    CurveAccounting refuse{&ledger,[](void*,std::size_t){return false;},[](void*,std::size_t){throw Error("unreserved cache was released");}};
    Pool disabled(128*1024,refuse);check(!disabled.capacity() && !disabled.get(a,[](auto&){}),"refused optional reservation changed fallback");
}
void test_concurrent_build_and_retire() {
    auto c=config();const auto key=source_key(source_family(c,3),{100,0,0},10);
    Ledger ledger;Pool pool(128*1024,ledger.callbacks());
    std::mutex mutex;std::condition_variable condition;bool entered=false,finish=false;
    std::thread worker([&]{auto lease=pool.get(key,[&](auto& row){
        {std::unique_lock lock(mutex);entered=true;condition.notify_one();condition.wait(lock,[&]{return finish;});}
        fill(row,9);
    });check(!lease,"retired in-flight row was published");});
    {std::unique_lock lock(mutex);condition.wait(lock,[&]{return entered;});}
    check(!pool.get(key,[](auto&){throw Error("duplicate fill");}) && pool.statistics().busy==1,"in-flight duplicate did not use local fallback");
    check(!pool.try_reclaim(),"reclamation freed an active row writer");pool.retire();
    {std::lock_guard lock(mutex);finish=true;}condition.notify_one();worker.join();
    check(pool.try_reclaim() && ledger.charged==0,"retired builder reservation did not drain");
}
// Partial-observation integration, not a full-bank sensitivity experiment.
// Identical original PCM reaches the public synchronous correlator path; a
// second parent must fill its L1 from the first parent's exact L2 rows.
void test_correlator_binding() {
    auto c=config();c.carrier_hz=1500;
    auto schedule=std::make_shared<SchedHopPlan>();schedule->time_multiplier=10;schedule->group_hops=2;
    schedule->clock_error_seconds=schedule->peer_clock_error_seconds=schedule->region_seconds=schedule->audio_error_seconds=0;
    schedule->ramp_seconds=.01;c.scheduled_hopping=schedule;
    const SchedHopSourceMap map(*schedule,*c.data_key,c.sample_rate);
    const auto physical=map.source_to_physical({100*map.source_samples_per_group(),0});
    c.stream_epoch=physical.sample/c.sample_rate;c.stream_phase_samples=physical.sample%c.sample_rate;
    PatternSearch search;search.start_offset_seconds=0;search.start_uncertainty_seconds=0;
    search.frequency_offsets_hz={0};search.clock_errors_ppm={0};search.drift_tolerant=false;search.worker_threads=1;
    PatternCorrelator a(c,search,16*1024*1024),b(c,search,16*1024*1024),reference(c,search,16*1024*1024);
    std::vector<float> input(2048);for(std::size_t i=0;i<input.size();++i)
        input[i]=static_cast<float>(.01*std::sin(static_cast<double>(i)*.23));
    SharedSourceCurves<> pool(256*1024);const auto family=source_family(c,17);
    CorrelationProjectionCache context(input,256*1024);
    context.bind_source_curves(&pool,&family);
    check(context.source_curves()==&pool && context.source_family()==&family,"prepared source binding changed");
    a.push(input,{},&context);const auto built=pool.statistics().builds;
    check(built>0,"eligible scheduled correlator never populated L2 source rows");
    b.push(input,{},&context);reference.push(input);
    check(pool.statistics().hits>0 && pool.statistics().builds==built,"second parent repeated source-curve construction");
    const auto compare=[&](const PatternCorrelator& x) {
        const auto actual=x.candidates(),expected=reference.candidates();
        check(actual.size()==expected.size(),"L2 reuse changed candidate count");
        for(std::size_t i=0;i<actual.size();++i) {
            const auto& lhs=actual[i];const auto& rhs=expected[i];
            check(lhs.first_sample==rhs.first_sample && lhs.end_sample==rhs.end_sample &&
                lhs.stream_symbol==rhs.stream_symbol && lhs.bit==rhs.bit && lhs.score==rhs.score &&
                lhs.alternative_score==rhs.alternative_score && lhs.admission_threshold==rhs.admission_threshold,
                "L2 reuse changed exact partial-observation evidence");
        }
        const auto partial=x.provisional(),original=reference.provisional();
        check(partial.bits==original.bits && partial.complete==original.complete,"L2 reuse changed pending/physical completion");
    };
    compare(a);compare(b);
    context.clear_source_curves();check(!context.source_curves() && !context.source_family(),"borrowed binding survived push scope");
    // Force mode may borrow the context but must not use its interpolated rows.
    auto forced=c;forced.search_arithmetic=SearchArithmetic::fp64;
    PatternCorrelator independent(forced,search,16*1024*1024);
    const auto before=pool.statistics();context.bind_source_curves(&pool,&family);independent.push(input,{},&context);
    const auto after=pool.statistics();
    check(after.hits==before.hits && after.builds==before.builds,"FP64-force consumed interpolated shared source rows");
    context.clear_source_curves();
}

}
int main(){try{test_identity_and_cross_parent();test_leases_eviction_and_cancellation();test_concurrent_build_and_retire();test_correlator_binding();
    std::cout<<"shared source-curve artifact tests passed\n";
}catch(const std::exception& e){std::cerr<<"shared source-curve artifact failure: "<<e.what()<<'\n';return 1;}}
