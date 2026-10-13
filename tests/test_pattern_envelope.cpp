#include "datapump/pattern_correlator.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "../src/pattern_envelope.hpp"
#include "../src/pattern_projection_cache.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <numbers>
#include <random>

using namespace datapump;
using namespace datapump::modem;
namespace {
void check(bool yes,const char* message){if(!yes)throw Error(message);}
template<class F> void rejects(F f,const char* message){try{f();}catch(const Error&){return;}throw Error(message);}
Config geometry(bool secret=true) {
    Config c;c.sample_rate=512;c.bandwidth_hz=4;c.carrier_hz=128;
    c.pattern_symbols=true;c.constellation_bits=1;c.spreading_factor=512;
    c.scramble=secret;c.dsss=false;c.dsss_factor=1;c.stream_epoch=1800000041;
    c.spreading_seed[0]=37;c.spreading_seed[17]=211;return c;
}
PatternSearch search_for(const Config& c,std::size_t delay=0) {
    PatternSearch search;search.start_offset_seconds=static_cast<double>(delay+pattern_pulse_padding_samples(c))/c.sample_rate;
    search.start_uncertainty_seconds=0;search.search_stream_phases=false;
    search.frequency_offsets_hz={0};search.worker_threads=1;search.chunk_bits=1;
    search.track_limit=4;search.candidate_limit=32;search.bit_limit=32;return search;
}
PatternCorrelatorOptions options() {PatternCorrelatorOptions o;o.amplitude_only=true;return o;}
std::vector<float> pcm(const Config& c,const Bytes& bits,std::size_t delay=37,
                       double frequency=0,double fraction=.375,bool silence=true) {
    PatternTransmitter tx(bits,c,c.stream_epoch,0,false);
    std::vector<std::complex<double>> analytic(static_cast<std::size_t>(tx.total_samples()));
    tx.read_analytic(analytic);
    const auto extra=silence?symbol_sample_count(c)+128:0;
    std::vector<float> result(delay+analytic.size()+extra);
    std::mt19937 random(70031);std::normal_distribution<double> noise(0,.002);
    for(std::size_t i=0;i<result.size();++i) {
        std::complex<double> value{};
        const auto coordinate=static_cast<double>(i)-delay-fraction;
        if(coordinate>=0 && coordinate<analytic.size()) {
            const auto at=static_cast<std::size_t>(coordinate);
            const auto part=coordinate-at;
            value=analytic[at]*(1-part);
            if(at+1<analytic.size())value+=analytic[at+1]*part;
        }
        value*=std::polar(1.,.731+2*std::numbers::pi*frequency*i/c.sample_rate);
        result[i]=static_cast<float>(value.real()+noise(random));
    }
    return result;
}
struct Stream {Bytes bits;bool complete=false;double score=0;std::uint64_t first=0;};
struct Result {std::vector<Stream> streams;PatternCorrelatorWork work;std::uint64_t first_publication=0;};
Result receive(const Config& c,PatternSearch search,std::span<const float> samples,std::size_t chunk,bool finish=true) {
    PatternCorrelator rx(c,search,4*1024*1024,options());Result result;
    std::map<std::pair<std::uint64_t,std::uint64_t>,std::size_t> lookup;
    const auto drain=[&](std::size_t consumed) {
        for(const auto& b:rx.take_bursts()) {
            check(!b.missing_slots,"envelope fixture admitted a missing-slot event");
            const auto key=std::pair{b.stream_first_sample,b.stream_first_symbol};
            auto [entry,new_stream]=lookup.emplace(key,result.streams.size());
            if(new_stream)result.streams.push_back({{},false,0,b.stream_first_sample});
            auto& stream=result.streams[entry->second];
            check(b.first_stream_symbol==b.stream_first_symbol+stream.bits.size(),"envelope pending identity changed");
            stream.bits.insert(stream.bits.end(),b.bits.begin(),b.bits.end());
            stream.complete=b.complete;stream.score=b.score;
            if(!b.bits.empty() && !result.first_publication)result.first_publication=consumed;
        }
    };
    for(std::size_t consumed=0;consumed<samples.size();) {
        const auto count=std::min(chunk,samples.size()-consumed);
        rx.push(samples.subspan(consumed,count));consumed+=count;drain(consumed);
        check(rx.working_bytes()<=4*1024*1024,"amplitude path exceeded workspace");
    }
    if(finish){rx.finish();drain(samples.size());}
    result.work=rx.work();return result;
}
const Stream& best(const Result& r) {
    check(!r.streams.empty(),"amplitude fixture did not admit a stream");
    return *std::max_element(r.streams.begin(),r.streams.end(),[](const auto& a,const auto& b){return a.bits.size()<b.bits.size();});
}
void conservative_bound() {
    std::mt19937 random(7701);std::exponential_distribution<double> weight;
    for(unsigned fixture=0;fixture<32;++fixture) {
        std::vector<double> w(128),e(128);detail::EnvelopeMoments m;
        for(std::size_t j=0;j<w.size();++j){w[j]=weight(random);e[j]=1+w[j];m.add(e[j],w[j],3);}
        const auto q=m.weighted_energy/m.energy;
        long double exact=0;
        const auto b=*std::max_element(w.begin(),w.end())-q;
        for(unsigned step=1;step<10000;++step) {
            const auto lambda=static_cast<long double>(step)/10000/b;
            long double value=0;for(auto x:w)value+=3*std::log1p(-lambda*(x-q));
            exact=std::max(exact,value);
        }
        check(m.score()<=exact+1e-9,"moment score exceeded conservative Gamma/Chernoff certificate");
        auto scaled=m;scaled.energy*=1e8L;scaled.energy_square*=1e16L;scaled.weighted_energy*=1e8L;
        check(std::abs(m.score()-scaled.score())<1e-9,"unknown noise-scale invariance failed");
        detail::EnvelopeMoments tempered;
        for(std::size_t j=0;j<w.size();++j)tempered.add(e[j],w[j],3,.125);
        check(tempered.score()<=m.score(),
            "held-chip evidence was charged as fresh independent PCM");
    }
    detail::EnvelopeFit impulse;
    for(unsigned j=0;j<128;++j)impulse.add(j<32?1000:1,j<32?10:1,2,j/32);
    check(impulse.score()==0,"one quarter supplied all envelope evidence");
    detail::EnvelopeMoments constant;
    for(unsigned j=0;j<128;++j)constant.add(j+1,1,2);
    check(constant.score()==0,"constant envelope manufactured bit evidence");
    detail::EnvelopeMoments flat_observation;
    for(unsigned j=0;j<128;++j)flat_observation.add(1,1+j%7,20,.5);
    check(flat_observation.score()==0,"constant received power manufactured a profile match");
}
void factored_statistics() {
    std::mt19937 rng(48197);std::uniform_real_distribution<double> unit(.001,1.);
    for(auto degrees:{1U,7U,39U})for(auto scale:{1e-15,1.,1e15})for(auto independent:{.375,100.}) {
        detail::EnvelopeTemplateFit t;detail::EnvelopeChannelFit c;
        std::array<detail::EnvelopeFit,2> reference;
        for(unsigned j=0;j<4096;++j) {
            const std::array<double,2> w{unit(rng),unit(rng)};
            const auto e=scale*(.01+w[0]+unit(rng)*.1);const auto q=std::min(3U,j/1024);
            t.add(w,q,independent);c.add(e,w,q);
            for(unsigned bit=0;bit<2;++bit)reference[bit].add(e,w[bit],degrees,q,independent);
        }
        for(unsigned bit=0;bit<2;++bit) {
            const auto old=reference[bit].score(),actual=c.score(t,bit,degrees);
            check(std::abs(old-actual)<1e-10*std::max(1.,std::abs(old)),
                "factored amplitude moments changed the original statistic");
        }
    }
}
void arithmetic_and_exact_reuse() {
    auto c=geometry();c.spreading_factor=128;
    auto search=search_for(c,37);search.start_uncertainty_seconds=1.5;
    const auto samples=pcm(c,Bytes{0,1});
    c.search_arithmetic=SearchArithmetic::fp64;const auto wide=receive(c,search,samples,317);
    c.search_arithmetic=SearchArithmetic::fp32;const auto single=receive(c,search,samples,317);
    check(wide.work.amplitude_float_envelopes==0 && single.work.amplitude_float_envelopes>0,
        "amplitude envelope arithmetic did not honor the selected precision");
    c.search_arithmetic=SearchArithmetic::default_mode;const auto automatic=receive(c,search,samples,317);
    check(automatic.work.amplitude_unchanged_polls>0 && automatic.work.amplitude_publication_polls>0,
        "partial amplitude input repeatedly rebuilt unchanged publication state");
    check(best(wide).bits==best(single).bits && best(single).bits==best(automatic).bits &&
          best(wide).first==best(single).first && best(wide).complete==best(single).complete,
        "native float amplitude changed acceptance/identity/absence");
    check(single.first_publication==wide.first_publication && automatic.first_publication==wide.first_publication,
        "native float amplitude changed next-poll publication");
    check(wide.work.amplitude_float_transforms==0 && single.work.amplitude_float_transforms>0 &&
          automatic.work.amplitude_float_transforms==single.work.amplitude_float_transforms,
        "amplitude arithmetic policy did not execute its resolved precision");
    check(std::abs(best(wide).score-best(single).score)<1e-4*std::max(1.,best(wide).score),
        "native float amplitude score drift exceeds fixture tolerance");
    for(auto scale:{1e-20F,1e20F}) {
        auto changed=samples;for(auto& x:changed)x*=scale;
        const auto result=receive(c,search,changed,317);
        check(result.work.amplitude_precision_fallbacks>0 && result.work.amplitude_float_transforms==0,
            "amplitude extreme-range fallback was not original FP64");
    }
}
void queued_publication_without_input() {
    auto c=geometry();c.spreading_factor=128;
    auto search=search_for(c,37);search.chunk_bits=2;search.track_limit=1;
    const Bytes bits{0,1,0};const auto samples=pcm(c,bits,37,0,.375,false);
    PatternCorrelator rx(c,search,4*1024*1024,options());
    PatternCorrelator partial(c,search,4*1024*1024,options());
    check(partial.take_bursts().empty(),"new amplitude receiver published evidence without input");
    const auto fft=pattern_amplitude_geometry(c,search).fft_samples;
    check(partial.projection_cache_headroom(fft-1)==0 && partial.projection_cache_headroom(fft)>0,
        "partial amplitude allocation bound missed the exact FFT boundary");
    const auto before=partial.working_bytes();partial.push(std::span(samples).first(fft-1));
    check(partial.take_bursts().empty() && partial.working_bytes()==before &&
          partial.projection_cache_headroom(1)>0,
        "partial amplitude push grew storage or lost full-block allocation protection");
    rx.push(samples);
    const auto first=rx.take_bursts();
    check(first.size()==1 && first[0].bits==Bytes({0,1}) && !first[0].complete,
        "full amplitude queue did not publish its original prefix");
    const auto second=rx.take_bursts();
    check(second.size()==1 && second[0].bits==Bytes({0}) && !second[0].complete &&
          second[0].stream_first_sample==first[0].stream_first_sample &&
          second[0].stream_first_symbol==first[0].stream_first_symbol &&
          second[0].first_stream_symbol==first[0].first_stream_symbol+2,
        "amplitude queue delayed committed output until more PCM or changed identity");
    check(rx.take_bursts().empty(),"amplitude repeated drain duplicated bits");
    rx.finish();
    check(rx.take_bursts().empty(),"EOF duplicated amplitude bits or manufactured completion");
}
void coverage_and_fallback() {
    auto c=geometry();auto s=search_for(c);s.hypotheses={{-17,-2000},{17,2000}};
    const auto g=pattern_amplitude_geometry(c,s);
    check(g.spectral_margin_bins>=2 && g.spectral_margin_bins<3,
        "experimental amplitude margin no longer includes its measured two-bin guard");
    auto short_row=c;short_row.spreading_factor=64;
    check(pattern_amplitude_geometry(short_row,s).spectral_margin_bins>=8,
        "short amplitude rows lost their conservative wider-bin fallback");
    check(g.supported && g.admitted_frequency_half_width_hz==17 && g.clock_half_width_ppm==2000,
        "amplitude geometry discarded declared frequency/clock boundaries");
    check(g.clock_hypotheses>1 && g.retained_low_hz<111 && g.retained_high_hz>145,
        "full carrier domain or uncapped clock grid was not retained");
    auto declared=search_for(c);declared.frequency_offsets_hz.clear();declared.admitted_frequency_half_width_hz=17;
    check(pattern_amplitude_geometry(c,declared).admitted_frequency_half_width_hz==17,
        "local default overwrote the declared RF domain");
    declared.expand_clock_search=true;
    check(pattern_amplitude_geometry(c,declared).admitted_frequency_half_width_hz==17,
        "expanded default overwrote the declared RF domain");
    auto coupled=search_for(c);coupled.couple_clock_to_carrier=true;coupled.frequency_offsets_hz={0,.025};
    coupled.clock_errors_ppm={-100,100};
    check(pattern_amplitude_geometry(c,coupled).clock_half_width_ppm>295,
        "independent clock and coupled carrier rate were not combined");
    auto bad=search_for(c);bad.frequency_offsets_hz={std::numeric_limits<double>::quiet_NaN()};
    check(!pattern_amplitude_geometry(c,bad).supported,"nonfinite RF hypothesis was silently discarded");
    c.dsss=true;check(!pattern_amplitude_geometry(c,s).supported,"unsupported DSSS silently changed detector");
    rejects([&]{PatternCorrelator rx(c,s,4*1024*1024,options());},"unsupported amplitude geometry did not reject explicitly");
    c=geometry();c.carrier_hz=2;check(!pattern_amplitude_geometry(c,s).supported,"DC-crossing envelope band was admitted");
    c=geometry();rejects([&]{PatternCorrelator rx(c,s,1,options());},"tiny workspace allocated an amplitude frontend");
}
void bits_progress_and_physical_end() {
    for(const auto chips:{64U,128U,256U})for(unsigned key=0;key<4;++key) {
        auto c=geometry();c.spreading_factor=chips;c.spreading_seed[3]=static_cast<std::uint8_t>(key*37);
        const auto result=receive(c,search_for(c,37),pcm(c,Bytes{0,1}),317);
        check(best(result).bits==Bytes({0,1}) && best(result).complete,"short private envelope row cannot admit its legitimate high-SNR bits");
    }
    for(const auto secret:{false,true}) {
        const auto c=geometry(secret);const Bytes bits{0,1,0};const auto samples=pcm(c,bits);
        const auto search=search_for(c,37);
        const auto all=receive(c,search,samples,samples.size()),pieces=receive(c,search,samples,317);
        check(best(all).bits==bits && best(pieces).bits==bits,"original public/private shaped envelope bits changed");
        check(best(all).complete && best(pieces).complete,"fully scored absent symbol did not complete amplitude stream");
        check(best(all).first==best(pieces).first && best(all).score==best(pieces).score,
            "arbitrary input blocks changed amplitude observations or identity");
        const auto first_end=37+pattern_pulse_padding_samples(c)+symbol_sample_count(c);
        const auto n=pattern_amplitude_geometry(c,search).fft_samples;
        check(pieces.first_publication<=first_end+n+317,"accepted bit was batched behind a later symbol");
        const auto short_capture=std::span(samples).first(static_cast<std::size_t>(37+pattern_pulse_padding_samples(c)+3*symbol_sample_count(c)+n));
        const auto eof=receive(c,search,short_capture,173);
        check(best(eof).bits==bits && !best(eof).complete,"EOF manufactured amplitude absence completion");
        auto shifted_search=search;shifted_search.frequency_offsets_hz={-17,17};
        const auto shifted=receive(c,shifted_search,pcm(c,bits,37,13.37),211);
        check(best(shifted).bits==bits,"phase/frequency-independent envelope channel missed an interior carrier");
    }
}
void structured_rejection() {
    const auto c=geometry();const auto s=search_for(c,37);const Bytes bits{0,1};
    const auto signal=pcm(c,bits);
    // Fixed whole-message cases, not a rare-event rate estimate. Every stream
    // is inspected; a correct longest result cannot hide an extra admission.
    for(unsigned key=1;key<=16;++key) {
        auto wrong=c;wrong.spreading_seed[1]=static_cast<std::uint8_t>(key*11);
        const auto result=receive(wrong,s,signal,4093);
        for(const auto& stream:result.streams)check(stream.bits.empty(),"strong unrelated private key admitted amplitude bits");
    }
    std::mt19937 random(4701);std::normal_distribution<float> noise(0,.03F);
    std::vector<float> samples(signal.size());for(auto& x:samples)x=noise(random);
    const auto pure=receive(c,s,samples,997);check(pure.streams.empty(),"noise-only amplitude fixture admitted bits");
    for(std::size_t i=0;i<samples.size();++i)samples[i]+=.7F*std::sin(2*std::numbers::pi*131.1*i/c.sample_rate);
    const auto tone=receive(c,s,samples,997);check(tone.streams.empty(),"constant carrier interference admitted amplitude bits");
    for(std::size_t i=0;i<samples.size();++i) {
        samples[i]=noise(random);
        for(const auto f:{71.3,117.1,128.7,143.9,188.2})samples[i]+=.5F*std::sin(2*std::numbers::pi*f*i/c.sample_rate);
    }
    check(receive(c,s,samples,997).streams.empty(),"unrelated multitone interference admitted amplitude bits");
}
void coarse_channels() {
    auto c=geometry();c.sample_rate=2048;c.carrier_hz=512;
    auto s=search_for(c,37);s.frequency_offsets_hz={-300,300};
    const auto g=pattern_amplitude_geometry(c,s);
    check(g.supported && g.channel_count>1 && g.channel_count<=32,"wide oscillator domain did not produce bounded coarse channels");
    std::size_t previous=g.first_bin;
    for(std::size_t j=0;j<g.channel_count;++j) {
        const auto channel=pattern_amplitude_channel(g,j);
        check(channel.first_bin<=previous && channel.bin_count<g.bin_count,"coarse channels lost coverage or retained full-band noise");
        previous=channel.first_bin+channel.bin_count;
    }
    check(previous==g.first_bin+g.bin_count,"coarse carrier union omitted the high frequency boundary");
    auto limited=s;limited.amplitude_channel_limit=1;
    const auto one=pattern_amplitude_geometry(c,limited);
    check(one.channel_count==1 && one.first_bin==g.first_bin && one.bin_count==g.bin_count,
        "planned channel limit discarded full RF coverage");
    for(const auto edge:{-300.,300.}) {
        const auto result=receive(c,s,pcm(c,Bytes{0,1},37,edge),4093);
        check(best(result).bits==Bytes({0,1}),"coarse amplitude bank missed an admitted frequency boundary");
        check(result.work.amplitude_channels==g.channel_count && result.work.amplitude_channel_updates>result.work.amplitude_observations,
            "coarse channel work was not reported");
        for(const auto& stream:result.streams)check(stream.bits==Bytes({0,1}),"coarse bank admitted an unrelated stream");
    }
    // All carrier cells share one envelope row. Reducing workspace widens
    // cells rather than removing RF hypotheses or allocating shifted PCM.
    PatternCorrelator wide(c,s,4*1024*1024,options());
    // The actual factored footprint is smaller than the unchanged virtual
    // unfactored band allowance. That footprint alone now exercises widening
    // without assuming storage savings may add detector bands.
    const auto tight_budget=wide.reserved_workspace_bytes();
    PatternCorrelator tight(c,s,tight_budget,options());
    check(tight.work().amplitude_channels<g.channel_count && tight.work().amplitude_bins==g.bin_count,
        "workspace fallback discarded complete RF coverage");
    // Correlated overlapping channels require no independence: subtracting
    // log(C) from each certified score pays the ordinary union bound.
    const auto threshold=23.;long double bound=0;
    for(std::size_t j=0;j<g.channel_count;++j)bound+=std::exp(-threshold-std::log(static_cast<double>(g.channel_count)));
    check(bound<=std::exp(-threshold)*(1+1e-12),"coarse channel trial accounting exceeded its union bound");
}
void precision_rows_and_bounded_scratch() {
    std::mt19937 random(77913);std::normal_distribution<float> noise(0,.3F);
    for(const auto n:{16U,64U,1024U,65536U}) {
        std::vector<float> pcm(n);for(auto& x:pcm)x=noise(random);
        constexpr auto budget=8*1024*1024;
        detail::CorrelationProjectionCache cache(pcm,budget,n);
        check(cache.fft_available(n) && !cache.fft_initialized(),"FFT lazy allocation changed");
        std::vector<long double> fp32(n/2),fp64(n/2);
        cache.fft_spectrum(pcm,1,fp32,{},true);
        const auto float_bytes=cache.allocated_bytes();
        check(cache.fft_initialized() && float_bytes<=cache.working_bytes(),"FP32 scratch exceeded complete reservation");
        cache.fft_spectrum(pcm,1,fp64);
        check(cache.allocated_bytes()>float_bytes && cache.allocated_bytes()<=cache.working_bytes() &&
              cache.working_bytes()<=budget,"precision switching retained both scratch arenas or exceeded workspace");
        const auto total=std::max(1e-30L,fp64.back());
        for(std::size_t j=0;j<fp32.size();++j)check(std::abs(fp32[j]-fp64[j])<=3e-5L*total,
            "native FP32 FFT energy disagreed beyond bounded fixture tolerance");
        const auto exact_energy=cache.fft_energy(pcm,1,n/2-1);
        check(exact_energy==static_cast<double>(fp64.back()),"old FP64 scalar FFT-energy path changed");
        unsigned double_computes=0,float_computes=0;
        const auto double_row=cache.spectrum(pcm,1307,n,1,n/2-1,512,[&](std::span<long double> out){
            ++double_computes;cache.fft_spectrum(pcm,1,out);
        });
        check(bool(double_row),"FP64 spectrum was not cached");
        const auto saved_double=std::vector<long double>(double_row.prefix.begin(),double_row.prefix.end());
        const auto float_row=cache.spectrum(pcm,1307,n,1,n/2-1,512,[&](std::span<long double> out){
            ++float_computes;cache.fft_spectrum(pcm,1,out,{},true);
        },{},true);
        check(bool(float_row) && float_row.prefix.data()!=double_row.prefix.data() &&
              double_computes==1 && float_computes==1,"operand precision was omitted from spectrum cache identity");
        const auto again=cache.spectrum(pcm,1307,n,1,n/2-1,512,[&](std::span<long double>){
            throw Error("FP64 precision row unexpectedly recomputed");
        });
        check(again && again.prefix.data()==double_row.prefix.data() &&
              std::equal(again.prefix.begin(),again.prefix.end(),saved_double.begin()),
              "FP32 scratch switching invalidated cached FP64 evidence");
        const auto float_again=cache.spectrum(pcm,1307,n,1,n/2-1,512,[&](std::span<long double>){
            throw Error("FP32 precision row unexpectedly recomputed");
        },{},true);
        check(float_again && float_again.prefix.data()==float_row.prefix.data(),"FP32 identity did not reuse preprocessing");
        check(cache.allocated_bytes()<=cache.working_bytes(),"mixed-precision immutable rows exceeded complete reservation");
        std::stop_source stop;stop.request_stop();
        rejects([&]{cache.spectrum(pcm,1307,n,1,n/2-1,512,[](auto){},stop.get_token(),true);},
                "FP32 cache hit ignored cancellation");
        rejects([&]{cache.fft_spectrum(pcm,1,fp32,stop.get_token(),true);},
                "FP32 FFT ignored cancellation");
    }
}
void finite_energy_ranges() {
    constexpr auto n=1024U;
    for(const auto scale:{0.F,1e-10F,1.F,1e10F}) {
        std::vector<float> pcm(n);
        for(std::size_t j=0;j<n;++j)pcm[j]=scale*static_cast<float>(.25*std::sin(.37*j)+.1*std::cos(.173*j));
        detail::CorrelationProjectionCache cache(pcm,256*1024,n);
        std::vector<long double> a(n/2),b(n/2);
        cache.fft_spectrum(pcm,1,a,{},true);cache.fft_spectrum(pcm,1,b);
        for(std::size_t j=0;j<a.size();++j)check(std::isfinite(a[j]) && a[j]>=0 &&
            std::abs(a[j]-b[j])<=3e-5L*std::max(1e-30L,b.back()),"finite FFT-energy scale lost agreement");
    }
}
void shared_frontend_identity() {
    auto c=geometry();auto s=search_for(c,37);const auto samples=pcm(c,Bytes{0},37,0,.375,false);
    PatternCorrelator a(c,s,4*1024*1024,options()),b(c,s,4*1024*1024,options()),reference(c,s,4*1024*1024,options());
    const auto n=pattern_amplitude_geometry(c,s).fft_samples;
    check(a.reserved_workspace_bytes()>=a.working_bytes()+2*n*sizeof(std::complex<double>),
        "local amplitude fallback scratch was not reserved");
    for(std::size_t offset=0;offset<samples.size();) {
        const auto count=std::min<std::size_t>(317,samples.size()-offset);const auto input=std::span(samples).subspan(offset,count);
        detail::CorrelationProjectionCache cache(input,64*1024,n);
        check(cache.fft_available(n),"bounded cohort did not share FFT plan/scratch");
        a.push(input,{},&cache);b.push(input,{},&cache);reference.push(input);
        check(cache.working_bytes()<=64*1024,"shared amplitude frontend exceeded its bound");offset+=count;
    }
    check(a.work().amplitude_transforms>0 && b.work().amplitude_transforms==0 && b.work().amplitude_cache_hits>0,
        "cohort duplicated identical amplitude transforms");
    const auto x=b.candidates(),y=reference.candidates();check(x.size()==y.size(),"cached amplitude search omitted evidence");
    for(std::size_t j=0;j<x.size();++j)check(x[j].score==y[j].score && x[j].bit==y[j].bit,
        "shared FFT energy changed private template contractions");
    // A range fallback is determined by the common PCM. Reuse it within the
    // automatic policy, but never borrow it for the independent FP64 request.
    std::vector<float> large(2*n,2e15F);
    PatternCorrelator large_a(c,s,4*1024*1024,options()),large_b(c,s,4*1024*1024,options());
    auto exact_config=c;exact_config.search_arithmetic=SearchArithmetic::fp64;
    PatternCorrelator large_exact(exact_config,s,4*1024*1024,options());
    detail::CorrelationProjectionCache large_cache(large,256*1024,n);
    large_a.push(large,{},&large_cache);large_b.push(large,{},&large_cache);large_exact.push(large,{},&large_cache);
    check(large_a.work().amplitude_precision_fallbacks>0 && large_a.work().amplitude_float_transforms==0 &&
          large_b.work().amplitude_transforms==0 && large_b.work().amplitude_cache_hits>0 &&
          large_exact.work().amplitude_transforms>0 && large_exact.work().amplitude_cache_hits==0,
          "deterministic range fallback lost sharing or contaminated FP64-force identity");
    std::vector<float> input(128,.1F);unsigned calls=0;
    detail::CorrelationProjectionCache cache(input,64*1024);
    const auto compute=[&]{++calls;return static_cast<double>(input[0]);};
    check(cache.energy(input,0,64,3,20,512,compute)==.1F && calls==1,"energy row did not compute");
    cache.energy(input,0,64,3,20,512,compute);check(calls==1,"energy row was not reused");
    cache.energy(input,64,64,3,20,512,compute);check(calls==2,"different capture coordinate reused energy");
    cache.energy(input,0,64,4,20,512,compute);check(calls==3,"different band reused energy");
    input[0]=.2F;detail::CorrelationProjectionCache next(input,64*1024);
    check(next.energy(input,0,64,3,20,512,compute)==.2F && calls==4,"next push reused stale energy");
    std::stop_source cancel;cancel.request_stop();
    rejects([&]{next.energy(input,0,64,3,20,512,compute,cancel.get_token());},"energy hit bypassed cancellation");
    check(calls==4,"cancelled energy hit performed work");
    // Partial pushes reserve the complete shared workspace without repeatedly
    // constructing twiddles or clearing unused large buffers.
    detail::CorrelationProjectionCache lazy(input,64*1024,64);
    const auto reservation=lazy.working_bytes(),initial_allocation=lazy.allocated_bytes();
    check(lazy.fft_available(64) && !lazy.fft_initialized() && initial_allocation<reservation,
        "partial amplitude push eagerly constructed its FFT or arenas");
    rejects([&]{lazy.fft_energy(std::span(input).first(64),3,20,cancel.get_token());},
        "cancelled lazy FFT began work");
    check(!lazy.fft_initialized() && lazy.allocated_bytes()==initial_allocation,
        "cancelled FFT allocated lazy scratch");
    const auto energy=lazy.fft_energy(std::span(input).first(64),3,20);
    check(energy>=0 && lazy.fft_initialized() && lazy.allocated_bytes()>initial_allocation &&
        lazy.allocated_bytes()<=reservation && lazy.working_bytes()==reservation,
        "lazy FFT changed its reservation or exceeded the workspace");
    detail::CorrelationProjectionCache spectra(input,64*1024,64);unsigned transforms=0;
    const auto fill=[&](std::span<long double> out){++transforms;for(std::size_t j=0;j<out.size();++j)out[j]=j*2;};
    const auto row=spectra.spectrum(input,0,64,3,20,512,fill);
    check(row && row.prefix.back()==40 && transforms==1,"shared spectrum prefix did not compute");
    check(!spectra.fft_initialized() && spectra.allocated_bytes()<=spectra.working_bytes(),
        "external spectrum callback unnecessarily initialized the FFT");
    check(spectra.spectrum(input,0,64,3,20,512,fill) && transforms==1,"identical spectrum was recomputed");
    spectra.spectrum(input,64,64,3,20,512,fill);check(transforms==2,"different spectrum origin reused stale input");
    rejects([&]{spectra.spectrum(input,0,64,3,20,512,fill,cancel.get_token());},"spectrum hit bypassed cancellation");
    check(spectra.working_bytes()<=64*1024,"spectrum cache exceeded its workspace");
    auto clipped=search_for(c);clipped.start_offset_seconds=0;clipped.input_sample_origin=13;
    PatternCorrelator partial(c,clipped,4*1024*1024,options());
    std::vector<float> empty(static_cast<std::size_t>(symbol_sample_count(c)+n));partial.push(empty);
    check(!partial.initial_search_complete(),"initial FFT alignment discard manufactured full-symbol coverage");
    partial.finish();check(!partial.initial_search_complete(),"EOF supplied the missing full symbol");
}
}
int main() {
    try {conservative_bound();factored_statistics();arithmetic_and_exact_reuse();queued_publication_without_input();coverage_and_fallback();bits_progress_and_physical_end();structured_rejection();coarse_channels();shared_frontend_identity();precision_rows_and_bounded_scratch();finite_energy_ranges();
        std::cout<<"orthogonal amplitude receiver checks passed\n";return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
