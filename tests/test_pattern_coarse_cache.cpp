// Static proposal only: root owns compilation/runtime and full qualification.
#include "../src/pattern_permutation_cache.hpp"
#include "datapump/sched_hop.hpp"
#include "datapump/fhss_schedule.hpp"
#include <bit>
#include <iostream>
#include <memory>
#include <vector>
using namespace datapump;
using namespace datapump::modem;
using namespace datapump::modem::detail;
namespace {
void check(bool b,const char* message){if(!b)throw Error(message);}
bool bits(double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);}
bool bits(std::complex<double> a,std::complex<double> b){return bits(a.real(),b.real())&&bits(a.imag(),b.imag());}
InnerCoefficientKey key() {
    InnerCoefficientKey k;k.key_namespace=7;k.pattern_epoch=k.layer_epoch=1789312671;
    k.position=193;k.factor=100;k.layer_present=1;
    for(std::size_t i=0;i<32;++i){k.pattern_root[i]=3*i+7;k.dsss_root[i]=5*i+11;}return k;
}
void cache_contract() {
    auto cache=std::make_unique<InnerCoefficientCache>();auto k=key();std::uint64_t builds=0;
    const auto original=[&]{++builds;return std::complex<double>{.375,-.625};};
    const auto first=cache->copy_or_build(k,original),hit=cache->copy_or_build(k,original);
    check(bits(first,{.375,-.625})&&bits(first,hit)&&builds==1,
          "inner miss cleanup altered returned value or cold/hit builder count");
    const auto different=[&](auto change) {
        auto q=k;change(q);const auto before=cache->statistics();cache->copy_or_build(q,original);
        check(cache->statistics().hits==before.hits && builds==before.builder_attempts+1,
              "full private identity change reused another inner coefficient");
    };
    different([](auto& q){++q.key_namespace;});different([](auto& q){++q.pattern_epoch;});
    different([](auto& q){++q.layer_epoch;});different([](auto& q){++q.position;});
    different([](auto& q){q.pattern_root[31]^=1;});different([](auto& q){q.dsss_root[31]^=1;});
    different([](auto& q){q.factor=10;});different([](auto& q){q.factor=1000;});
    different([](auto& q){q.bit=1;q.pattern_domain=q.layer_domain=StreamDomain::PatternOneV2;});
    different([](auto& q){q.layer_present=0;});
    const auto invalid=[&](auto change) {
        auto q=k;change(q);const auto before=cache->statistics();cache->copy_or_build(q,original);
        check(cache->statistics().hits==before.hits && cache->statistics().publications==before.publications,
              "unsupported inner identity was admitted to cache");
    };
    invalid([](auto& q){q.key_namespace=0;});invalid([](auto& q){q.bit=2;});
    invalid([](auto& q){q.algorithm_revision=2;});invalid([](auto& q){q.outer_version=1;});
    invalid([](auto& q){q.pattern_purpose=StreamPurpose::Data;});
    invalid([](auto& q){q.layer_domain=StreamDomain::Payload;});
    invalid([](auto& q){q.position=std::numeric_limits<std::uint64_t>::max();});
    // Four-way bounded replacement; exact full identity remains authoritative
    // when roots deliberately collide in the scalar-only bucket selector.
    const auto before_evict=cache->statistics().evictions;
    for(unsigned i=0;i<6;++i){auto q=k;q.pattern_root[0]=static_cast<std::uint8_t>(91+i);cache->copy_or_build(q,original);}
    check(cache->statistics().evictions>before_evict,"four-way coefficient set did not exercise bounded replacement");
    std::stop_source stop;stop.request_stop();bool cancelled=false;const auto before_stop=builds;
    try{cache->copy_or_build(k,original,stop.get_token());}catch(const Error&){cancelled=true;}
    check(cancelled&&builds==before_stop,"pre-cancelled coefficient request invoked original builder");
    auto late=std::make_unique<InnerCoefficientCache>();std::stop_source late_stop;cancelled=false;
    try{late->copy_or_build(k,[&]{late_stop.request_stop();return original();},late_stop.get_token());}catch(const Error&){cancelled=true;}
    check(cancelled&&!late->statistics().publications,"post-builder cancellation published inner coefficient");
    bool failed=false;try{late->copy_or_build(k,[]{throw Error("intentional inner builder error");return std::complex<double>{};});}catch(const Error&){failed=true;}
    check(failed&&!late->statistics().publications,"throwing inner builder published a row");
    // Calling retirement FROM original builder proves builder is outside lock;
    // retirement also prevents that already in-flight miss from refilling.
    auto retired=std::make_unique<InnerCoefficientCache>();
    const auto result=retired->copy_or_build(k,[&]{check(retired->try_reclaim(),"original inner builder ran under cache mutex");return original();});
    retired->copy_or_build(k,original);
    check(bits(result,{.375,-.625})&&!retired->statistics().publications&&!retired->statistics().hits,
          "retired cache refilled or changed original private output");
    const auto round=std::fegetround();const auto before_round=cache->statistics();
    check(std::fesetround(FE_UPWARD)==0,"unable to install unsupported rounding fixture");
    (void)cache->copy_or_build(k,original);
    check(std::fesetround(round)==0,"unable to restore original rounding fixture");
    check(cache->statistics().hits==before_round.hits && cache->statistics().publications==before_round.publications,
          "cross-worker arithmetic mismatch reused or published an inner double");
}
Config configuration(unsigned factor) {
    Config c;c.scramble=c.dsss=true;c.dsss_factor=factor;c.outer_dsss_version=OuterDsssVersion::interleaved_v2;
    c.bandwidth_hz=10;c.sample_rate=400*factor;c.carrier_hz=100*factor;c.spreading_factor=64;
    c.data_key.emplace(Bytes(32,0x57));
    for(std::size_t i=0;i<32;++i){c.spreading_seed[i]=3*i+7;c.dsss_seed[i]=5*i+11;}return c;
}
struct Snapshot {std::vector<std::array<std::complex<double>,2>> values,shaped;TransmitTrace trace;};
Snapshot snapshot(Config c,std::uint64_t epoch,std::uint64_t index,PatternPermutationPools* pool,
        std::uint64_t ns,bool capture=false) {
    PatternCode code(c,epoch);const auto first=index*code.chips_per_symbol();
    if(capture){code.enable_transmit_trace(first);code.capture_transmit_trace(true);}
    PatternPermutationScope scope(pool,ns);code.prepare_symbol(first);Snapshot out;
    for(std::uint64_t i=0;i<code.chips_per_symbol();++i)out.values.push_back(code.values(first+i));
    for(const auto at:{0.,.25,1.,7.25,17.5})out.shaped.push_back(code.shaped_values(first,at*code.chip_samples()));
    out.shaped.push_back(code.shaped_values(first,static_cast<double>(code.symbol_samples()-1)));
    if(capture)code.copy_transmit_trace(out.trace,code.chips_per_symbol());return out;
}
void same(const Snapshot& a,const Snapshot& b) {
    check(a.values.size()==b.values.size()&&a.shaped.size()==b.shaped.size(),"inner cache changed output extent");
    for(std::size_t i=0;i<a.values.size();++i)for(unsigned bit=0;bit<2;++bit)
        check(bits(a.values[i][bit],b.values[i][bit]),"inner cache changed private coefficient/rotation/gate bits");
    for(std::size_t i=0;i<a.shaped.size();++i)for(unsigned bit=0;bit<2;++bit)
        check(bits(a.shaped[i][bit],b.shaped[i][bit]),"inner cache changed original shaped sum bits");
    check(a.trace.pattern_key==b.trace.pattern_key&&a.trace.outer_dsss_first_pair==b.trace.outer_dsss_first_pair&&
        a.trace.pattern_input==b.trace.pattern_input&&a.trace.pattern_output==b.trace.pattern_output&&
        a.trace.dsss_key==b.trace.dsss_key&&a.trace.outer_dsss_key==b.trace.outer_dsss_key&&
        a.trace.dsss_permutation_seed==b.trace.dsss_permutation_seed&&a.trace.outer_dsss_epoch==b.trace.outer_dsss_epoch&&
        a.trace.outer_dsss_byte_offset==b.trace.outer_dsss_byte_offset&&a.trace.dsss_permutation_byte_offset==b.trace.dsss_permutation_byte_offset,
        "inner cache changed captured private bytes/canonical trace metadata");
}
void actual_pattern_and_budget() {
    constexpr std::size_t maps=256*1024;const auto required=sizeof(PatternPermutationPools)+sizeof(InnerCoefficientCache)+maps;
    PatternPermutationPools small(required-1,permutation_cache_max_rows,true),exact(required,permutation_cache_max_rows,true),off(required,permutation_cache_max_rows,false);
    check(!small.has_inner_coefficient_cache()&&exact.has_inner_coefficient_cache()&&!off.has_inner_coefficient_cache()&&
        exact.allocated_bytes()==sizeof(PatternPermutationPools)+sizeof(InnerCoefficientCache)&&
        exact.allocated_bytes()+maps==exact.working_bytes(),"inner factory displaced original256KiB or mischarged actual sizeof");
    for(const auto factor:{10U,100U,1000U})for(const bool scheduled:{false,true})for(const bool partial:{false,true}) {
        auto c=configuration(factor);std::uint64_t epoch=1789312671;
        if(partial)c.integration_seconds=std::nextafter(static_cast<double>(symbol_sample_count(c)+pattern_chip_samples(c)/2)/c.sample_rate,0.);
        if(scheduled){c.scheduled_hopping=std::make_shared<SchedHopPlan>(SchedHopPlan{10,1});
            const SchedHopSourceMap map(*c.scheduled_hopping,*c.data_key,c.sample_rate);
            const auto at=map.source_to_physical({100*map.source_samples_per_group(),0});epoch=at.sample/c.sample_rate;c.stream_phase_samples=at.sample%c.sample_rate;}
        PatternPermutationPools pool(required,permutation_cache_max_rows,true);
        const auto original=snapshot(c,epoch,1,nullptr,0),cold=snapshot(c,epoch,1,&pool,7),warm=snapshot(c,epoch,1,&pool,7);
        same(original,cold);same(cold,warm);
        check(pattern_permutation_sharing_supported?pool.inner_coefficient_statistics().hits>0:pool.inner_coefficient_statistics().requests==0,
              "actual private V2 coefficients never reused across independent PatternCodes");
        const auto address=symbol_stream_address(c,epoch,c.stream_phase_samples,1);auto rebased=c;rebased.stream_phase_samples=address.sample_in_second;
        same(original,snapshot(rebased,address.epoch,0,&pool,7));
        const auto isolated=[&](Config changed,std::uint64_t ns) {
            const auto before=pool.inner_coefficient_statistics().hits;
            same(snapshot(changed,epoch,1,nullptr,0),snapshot(changed,epoch,1,&pool,ns));
            check(pool.inner_coefficient_statistics().hits==before,"changed private root/namespace reused inner row");
        };
        auto changed=c;changed.spreading_seed[31]^=1;isolated(changed,7);
        changed=c;changed.dsss_seed[31]^=1;isolated(changed,7);isolated(c,8);
        const auto before_trace=pool.inner_coefficient_statistics().requests;
        same(snapshot(c,epoch,1,nullptr,0,true),snapshot(c,epoch,1,&pool,7,true));
        check(pool.inner_coefficient_statistics().requests==before_trace,"trace-captured PatternCode consulted shared inner cache");
        auto legacy=c;legacy.outer_dsss_version=OuterDsssVersion::legacy_v1;const auto before_legacy=pool.inner_coefficient_statistics().requests;
        same(snapshot(legacy,epoch,1,nullptr,0),snapshot(legacy,epoch,1,&pool,7));
        check(pool.inner_coefficient_statistics().requests==before_legacy,"unsupported V1 source used V2 inner cache");
        check(pool.allocated_bytes()<=pool.working_bytes(),"map+coefficient caches exceeded original parent grant");
        pool.reclaim_at_barrier();check(!pool.has_inner_coefficient_cache()&&pool.allocated_bytes()==sizeof(PatternPermutationPools),
              "joined pool reclaim retained private coefficient storage");
        same(original,snapshot(c,epoch,1,&pool,7));
    }
    // Original canonical stream ordinals within one epoch remain distinct.
    auto short_c=configuration(10);short_c.sample_rate=64000;short_c.carrier_hz=16000;short_c.bandwidth_hz=1280;
    const auto a7=symbol_stream_address(short_c,100,0,7),a8=symbol_stream_address(short_c,100,0,8);
    check(a7.epoch==a8.epoch&&a7.ordinal==7&&a8.ordinal==8,"nonzero-ordinal fixture lost canonical addressing");
    PatternPermutationPools ordinals(required,permutation_cache_max_rows,true);
    same(snapshot(short_c,100,7,nullptr,0),snapshot(short_c,100,7,&ordinals,7));
    const auto ordinal_hits=ordinals.inner_coefficient_statistics().hits;
    same(snapshot(short_c,100,8,nullptr,0),snapshot(short_c,100,8,&ordinals,7));
    check(ordinals.inner_coefficient_statistics().hits==ordinal_hits,"neighboring canonical ordinal reused inner coefficient");
    const auto pcm=[&](PatternPermutationPools* pool) {
        PatternTransmitter tx({0,1,0},short_c,100,0,false);
        check(tx.total_samples()<32768,"bounded exact PCM fixture exceeded original extent");
        std::vector<float> out(tx.total_samples());PatternPermutationScope scope(pool,pool?7:0);
        for(std::size_t at=0;at<out.size();) {
            const auto count=tx.read(std::span(out).subspan(at,std::min<std::size_t>(503,out.size()-at)));
            check(count>0,"original PCM fixture stopped before its physical end");at+=count;
        }
        return out;
    };
    const auto original_pcm=pcm(nullptr),cached_pcm=pcm(&ordinals);
    check(original_pcm.size()==cached_pcm.size(),"inner cache changed exact transmitted PCM extent");
    for(std::size_t i=0;i<original_pcm.size();++i)
        check(std::bit_cast<std::uint32_t>(original_pcm[i])==std::bit_cast<std::uint32_t>(cached_pcm[i]),
              "inner cache changed original limiter/carrier/transmitted PCM bits");
    // Cache owns PRE-gate coefficients. Both actual omitted and retained fine
    // chips must still run the original matched-FHSS physical coefficient gate.
    auto gated=configuration(100);auto hop=std::make_shared<FhssPlan>();
    hop->channels=2;hop->shifts[0]=0;hop->shifts[1]=100;hop->dwell_seconds=.4;
    hop->power_ramps=hop->matched_gaps=true;gated.fake_hopping=hop;
    PatternPermutationPools gates(required,permutation_cache_max_rows,true);
    const auto gated_reference=snapshot(gated,100,0,nullptr,0),gated_cold=snapshot(gated,100,0,&gates,7);
    same(gated_reference,gated_cold);same(gated_reference,snapshot(gated,100,0,&gates,7));
    std::size_t zero=0,nonzero=0;
    for(const auto& value:gated_reference.values)(value[0]==std::complex<double>{}?zero:nonzero)++;
    check(zero&&nonzero,"matched-FHSS negative/positive gate fixture was not exercised");
    check(pattern_permutation_sharing_supported?gates.inner_coefficient_statistics().hits>0:gates.inner_coefficient_statistics().requests==0,
          "matched-gaps fixture did not exercise pooled pre-gate coefficients");
    // A warmed pooled double must not replace original computation when this
    // executing worker has an unsupported arithmetic environment.
    auto rounding_config=configuration(10);PatternPermutationPools rounding(required,permutation_cache_max_rows,true);
    (void)snapshot(rounding_config,100,0,&rounding,7);const auto rounding_before=rounding.inner_coefficient_statistics();
    struct RestoreRound {int value=std::fegetround();~RestoreRound(){std::fesetround(value);}};
    {
        RestoreRound restore;check(std::fesetround(FE_UPWARD)==0,"cannot install actual-code rounding fixture");
        same(snapshot(rounding_config,100,0,nullptr,0),snapshot(rounding_config,100,0,&rounding,7));
    }
    check(rounding.inner_coefficient_statistics().hits==rounding_before.hits &&
          rounding.inner_coefficient_statistics().publications==rounding_before.publications,
          "actual unsupported-fenv PatternCode consumed or published pooled doubles");
    // Scalar bit0 must not generate/cache bit1 before its original request.
    auto c=configuration(100);PatternCode scalar(c,100);PatternPermutationPools one(required,permutation_cache_max_rows,true);
    {PatternPermutationScope scope(&one,7);(void)scalar.value(0,0);}
    check(pattern_permutation_sharing_supported?one.inner_coefficient_statistics().builder_attempts==1:one.inner_coefficient_statistics().requests==0,
          "single-bit request prematurely generated the alternative private row");
    std::cout<<"inner coefficient exact/private/lifetime/budget checks passed; cache_bytes="<<sizeof(InnerCoefficientCache)<<'\n';
}
}
int main(){try{
    if(!inner_coefficient_arithmetic_supported()){
        std::cout<<"inner coefficient positive native coverage omitted; unsupported arithmetic retains original builder\n";return 0;
    }
    cache_contract();actual_pattern_and_budget();return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
