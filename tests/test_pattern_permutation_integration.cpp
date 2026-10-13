#include "../src/pattern_permutation_cache.hpp"
#include "datapump/sched_hop.hpp"
#include <array>
#include <cmath>
#include <ctime>
#include <iostream>
#include <vector>
using namespace datapump;
using namespace datapump::modem;
using namespace datapump::modem::detail;
namespace {
void check(bool b,const char* text){if(!b)throw Error(text);}
Config configuration(unsigned factor) {
    Config c;c.scramble=c.dsss=true;c.dsss_factor=factor;c.outer_dsss_version=OuterDsssVersion::interleaved_v2;
    c.bandwidth_hz=10;c.sample_rate=400*factor;c.carrier_hz=100*factor;c.spreading_factor=64;
    c.data_key.emplace(Bytes(32,0x57));
    for(std::size_t i=0;i<32;++i){c.spreading_seed[i]=static_cast<std::uint8_t>(3*i+7);c.dsss_seed[i]=static_cast<std::uint8_t>(5*i+11);}
    return c;
}
struct Snapshot {std::vector<std::array<std::complex<double>,2>> coefficients,shaped;TransmitTrace trace;};
Snapshot snapshot(Config c,std::uint64_t epoch,std::uint64_t index,PatternPermutationPools* pool,std::uint64_t ns) {
    PatternCode code(c,epoch);const auto first=index*code.chips_per_symbol();
    code.enable_transmit_trace(first);code.capture_transmit_trace(true);
    PatternPermutationScope scope(pool,ns);code.prepare_symbol(first);
    Snapshot out;out.coefficients.reserve(code.chips_per_symbol());
    for(std::uint64_t i=0;i<code.chips_per_symbol();++i)out.coefficients.push_back(code.values(first+i));
    for(const auto position:{0.L,.25L,.5L,1.L,7.25L,17.5L})
        out.shaped.push_back(code.shaped_values(first,static_cast<double>(position*code.chip_samples())));
    out.shaped.push_back(code.shaped_values(first,static_cast<double>(code.symbol_samples()-1)));
    code.copy_transmit_trace(out.trace,code.chips_per_symbol());return out;
}
void same(const Snapshot& a,const Snapshot& b) {
    check(a.coefficients==b.coefficients && a.shaped==b.shaped,"integrated cached PatternCode changed exact coefficients/shaping");
    check(a.trace.outer_dsss_key==b.trace.outer_dsss_key && a.trace.dsss_permutation_seed==b.trace.dsss_permutation_seed &&
          a.trace.outer_dsss_epoch==b.trace.outer_dsss_epoch && a.trace.outer_dsss_byte_offset==b.trace.outer_dsss_byte_offset &&
          a.trace.dsss_permutation_byte_offset==b.trace.dsss_permutation_byte_offset && a.trace.outer_dsss_first_pair==b.trace.outer_dsss_first_pair &&
          a.trace.pattern_input==b.trace.pattern_input && a.trace.pattern_output==b.trace.pattern_output && a.trace.dsss_key==b.trace.dsss_key,
          "integrated cache changed trace seed, rotations, private bytes or canonical address");
}
void exact_actual_code() {
    for(const auto factor:{10U,100U,1000U})for(const bool scheduled:{false,true})for(const bool partial:{false,true}) {
        auto c=configuration(factor);std::uint64_t epoch=1789312671;
        if(partial)c.integration_seconds=std::nextafter(static_cast<double>(symbol_sample_count(c)+pattern_chip_samples(c)/2)/c.sample_rate,0.);
        if(scheduled) {
            c.scheduled_hopping=std::make_shared<SchedHopPlan>(SchedHopPlan{10,1});
            const SchedHopSourceMap map(*c.scheduled_hopping,*c.data_key,c.sample_rate);
            const auto at=map.source_to_physical({100*map.source_samples_per_group(),0});epoch=at.sample/c.sample_rate;c.stream_phase_samples=at.sample%c.sample_rate;
        }
        const auto address=symbol_stream_address(c,epoch,c.stream_phase_samples,1);
        auto rebased=c;rebased.stream_phase_samples=address.sample_in_second;
        PatternPermutationPools pool(256*1024);
        const auto original=snapshot(c,epoch,1,nullptr,0),first=snapshot(c,epoch,1,&pool,7),alias=snapshot(rebased,address.epoch,0,&pool,7);
        same(original,first);same(first,alias);check(pattern_permutation_sharing_supported?pool.statistics().hits>0:pool.statistics().hits==0,"actual PatternCode canonical alias/fiber fallback violated cache policy");
        check(pool.allocated_bytes()<=pool.working_bytes() && pool.working_bytes()<=256*1024,"integrated pools exceeded aggregate reservation");
        const auto before=pool.statistics().hits;
        auto changed=c;changed.dsss_seed[0]^=1;(void)snapshot(changed,epoch,1,&pool,7);
        check(pool.statistics().hits==before,"changed exact DSSS root reused another permutation");
        (void)snapshot(c,epoch,1,&pool,8);check(pool.statistics().hits==before,"selected-key namespaces merged");
        pool.reclaim_at_barrier();const auto retired=pool.statistics().publications;
        same(original,snapshot(c,epoch,1,&pool,7));check(pool.statistics().publications==retired,"retired context refilled");
    }
}
void exact_residual_reservation() {
    const auto c=configuration(10);const auto address=symbol_stream_address(c,100,0,0);
    const auto key=*permutation_key(c,7,address);const auto n=key.complete_chips;
    const auto base=sizeof(PatternPermutationPools),minimum=PermutationSnapshotCache::minimum_bytes(n);
    std::vector<std::uint32_t> row(n);std::array<std::uint8_t,32> seed{};
    const auto build=[](std::span<std::uint32_t> target,auto& secret) {
        for(std::size_t i=0;i<target.size();++i)target[i]=static_cast<std::uint32_t>(i);
        secret.fill(0x39);
    };
    PatternPermutationPools below(base+minimum-1);
    below.copy_or_build(key,row,seed,build);
    check(below.allocated_bytes()==base && below.statistics().publications==0,
        "residual budget allocated a child before its full footprint fit");
    PatternPermutationPools exact(base+minimum);
    exact.copy_or_build(key,row,seed,build);exact.copy_or_build(key,row,seed,build);
    check(exact.statistics().publications==1 && exact.statistics().hits==1 &&
        exact.allocated_bytes()<=exact.working_bytes(),"exact child reservation boundary failed");
}

struct Bench {double total=0,construction=0,prepare=0,checksum=0;std::uint64_t hits=0,builds=0;std::size_t bytes=0;};
Bench bench(unsigned factor,bool cached) {
    const auto c=configuration(factor);constexpr std::uint64_t epoch=1789312671;
    PatternPermutationPools pool(cached?256*1024:0);Bench out;const auto start=std::clock();
    for(unsigned parent=0;parent<16;++parent) {
        const auto construction=std::clock();PatternCode code(c,epoch);out.construction+=double(std::clock()-construction)/CLOCKS_PER_SEC;
        const auto prepare=std::clock();{PatternPermutationScope scope(cached?&pool:nullptr,7);code.prepare_symbol(0);}
        out.prepare+=double(std::clock()-prepare)/CLOCKS_PER_SEC;out.checksum+=std::norm(code.values(0)[0]);
    }
    out.total=double(std::clock()-start)/CLOCKS_PER_SEC;const auto stats=pool.statistics();out.hits=stats.hits;out.builds=stats.private_builds;out.bytes=pool.working_bytes();return out;
}
void cancellation_and_scope() {
    auto c=configuration(10);PatternPermutationPools pool(256*1024);std::stop_source stop;stop.request_stop();
    PatternCode code(c,100);bool cancelled=false;
    {PatternPermutationScope scope(&pool,7);try{code.prepare_symbol(0,stop.get_token());}catch(const Error&){cancelled=true;}}
    check(cancelled && !pool.statistics().publications,"cancelled prepare_symbol published a permutation");
    check(!pattern_permutation_binding.pools && !pattern_permutation_binding.key_namespace,"scoped binding leaked to transmitter/other caller");
    const auto original=snapshot(c,100,0,nullptr,0);same(original,snapshot(c,100,0,&pool,0));
    check(!pool.statistics().publications,"namespace zero enabled sharing");
    auto short_c=c;short_c.bandwidth_hz=1280;short_c.sample_rate=64000;short_c.carrier_hz=16000;
    const auto a=symbol_stream_address(short_c,100,0,7),b=symbol_stream_address(short_c,100,0,8);
    check(a.epoch==b.epoch && a.ordinal==7 && b.ordinal==8,"short fixture did not derive nonzero ordinals");
    same(snapshot(short_c,100,7,nullptr,0),snapshot(short_c,100,7,&pool,7));
    same(snapshot(short_c,100,8,nullptr,0),snapshot(short_c,100,8,&pool,7));
}
}

void complete_map_request_accounting() {
    const auto c=configuration(10);auto key=*permutation_key(c,7,{100,0,0});
    std::vector<std::uint32_t> target(key.complete_chips);std::array<std::uint8_t,32> seed{};
    std::uint64_t actual_builds=0;
    const auto build=[&](auto row,auto& secret) {
        ++actual_builds;for(std::size_t i=0;i<row.size();++i)row[i]=static_cast<std::uint32_t>(i);
        secret.fill(0x39);
    };
    const auto base=sizeof(PatternPermutationPools);
    PatternPermutationPools bypass(base);
    bypass.copy_or_build(key,target,seed,build);
    const auto missing=bypass.request_statistics();
    check(missing.requests==1 && missing.hits==0 && missing.builder_attempts==1 &&
          missing.builder_completed==1 && missing.child_fallbacks==1 && actual_builds==1 &&
          bypass.statistics().private_builds==0 && bypass.allocated_bytes()==base,
          "complete counters omitted parent childless fallback or its actual bytes");
    PatternPermutationPools exact(base+PermutationSnapshotCache::minimum_bytes(key.complete_chips));
    exact.copy_or_build(key,target,seed,build);exact.copy_or_build(key,target,seed,build);
    const auto hit=exact.request_statistics();
    check(hit.requests==2 && hit.hits==1 && hit.builder_attempts==1 && hit.builder_completed==1 &&
          hit.child_fallbacks==0 && exact.row_capacity()==1 && exact.count_classes()==1 &&
          exact.allocated_bytes()<=exact.working_bytes(),"cold/hit aggregate accounting or exact quota failed");
    std::stop_source cancelled;cancelled.request_stop();bool rejected=false;
    try {exact.copy_or_build(key,target,seed,build,cancelled.get_token());}catch(const Error&){rejected=true;}
    check(rejected && exact.request_statistics().builder_attempts==1,
          "pre-cancelled map request invoked the private builder");
    ++key.epoch;bool failed=false;
    try {exact.copy_or_build(key,target,seed,[](auto,auto&){throw Error("intentional builder error");});}
    catch(const Error&){failed=true;}
    const auto error=exact.request_statistics();
    check(failed && error.requests==4 && error.builder_attempts==2 && error.builder_completed==1,
          "throwing private builder disappeared or was counted completed");
    exact.reclaim_at_barrier();const auto before_reclaim=exact.request_statistics();
    exact.copy_or_build(key,target,seed,build);
    const auto reclaimed=exact.request_statistics();
    check(reclaimed.requests==before_reclaim.requests+1 && reclaimed.child_fallbacks==1 &&
          reclaimed.builder_attempts==3 && reclaimed.builder_completed==2 && exact.row_capacity()==0 &&
          exact.allocated_bytes()==base && exact.working_bytes()==base,
          "joined reclaim refilled a retired cache or lost complete fallback counts");
    PatternPermutationPools interrupted(base+PermutationSnapshotCache::minimum_bytes(key.complete_chips));
    std::stop_source during;bool stopped=false;
    try {interrupted.copy_or_build(key,target,seed,[&](auto row,auto& secret){build(row,secret);during.request_stop();},during.get_token());}
    catch(const Error&){stopped=true;}
    const auto stop_stats=interrupted.request_statistics();
    check(stopped && stop_stats.builder_attempts==1 && stop_stats.builder_completed==1 &&
          interrupted.statistics().publications==0,"post-builder cancellation published a row or misreported returned builder");
    // Count cap is unchanged. Large legal wire geometry uses the original
    // private generator; complete counters still expose that bypass.
    auto large=key;large.complete_chips=65537;std::vector<std::uint32_t> larger(large.complete_chips);
    PatternPermutationPools capped(8*1024*1024);capped.copy_or_build(large,larger,seed,build);
    check(capped.request_statistics().child_fallbacks==1 && capped.row_capacity()==0 &&
          capped.allocated_bytes()==base,"ordinary 65536-chip cache eligibility bound changed");
    // Deterministic LRU churn: fill limit+1 distinct canonical keys, then seek
    // the first. Identity includes exact epoch; original output stays unchanged.
    auto small=key;small.complete_chips=32;std::vector<std::uint32_t> row(32);
    PatternPermutationPools churn(8*1024*1024);
    for(std::size_t i=0;i<=permutation_cache_max_rows;++i) {
        small.epoch=100+i;churn.copy_or_build(small,row,seed,build);
    }
    small.epoch=100;churn.copy_or_build(small,row,seed,build);
    check(churn.row_capacity()==permutation_cache_max_rows && churn.statistics().evictions>=2 &&
          churn.request_statistics().hits==0 && churn.allocated_bytes()<=churn.working_bytes(),
          "bounded row-cap/LRU experiment changed identity or allocation semantics");
    const auto last=churn.request_statistics();churn.copy_or_build(small,row,seed,build);
    check(churn.request_statistics().hits==last.hits+1 && row.front()==0 && row.back()==31 && seed[0]==0x39,
          "exact retained row failed after canonical working-set churn");
    std::cout<<"PERMUTATION_REQUEST_ACCOUNTING rows="<<permutation_cache_max_rows
             <<" bank_limit="<<permutation_bank_byte_limit<<" pool_fixed="<<base
             <<" churn_allocated="<<churn.allocated_bytes()<<" cap="<<churn.row_capacity()<<'\n';
}

int main(){try {
    exact_actual_code();cancellation_and_scope();exact_residual_reservation();complete_map_request_accounting();
    for(const auto factor:{10U,100U,1000U}) {
        const auto direct=bench(factor,false),cached=bench(factor,true);
        check(direct.checksum==cached.checksum && cached.hits==(pattern_permutation_sharing_supported?15:0),"conditional sixteen-parent benchmark changed code or failed exact reuse");
        std::cout<<"PERMUTATION_STAGE factor="<<factor<<" parents=16 direct_cpu="<<direct.total<<" shared_cpu="<<cached.total
            <<" direct_construct_cpu="<<direct.construction<<" shared_construct_cpu="<<cached.construction
            <<" direct_prepare_cpu="<<direct.prepare<<" shared_prepare_cpu="<<cached.prepare
            <<" hits="<<cached.hits<<" private_builds="<<cached.builds<<" optional_bytes="<<cached.bytes<<'\n';
    }
    std::cout<<"actual PatternCode permutation integration checks passed; conditional stage benchmark only\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
