#include "../src/pattern_projection_cache.hpp"
#include "datapump/pattern_correlator.hpp"
#include "datapump/pattern_pulse.hpp"
#include <iostream>
#include <random>
#include <vector>

using namespace datapump;
using namespace datapump::modem;
using namespace datapump::modem::detail;
namespace {
void check(bool value,const char* message){if(!value)throw Error(message);}
template<class Action> void rejects(Action action,const char* message){try{action();}catch(const Error&){return;}throw Error(message);}
bool equal(const CorrelationProjection& a,const CorrelationProjection& b) {
    return a.xc==b.xc && a.xs==b.xs && a.cc==b.cc && a.ss==b.ss && a.cs==b.cs && a.energy==b.energy;
}
void carrier_statistics_and_identity() {
    std::mt19937 random(9708);std::normal_distribution<float> noise(0,.3F);
    std::vector<float> samples(2048);for(auto& x:samples)x=noise(random);
    CorrelationProjectionCache cache(samples,256*1024);
    check(cache.enabled() && cache.working_bytes()<=256*1024,"cache exceeded its explicit byte ceiling");
    check(cache.covers(samples) && cache.covers(std::span(samples).subspan(31,128)),"immutable subspan was not covered");
    auto foreign=samples;check(!cache.covers(foreign),"different input storage reused a projection");
    std::size_t cases=0;
    for(const auto frequency:{0.,.005,.5,1500.,5999.999,6000.})for(const auto block_size:{32U,128U}) {
        const auto input=std::span(samples).subspan(7+cases*3,block_size);
        const auto local_sample=1000003ULL+cases*11;
        const auto value=cache.get(input,local_sample,12000,frequency,block_size,true);
        check(static_cast<bool>(value),"eligible immutable carrier row was not cached");
        // The original receiver oscillator restarts at this exact raw block
        // coordinate. Compare every covariance/energy entry as well as I/Q.
        CorrelationProjection expected;std::complex<double> moment;
        constexpr double tau=2*std::numbers::pi;
        auto oscillator=std::polar(1.,static_cast<double>(std::remainder(
            static_cast<long double>(local_sample)*tau*frequency/12000,static_cast<long double>(tau))));
        const auto rotation=std::polar(1.,tau*frequency/12000);
        check(equal(value.prefix[0],expected) && value.first_moment[0]==moment,"cached origin was not zero");
        for(std::size_t i=0;i<input.size();++i) {
            const auto x=static_cast<double>(input[i]),c=oscillator.real(),s=oscillator.imag();
            expected.xc+=x*c;expected.xs+=x*s;expected.cc+=c*c;expected.ss+=s*s;expected.cs+=c*s;expected.energy+=x*x;
            moment+=static_cast<double>(i)*std::complex<double>{x*c,x*s};
            check(equal(value.prefix[i+1],expected) && value.first_moment[i+1]==moment,
                  "shared projection changed exact original carrier statistics");
            oscillator*=rotation;
        }
        const auto computed=cache.computed_samples(),hits=cache.hits();
        const auto again=cache.get(input,local_sample,12000,frequency,block_size,true);
        check(again.prefix.data()==value.prefix.data() && cache.computed_samples()==computed && cache.hits()==hits+1,
              "identical public geometry repeated preprocessing");
        const auto shifted=cache.get(input,local_sample+1,12000,frequency,block_size,true);
        check(shifted && shifted.prefix.data()!=value.prefix.data(),"different receiver origins reused carrier phase");
        const auto other_rate=cache.get(input,local_sample,16000,frequency,block_size,true);
        check(other_rate && other_rate.prefix.data()!=value.prefix.data(),"different sample rate reused a row");
        ++cases;
    }
    const auto block=std::span(samples).first(32);
    const auto plain=cache.get(block,0,12000,.5,32,false);
    const auto weighted=cache.get(block,0,12000,.5,32,true);
    check(plain && plain.first_moment.empty() && weighted && !weighted.first_moment.empty(),"weighted row identity was lost");
    check(!cache.get(foreign,0,12000,.5,2048,true),"foreign PCM was prepared through a bank cache");
    check(!cache.get(block,0,12000,.5,16,true),"unsupported oscillator block was cached");
    CorrelationProjectionCache disabled(samples,1);check(!disabled.enabled() && !disabled.covers(samples),"tiny workspace enabled the cache");
    samples[0]=std::numeric_limits<float>::quiet_NaN();
    rejects([&]{CorrelationProjectionCache invalid(samples,256*1024);},"shared frontend accepted nonfinite input");
}
void bounded_storage_and_cancellation() {
    std::vector<float> samples(2048,.25F);
    CorrelationProjectionCache cache(samples,64*1024);
    const auto input=std::span(samples).first(128);
    const auto first=cache.get(input,0,12000,1500,128,true);
    check(static_cast<bool>(first),"bounded cache could not prepare its first row");
    const auto saved=first.prefix.back();const auto* address=first.prefix.data();
    unsigned misses=0;
    for(unsigned n=1;n<300;++n)if(!cache.get(input,n*128ULL,12000,1500,128,true))++misses;
    check(misses && cache.working_bytes()<=64*1024,"cache exhaustion allocated beyond its ceiling");
    const auto again=cache.get(input,0,12000,1500,128,true);
    check(again && again.prefix.data()==address && equal(again.prefix.back(),saved),"new rows invalidated an earlier immutable view");
    const auto computed=cache.computed_samples(),hits=cache.hits();
    std::stop_source cancel;cancel.request_stop();
    rejects([&]{cache.get(input,0,12000,1500,128,true,cancel.get_token());},"cache hit bypassed cancellation");
    check(cache.computed_samples()==computed && cache.hits()==hits,"cancelled lookup published work");
    CorrelationProjectionCache indexed(samples,256*1024);
    const auto singleton=std::span(samples).first(1);
    for(unsigned n=0;n<128;++n)check(bool(indexed.get(singleton,n,12000,1500,32,true)),
                                  "bounded index filled before its documented row capacity");
    check(!indexed.get(singleton,128,12000,1500,32,true) && indexed.computed_samples()==128,
          "bounded index grew beyond its row capacity");
    CorrelationProjectionCache retry(samples,64*1024);
    rejects([&]{retry.get(input,391,12000,.5,128,true,cancel.get_token());},"cancelled cache miss completed");
    check(retry.computed_samples()==0 && retry.hits()==0 &&
          bool(retry.get(input,391,12000,.5,128,true)),"cancelled miss corrupted the uncancelled retry");
    // A reused PCM buffer in a later bank push must have a fresh lifetime.
    samples[0]=-.75F;
    CorrelationProjectionCache next(samples,64*1024);
    const auto new_value=next.get(input,0,12000,1500,128,true);
    check(new_value && new_value.prefix[1].xc==-.75 && new_value.prefix[1].energy==.5625,
          "a later input push reused stale PCM statistics");
}
void private_key_and_epoch_receivers_share_only_pcm() {
    Config base;base.sample_rate=8000;base.carrier_hz=1500;base.bandwidth_hz=1.6;
    base.pattern_symbols=true;base.constellation_bits=1;base.spreading_factor=128;
    base.scramble=base.dsss=true;base.stream_epoch=9708;
    base.spreading_seed[3]=51;base.dsss_seed[13]=171;
    const auto chip=pattern_chip_samples(base);base.integration_seconds=16.*chip/base.sample_rate;
    check(chip>4096,"multi-key cache fixture did not select high-chip moments");
    const auto symbol=symbol_sample_count(base);constexpr long double origin=.137L;
    std::vector<float> pcm(static_cast<std::size_t>(std::ceil(symbol+origin)));
    PatternCode code(base,base.stream_epoch);
    std::mt19937 random(9708);std::normal_distribution<float> noise(0,.1F);
    for(std::size_t i=0;i<pcm.size();++i) {
        const auto within=static_cast<double>(static_cast<long double>(i)-origin);
        const auto value=within>=0?pattern_limit_pcm(code.shaped_value(0,1,within)):std::complex<double>{};
        pcm[i]=static_cast<float>((value*std::polar(1.,2*std::numbers::pi*base.carrier_hz*i/base.sample_rate+.71)).real())+noise(random);
    }
    PatternSearch search;search.start_offset_seconds=static_cast<double>(origin/base.sample_rate);
    search.start_uncertainty_seconds=0;search.search_stream_phases=false;search.compact_clock_search=true;
    search.hypotheses={{0,0}};search.retain_score=0;search.candidate_limit=8;
    search.track_limit=1;search.bit_limit=4;search.worker_threads=1;
    std::vector<PatternCorrelator> shared,individual;
    for(unsigned identity=0;identity<3;++identity) {
        auto config=base;if(identity==1)config.spreading_seed[3]^=0x5a;if(identity==2)++config.stream_epoch;
        shared.emplace_back(config,search,4*1024*1024);individual.emplace_back(config,search,4*1024*1024);
        check(shared.back().work().backend==PatternCorrelationBackend::pulse_moments,"multi-key fixture used an unsupported backend");
    }
    std::size_t hits=0;
    for(std::size_t offset=0;offset<pcm.size();) {
        const auto input=std::span(pcm).subspan(offset,std::min<std::size_t>(2048,pcm.size()-offset));
        CorrelationProjectionCache cache(input,(offset/2048)%2?64*1024:256*1024);
        for(std::size_t i=0;i<shared.size();++i) {
            shared[i].push(input,{},&cache);individual[i].push(input);
            const auto a=shared[i].take_bursts(),b=individual[i].take_bursts();
            check(a.size()==b.size(),"shared preprocessing changed next-poll publication");
            for(std::size_t j=0;j<a.size();++j)check(a[j].bits==b[j].bits && a[j].complete==b[j].complete &&
                a[j].first_sample==b[j].first_sample && a[j].end_sample==b[j].end_sample,
                "shared preprocessing changed bit identity or physical completion");
        }
        hits+=cache.hits();offset+=input.size();
    }
    check(hits>0,"independent keys and epochs did not reuse public preprocessing");
    for(std::size_t i=0;i<shared.size();++i) {
        const auto a=shared[i].candidates(),b=individual[i].candidates();
        check(a.size()==b.size() && !a.empty(),"cache changed full symbol coverage");
        for(std::size_t j=0;j<a.size();++j)check(a[j].bit==b[j].bit && a[j].score==b[j].score &&
            a[j].alternative_score==b[j].alternative_score && a[j].admission_threshold==b[j].admission_threshold &&
            a[j].first_sample==b[j].first_sample && a[j].end_sample==b[j].end_sample,
            "private key/epoch patterns were reused through the PCM cache");
        shared[i].finish();individual[i].finish();
        check(shared[i].initial_search_complete()==individual[i].initial_search_complete(),"cache changed physical acquisition coverage");
    }
}
}
int main() {
    try {
        carrier_statistics_and_identity();bounded_storage_and_cancellation();private_key_and_epoch_receivers_share_only_pcm();
        std::cout<<"pattern projection cache tests passed\n";return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
