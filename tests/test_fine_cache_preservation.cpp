#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/symbol_schedule.hpp"
#include "datapump/crypto.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

using namespace datapump;
using namespace datapump::modem;
namespace {
thread_local std::uint64_t evaluations=0,address_queries=0;
bool baseline=false;
void check(bool value,const char* message) {if(!value)throw Error(message);}
template<class F> void rejects(F&& work,const char* message) {
    try {work();}catch(const Error&){return;}throw Error(message);
}
Config configuration(unsigned factor=100) {
    Config c;c.sample_rate=6000;c.carrier_hz=1500;c.bandwidth_hz=8;
    c.pattern_symbols=true;c.constellation_bits=1;c.spreading_factor=64;
    c.scramble=true;c.dsss=true;c.dsss_factor=factor;
    c.outer_dsss_version=OuterDsssVersion::interleaved_v2;
    if(factor==1000){c.sample_rate=32000;c.carrier_hz=8000;}
    c.data_key.emplace(Bytes(32,0x67));
    for(std::size_t i=0;i<c.spreading_seed.size();++i) {
        c.spreading_seed[i]=static_cast<std::uint8_t>(3*i+7);
        c.dsss_seed[i]=static_cast<std::uint8_t>(5*i+11);
    }
    return c;
}
bool same(const SymbolStreamAddress& a,const SymbolStreamAddress& b) {
    return a.epoch==b.epoch&&a.ordinal==b.ordinal;
}
SymbolStreamAddress address(const Config& c,std::uint64_t epoch,std::uint64_t index) {
    return symbol_stream_address(c,epoch,c.stream_phase_samples,index);
}
std::uint64_t digest=1469598103934665603ULL;
template<class T> void hash_bytes(std::span<const T> values) {
    for(const auto byte:std::as_bytes(values)) {
        digest^=std::to_integer<unsigned char>(byte);digest*=1099511628211ULL;
    }
}
void hash_pair(const std::array<std::complex<double>,2>& values) {
    // Explicit components avoid implementation padding in complex objects.
    for(const auto v:values) {
        const std::array<double,2> parts{v.real(),v.imag()};hash_bytes<double>(parts);
    }
}
void miss_count(std::uint64_t delta,bool reused,const char* label) {
#ifdef DATAPUMP_FINE_COEFFICIENT_PROBE
    check(delta==(reused&&!baseline?0U:2U),label);
#else
    (void)delta;(void)reused;(void)label;
#endif
}
void alias_and_waveform() {
    constexpr std::uint64_t epoch=1789312671;
    for(const unsigned factor:{10U,100U,1000U}) {
        auto c=configuration(factor);PatternCode code(c,epoch);
        const auto bytes=code.working_bytes(),chips=code.chips_per_symbol(),chip=code.chip_samples();
        for(const auto phase:std::array<std::uint64_t,6>{1,1500,5999,17,1,0}) {
            const auto old=code.values(7*chips+11);
            const auto before=evaluations;code.set_stream_phase_samples(phase);
            const auto actual=code.values(7*chips+11);
            miss_count(evaluations-before,true,"whole-second alias did not retain both coefficients");
            auto shifted=c;shifted.stream_phase_samples=phase;PatternCode fresh(shifted,epoch);
            check(actual==old&&actual==fresh.values(7*chips+11),"whole-second alias changed a private coefficient");
            for(const auto index:std::array<std::uint64_t,3>{0,1,7})
                for(const double within:{-.375*chip,23.125*chip,static_cast<double>(code.symbol_samples())-.375*chip}) {
                    const auto value=code.shaped_values(index*chips,within);
                    check(value==fresh.shaped_values(index*chips,within),"alias changed exact shaped pair");hash_pair(value);
                }
        }
        check(code.working_bytes()==bytes,"phase reuse grew retained workspace");
        rejects([&]{code.set_stream_phase_samples(c.sample_rate);},"setter accepted out-of-range phase");
        auto unchanged=c;PatternCode phase_zero(unchanged,epoch);
        check(code.values(0)==phase_zero.values(0),"rejected phase setter mutated the original phase");
        // Complete encrypted private PCM, in varying chunks. Root compares the
        // emitted hash of this exact fixture between old and new code objects.
        Bytes wire;for(const unsigned plain:{0U,1U}) {
            const auto at=address(c,epoch,wire.size());
            const auto mask=c.data_key->stream(StreamPurpose::Data,at.epoch,at.ordinal/8,1);
            wire.push_back(static_cast<std::uint8_t>(plain^((mask[0]>>(7-at.ordinal%8))&1U)));
        }
        hash_bytes<std::uint8_t>(wire);
        PatternTransmitter tx(wire,c,epoch,0,false,true);
        std::array<float,503> pcm{};std::uint64_t generated=0;
        while(!tx.finished()) {
            const auto n=tx.read(pcm);check(n!=0,"private transmitter stalled");
            hash_bytes<float>(std::span<const float>(pcm).first(n));generated+=n;
        }
        check(generated==tx.total_samples(),"phase proposal changed the physical PCM endpoint");
        TransmitTrace trace;tx.copy_transmit_trace(trace,2*chips);
        hash_bytes<std::uint8_t>(trace.pattern_input);hash_bytes<std::uint8_t>(trace.pattern_output);
        hash_bytes<std::uint8_t>(trace.pattern_key);hash_bytes<std::uint8_t>(trace.dsss_key);
        hash_bytes<std::uint8_t>(trace.outer_dsss_key);hash_bytes<std::uint8_t>(trace.dsss_permutation_seed);
        std::cout<<"waveform factor="<<factor<<" chip="<<chip<<" chips="<<chips
                 <<" samples="<<generated<<" digest="<<digest<<" workspace="<<bytes<<'\n';
    }
}
void fractional_and_sparse() {
    auto c=configuration(10);c.bandwidth_hz=80;c.integration_seconds=.3;c.stream_phase_samples=100;
    constexpr std::uint64_t epoch=1789312671;PatternCode code(c,epoch);
    const auto chips=code.chips_per_symbol();check(chips>16,"fractional fixture too small");
    auto next=c;next.stream_phase_samples=1100;
    check(same(address(c,epoch,0),address(next,epoch,0)),"fixture lost symbol-zero alias");
    check(!same(address(c,epoch,3),address(next,epoch,3)),"fixture lost later UTC crossing");
    const auto zero=code.values(0),later=code.values(3*chips+1);
    const auto before=evaluations;code.set_stream_phase_samples(1100);const auto kept=code.values(0);
    miss_count(evaluations-before,true,"symbol-zero alias missed or stale later symbol confused proof");
    const auto before_later=evaluations;const auto changed=code.values(3*chips+1);
    miss_count(evaluations-before_later,false,"later canonical crossing retained stale coefficients");
    PatternCode fresh(next,epoch);
    check(kept==zero&&kept==fresh.values(0),"fractional zero alias changed");
    check(changed==fresh.values(3*chips+1)&&changed!=later,"later crossing used the first symbol's identity");
    // Nonzero ordinal and a partial final chip preserve complete geometry.
    c.integration_seconds=std::nextafter(.3005,0.);c.stream_phase_samples=1804;
    PatternCode partial(c,epoch);const auto n=partial.chips_per_symbol();
    check(address(c,epoch,0).ordinal!=0&&partial.symbol_samples()%partial.chip_samples()!=0,
          "partial/nonzero-ordinal fixture lost intended geometry");
    for(const auto phase:std::array<std::uint64_t,5>{1805,5999,1804,0,1805}) {
        partial.values(n-1);partial.values(n+2);partial.set_stream_phase_samples(phase);
        auto shifted=c;shifted.stream_phase_samples=phase;PatternCode independent(shifted,epoch);
        for(const auto abs:std::array<std::uint64_t,4>{0,n-1,n+2,9*n+3})
            check(partial.values(abs)==independent.values(abs),"partial/ordinal phase mapping changed");
    }
    // More than two distinct retained symbols deliberately falls back; this
    // bounds proof work to four address lookups, irrespective of seek history.
    c.integration_seconds=.3;c.stream_phase_samples=100;PatternCode sparse(c,epoch);
    for(const auto abs:std::array<std::uint64_t,3>{0,3*chips+1,6*chips+2})sparse.values(abs);
    const auto queries_before=address_queries;sparse.set_stream_phase_samples(101);
#ifdef DATAPUMP_FINE_COEFFICIENT_PROBE
    check(address_queries-queries_before<=4,"sparse seek history exceeded the two-index proof bound");
#endif
    auto shifted=c;shifted.stream_phase_samples=101;PatternCode independent(shifted,epoch);
    for(const auto abs:std::array<std::uint64_t,3>{0,3*chips+1,6*chips+2})
        check(sparse.values(abs)==independent.values(abs),"bounded third-symbol fallback changed a coefficient");
}
void scheduled(bool packed) {
    auto c=configuration(100);auto plan=std::make_shared<SchedHopPlan>();
    plan->time_multiplier=10;plan->group_hops=1;
    plan->clock_error_seconds=plan->peer_clock_error_seconds=plan->region_seconds=plan->audio_error_seconds=0;
    plan->ramp_seconds=.01;c.scheduled_hopping=plan;
    if(packed)c.integration_seconds=.02; // <= guard width: force the intended packed calendar
    check(packed_scheduled_symbols(c)==packed,"scheduled fixture selected the wrong calendar kind");
    const SchedHopSourceMap map(*plan,*c.data_key,c.sample_rate);
    const auto origin=map.source_to_physical(map.locate(8312ULL*c.sample_rate).source);
    const auto epoch=origin.sample/c.sample_rate;c.stream_phase_samples=origin.sample%c.sample_rate;
    PatternCode code(c,epoch);const auto chips=code.chips_per_symbol(),bytes=code.working_bytes();
    const auto initial=address(c,epoch,0);
    auto alias=c;bool found=false;
    for(std::uint64_t p=0;p<c.sample_rate;++p)if(p!=c.stream_phase_samples) {
        alias.stream_phase_samples=p;if(same(initial,address(alias,epoch,0))){found=true;break;}
    }
    check(found,"scheduled fixture has no exact canonical alias");
    code.values(0);const auto before=evaluations;code.set_stream_phase_samples(alias.stream_phase_samples);
    const auto actual=code.values(0);miss_count(evaluations-before,true,"exact scheduled alias missed both bit rings");
    PatternCode independent(alias,epoch);check(actual==independent.values(0),"scheduled canonical alias changed coefficients");
    for(const auto phase:std::array<std::uint64_t,5>{0,1,5999,c.stream_phase_samples,alias.stream_phase_samples}) {
        for(const auto abs:std::array<std::uint64_t,3>{chips-1,chips+2,9*chips+3})code.values(abs);
        code.set_stream_phase_samples(phase);auto shifted=c;shifted.stream_phase_samples=phase;PatternCode fresh(shifted,epoch);
        for(const auto abs:std::array<std::uint64_t,5>{0,chips-1,chips+2,9*chips+3,17*chips+7})
            check(code.values(abs)==fresh.values(abs),"keyed split/packed canonical phase mapping changed");
        for(const auto index:std::array<std::uint64_t,2>{0,1}) {
            const auto point=static_cast<double>(code.symbol_samples())-.375*code.chip_samples();
            const auto value=code.shaped_values(index*chips,point);
            check(value==fresh.shaped_values(index*chips,point),"scheduled neighboring pulse tail changed");hash_pair(value);
        }
    }
    check(code.working_bytes()==bytes,"scheduled phase proof grew retained calendar workspace");
    std::cout<<"scheduled packed="<<packed<<" alias="<<alias.stream_phase_samples<<" workspace="<<bytes<<'\n';
}
void diagnostics_and_errors() {
    constexpr std::uint64_t epoch=1789312671;auto c=configuration(100);
    for(const unsigned factor:{1U,100U}) {
        auto old_path=configuration(factor);
        if(factor>1)old_path.outer_dsss_version=OuterDsssVersion::legacy_v1;
        PatternCode untouched(old_path,epoch);untouched.values(0);const auto before=evaluations;
        untouched.set_stream_phase_samples(1);const auto actual=untouched.values(0);
        miss_count(evaluations-before,false,"DSSS Off/legacy path unexpectedly gained calendar proof or retention");
        old_path.stream_phase_samples=1;PatternCode independent(old_path,epoch);
        check(actual==independent.values(0),"DSSS Off/legacy fallback changed exact coefficients");
    }
    PatternCode traced(c,epoch);traced.enable_transmit_trace(0);traced.capture_transmit_trace(true);
    traced.values(0);TransmitTrace prior;traced.copy_transmit_trace(prior,1);
    const auto before=evaluations;traced.set_stream_phase_samples(1);traced.values(0);
    miss_count(evaluations-before,false,"transmit trace bypass did not preserve original invalidation");
    TransmitTrace after;traced.copy_transmit_trace(after,1);
    check(prior.pattern_input==after.pattern_input&&prior.pattern_output==after.pattern_output&&
          prior.pattern_key==after.pattern_key&&prior.dsss_key==after.dsss_key&&
          prior.outer_dsss_key==after.outer_dsss_key&&prior.dsss_permutation_seed==after.dsss_permutation_seed&&
          prior.outer_dsss_epoch==after.outer_dsss_epoch&&prior.outer_dsss_byte_offset==after.outer_dsss_byte_offset&&
          prior.dsss_permutation_byte_offset==after.dsss_permutation_byte_offset&&prior.outer_dsss_first_pair==after.outer_dsss_first_pair,
          "phase proof changed retained diagnostic bytes");
    traced.capture_transmit_trace(false);traced.values(7);const auto inactive=evaluations;
    traced.set_stream_phase_samples(2);traced.values(7);
    miss_count(evaluations-inactive,false,"allocated inactive trace unexpectedly enabled cache preservation");
    auto gate=std::make_shared<FhssPlan>();gate->channels=2;gate->shifts[0]=30000000;gate->shifts[1]=31000000;
    gate->dwell_seconds=.4;gate->power_ramps=true;gate->matched_gaps=true;
    gate->clock_error_seconds=gate->peer_clock_error_seconds=gate->audio_error_seconds=0;c.fake_hopping=gate;
    PatternCode gated(c,epoch);const auto ungated=gated.values(0);const auto b=evaluations;gated.set_stream_phase_samples(1100);
    const auto actual=gated.values(0);miss_count(evaluations-b,false,"coefficient gate unexpectedly preserved phase-dependent entries");
    auto shifted=c;shifted.stream_phase_samples=1100;PatternCode fresh(shifted,epoch);
    check(actual==fresh.values(0)&&actual!=ungated,"diagnostic coefficient gate lost actual phase-dependent omission");
    // New phase can overflow a stale distant index; the setter itself formerly
    // made no address query. It must still accept the valid phase and defer the
    // address error until that exact value is requested.
    c=configuration(10);c.bandwidth_hz=80;c.integration_seconds=.3;
    const auto near_end=std::numeric_limits<std::uint64_t>::max()-1;
    PatternCode overflow(c,near_end);const auto chips=overflow.chips_per_symbol();overflow.values(6*chips);
    overflow.set_stream_phase_samples(5900);auto target=c;target.stream_phase_samples=5900;PatternCode exact(target,near_end);
    check(overflow.values(0)==exact.values(0),"failed optional proof did not restore/commit the requested phase");
    rejects([&]{overflow.values(6*chips);},"stale distant canonical overflow was hidden by preserved cache");
    rejects([&]{overflow.set_stream_phase_samples(c.sample_rate);},"invalid phase lost original setter Error behavior");
    check(overflow.values(0)==exact.values(0),"invalid phase mutated state after failed optional proof");
}
void sweep() {
    auto c=configuration(100);auto plan=std::make_shared<SchedHopPlan>();plan->time_multiplier=10;
    plan->clock_error_seconds=plan->peer_clock_error_seconds=plan->audio_error_seconds=0;c.scheduled_hopping=plan;
    constexpr std::uint64_t epoch=8312;
    for(const unsigned span:{128U,512U}) {
        PatternCode code(c,epoch);double checksum=0;const auto before=evaluations,queries_before=address_queries;const auto start=std::clock();
        for(unsigned iteration=0;iteration<1024;++iteration)for(const auto phase:std::array<std::uint64_t,6>{0,1,2,5999,17,0}) {
            code.set_stream_phase_samples(phase);
            // Includes setter cost. The small neighborhood fits both rings;
            // the production 512-sample span can exceed 32 fine chips/bit.
            for(unsigned n=0;n<span;++n) {
                const auto value=code.shaped_values(0,1500.375+n);
                checksum+=value[0].real()+value[1].imag();
            }
        }
        const auto cpu=static_cast<double>(std::clock()-start)/CLOCKS_PER_SEC;
        std::cout<<"phase_sweep span="<<span<<" cpu="<<cpu<<" evaluations="<<evaluations-before<<" address_queries="<<address_queries-queries_before
                 <<" checksum="<<std::hexfloat<<checksum<<std::defaultfloat<<" workspace="<<code.working_bytes()<<'\n';
    }
}
}
extern "C" void datapump_test_fine_value() {++evaluations;}
extern "C" void datapump_test_fine_address() {++address_queries;}
int main(int argc,char** argv) {
    try {
        bool cpu=false;for(int i=1;i<argc;++i) {
            const std::string_view arg(argv[i]);if(arg=="--baseline")baseline=true;
            else if(arg=="--cpu")cpu=true;else throw Error("unknown fine-cache fixture argument");
        }
        if(cpu){sweep();return 0;}
        alias_and_waveform();fractional_and_sparse();scheduled(false);scheduled(true);diagnostics_and_errors();
        std::cout<<"PASS exact fine coefficient preservation digest="<<digest<<'\n';return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
