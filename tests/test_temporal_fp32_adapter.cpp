#define DATAPUMP_ORDERED_PULSE_CONTRACT_OFF 1
#include "scheduled_temporal_templates.hpp"
#include "pattern_correlator_batch.hpp"
#include "datapump/crypto.hpp"
#include "datapump/sched_hop.hpp"
#include <bit>
#include <iostream>
#include <memory>
#include <vector>
using namespace datapump;
using namespace datapump::modem;
using namespace datapump::modem::detail;
using Batch=ScheduledTemporalTemplates;
using Value=std::array<std::complex<double>,2>;
static void check(bool value,const char* message){if(!value)throw Error(message);}
static bool bits(double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);}
static bool bits(std::complex<double> a,std::complex<double> b){return bits(a.real(),b.real())&&bits(a.imag(),b.imag());}
static Config configuration(SearchArithmetic arithmetic=SearchArithmetic::default_mode) {
    Config c;c.sample_rate=6000;c.carrier_hz=1500;c.bandwidth_hz=8;c.integration_seconds=16;
    c.pattern_symbols=true;c.constellation_bits=1;c.spreading_factor=64;c.scramble=c.dsss=true;c.dsss_factor=100;
    c.outer_dsss_version=OuterDsssVersion::interleaved_v2;c.data_key.emplace(Bytes(32,0x67));
    for(std::size_t i=0;i<c.spreading_seed.size();++i){c.spreading_seed[i]=3*i+7;c.dsss_seed[i]=5*i+11;}
    c.stream_epoch=1799999967;c.search_arithmetic=arithmetic;
    c.scheduled_hopping=std::make_shared<SchedHopPlan>(SchedHopPlan{10,1});return c;
}
static void prepare(Batch& b,PatternCode& code,const Config& c,std::uint64_t shift=0,std::stop_token stop={}) {
    const auto phase=(shift/15*17)%c.sample_rate,first_chip=(shift/15%7)*code.chips_per_symbol();
    b.begin(42,4096);b.start(phase,first_chip,4096);
    for(std::size_t n=0;n<512;++n)if(n%7)check(b.append(n,135.5+shift+n),"original selected raw exporter overflow");
    b.end(4608);check(b.prepare(code,c,stop),"temporal preparation unexpectedly rejected");
    code.set_stream_phase_samples(phase);code.prepare_symbol(first_chip); // exact caller restore
}
static void compare(Batch& batch,PatternCode& reference,const Config& c,std::uint64_t shift,bool exact) {
    const auto phase=(shift/15*17)%c.sample_rate,first_chip=(shift/15%7)*reference.chips_per_symbol();
    reference.set_stream_phase_samples(phase);reference.prepare_symbol(first_chip);
    std::array<CorrelationFit,2> a{},b{};
    for(std::size_t n=0;n<512;++n) {
        Value value;const auto within=135.5+shift+n;
        const bool found=batch.raw(42,4096,phase,first_chip,4096+n,n,within,value);
        check(found==(n%7!=0),"prepared precision path changed exact selected physical rows");if(!found)continue;
        const auto original=reference.shaped_values(first_chip,within);
        const auto carrier=std::polar(1.,.317*n);const auto x=static_cast<double>(static_cast<float>(.6*std::sin(.27*n)));
        const CorrelationProjection p{x*carrier.real(),x*carrier.imag(),carrier.real()*carrier.real(),carrier.imag()*carrier.imag(),carrier.real()*carrier.imag(),x*x};
        for(unsigned bit=0;bit<2;++bit) {
            if(exact)check(bits(value[bit],original[bit]),"original gathered FP64 refinement changed template bits");
            else check(std::abs(value[bit].real()-original[bit].real())<=1e-5 &&
                       std::abs(value[bit].imag()-original[bit].imag())<=1e-5,"native template exceeded requested component radius");
            const auto av=.73*pattern_limit_pcm(2.2*value[bit])/2.2,bv=.73*pattern_limit_pcm(2.2*original[bit])/2.2;
            if(exact)check(bits(av,bv),"original limiter order changed under refinement");
            CorrelationFit ca,cb;ca.add(p,av,1);cb.add(p,bv,1);
            const auto merge=[](auto& to,const auto& from){to.xc+=from.xc;to.xs+=from.xs;to.cc+=from.cc;to.ss+=from.ss;to.cs+=from.cs;to.energy+=from.energy;to.count+=from.count;};
            merge(a[bit],ca);merge(b[bit],cb);
        }
    }
    for(unsigned bit=0;bit<2;++bit) {
        check(bits(a[bit].energy,b[bit].energy)&&a[bit].count==b[bit].count,"original PCM energy/count changed");
        if(exact)check(bits(a[bit].xc,b[bit].xc)&&bits(a[bit].xs,b[bit].xs)&&bits(a[bit].cc,b[bit].cc)&&
            bits(a[bit].ss,b[bit].ss)&&bits(a[bit].cs,b[bit].cs)&&bits(a[bit].score(),b[bit].score()),"original wider Fit changed under refinement");
        else check(std::isfinite(a[bit].score()),"native template produced nonfinite wider score");
    }
    Value unused;
    check(!batch.raw(43,4096,phase,first_chip,4097,1,136.5+shift,unused) &&
          !batch.raw(42,4097,phase,first_chip,4097,1,136.5+shift,unused) &&
          !batch.raw(42,4096,phase+1,first_chip,4097,1,136.5+shift,unused) &&
          !batch.raw(42,4096,phase,first_chip+1,4097,1,136.5+shift,unused) &&
          !batch.raw(42,4096,phase,first_chip,4097,1,std::nextafter(136.5+shift,0.),unused),"precision path weakened output identity fences");
}
int main() {
    if(!Batch::supported()){std::cerr<<"native FP32 adapter coverage unavailable under executing ISA/fenv; omitted, not native pass\n";return 77;}
    for(const auto arithmetic:{SearchArithmetic::default_mode,SearchArithmetic::fp32}) {
        const auto c=configuration(arithmetic);PatternCode code(c,c.stream_epoch),reference(c,c.stream_epoch);
        auto batch=std::make_unique<Batch>();auto pool=std::make_unique<Batch::SharedPlanCache>();
        batch->bind_shared_plan_cache(pool.get());batch->allocate_fp32();
        for(const std::uint64_t shift:{0ULL,555ULL}) {
            prepare(*batch,code,c,shift);check(!pool->pinned(),"native facade retained exact public Plan lease");
            check(pool->try_clear(),"native output could not survive public exact Plan eviction");
            compare(*batch,reference,c,shift,false);
        }
        const auto counts=batch->fp32_counters();
        check(counts.native_tiles>0 && counts.consumed_native>0 && counts.packed_hits>0,"actual native execution/cache reuse missing");
        check(counts.numeric.native_samples==batch->statistics.numeric.output_pairs,"native output counter delta missing from existing Temporal numeric counters");
        check(counts.consumed_native==batch->statistics.consumed,"logical/native output consumption disagreed");
        check(batch->fp32_bytes()==Batch::fp32_charge,"optional actual sizeof not charged");
        std::stop_source cancelled;cancelled.request_stop();bool stopped=false;
        try{prepare(*batch,code,c,1110,cancelled.get_token());}catch(const Error&){stopped=true;}
        check(stopped&&!pool->pinned(),"native cancellation leaked a pinned public Plan");
        Value stale;const auto cancelled_chip=4*code.chips_per_symbol();
        check(!batch->raw(42,4096,1258,cancelled_chip,4097,1,1246.5,stale),"cancelled exact-identity native attempt exposed stale output");
        batch->drop_fp32();check(batch->fp32_bytes()==0,"optional precision payload not released");
    }
    {
        const auto c=configuration();PatternCode code(c,c.stream_epoch),reference(c,c.stream_epoch);
        auto batch=std::make_unique<Batch>();batch->allocate_fp32(0);prepare(*batch,code,c);
        compare(*batch,reference,c,0,true);const auto counts=batch->fp32_counters();
        check(counts.refined_tiles==1 && counts.native_tiles==0 && counts.consumed_refined>0,"zero-limit rejection did not execute original FP64 refinement");
        check(counts.numeric.error_fallback_samples>0,"local numerical fallback counter missing");
        check(batch->statistics.numeric.value_pairs>0 && counts.attempts==1 && batch->statistics.numeric.prepares==2,
              "refinement repeated original gather/context preparation");
    }
    {
        const auto c=configuration(SearchArithmetic::fp64);PatternCode code(c,c.stream_epoch),reference(c,c.stream_epoch);
        auto batch=std::make_unique<Batch>();batch->allocate_fp32();prepare(*batch,code,c);compare(*batch,reference,c,0,true);
        check(batch->fp32_counters().attempts==0,"FP64force entered native prepare/evaluate");
    }
    {
        // Cancellation AFTER a certified lease already exists exercises both
        // facade and original gather guards, then automatic consumed-pin release.
        const auto c=configuration();PatternCode code(c,c.stream_epoch);
        auto original=std::make_unique<Batch::Tile>();
        auto facade=std::make_unique<Batch::Fp32Execution>(*original);
        auto pool=std::make_unique<Batch::PlanCache>();Batch::Tile::Statistics numeric;
        std::array<double,16> key{};for(std::size_t i=0;i<key.size();++i)key[i]=135.25+.5*i;
        auto acquired=pool->acquire(key,code.symbol_samples(),code.chip_samples(),
            [&](auto& plan,auto at,std::uint64_t symbol,std::uint64_t chip,std::stop_token stop){return plan.build_original(at,symbol,chip,numeric,stop);});
        check(bool(acquired.lease),"certified cancellation lease unavailable");
        const Batch::Tile::Lane binding{&code,&c,0,0,acquired.lease.plan().first_local};
        facade->begin_request(key,code.symbol_samples(),code.chip_samples());
        std::stop_source cancel;cancel.request_stop();bool stopped=false;
        try{facade->template gather_evaluate_leased<512,Batch::PlanCache::capacity>(
            {&binding,1},std::move(acquired.lease),Batch::Tile::Backend::scalar,cancel.get_token());}
        catch(const Error&){stopped=true;}
        Value stale;check(stopped&&!pool->pinned()&&pool->try_clear()&&!facade->raw(0,stale)&&original->samples()==0,
                          "certified live-lease cancellation retained pin/binding/output");facade->end_request();
    }
    std::cout<<"native actual Temporal ownership/precision/refinement/identity/energy/limiter/Fit/cancel oracle passed; optional_bytes="
             <<Batch::fp32_charge<<" temporal_bytes="<<sizeof(Batch)<<" callee_reservation="<<Batch::callee_allowance<<'\n';
}
