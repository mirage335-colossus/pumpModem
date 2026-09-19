#include "datapump/pattern_correlator.hpp"
#include "datapump/pattern_receiver.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "../src/pattern_differential.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <random>
#include <tuple>

using namespace datapump;
namespace {
void check(bool ok,const char* text) {if(!ok)throw Error(text);}
constexpr std::size_t budget=16*1024*1024;
modem::Config config(bool keyed=true) {
    modem::Config c;c.sample_rate=256;c.carrier_hz=64;c.bandwidth_hz=128;
    c.spreading_factor=8192;c.scramble=keyed;c.stream_epoch=1800000041;
    for(std::size_t i=0;i<c.spreading_seed.size();++i)c.spreading_seed[i]=static_cast<std::uint8_t>(i*13+29);
    return c;
}
modem::PatternSearch search(bool enabled=true,unsigned workers=1) {
    modem::PatternSearch s;s.frequency_offsets_hz={0};s.start_offset_seconds=0;
    s.start_uncertainty_seconds=2./256;s.initial_stream_symbols=1;
    s.differential_window_seconds=enabled?.25:0;s.chunk_bits=1;
    s.worker_threads=workers;s.candidate_limit=32;s.track_limit=4;s.bit_limit=64;
    return s;
}
enum class Signal {drifting,noise,fragment,carrier};
std::vector<float> capture(const modem::Config& c,const Bytes& bits,Signal kind,std::uint64_t seed=1237) {
    modem::PatternTransmitter tx(bits,c,c.stream_epoch,0,false);
    std::vector<std::complex<double>> analytic(static_cast<std::size_t>(tx.total_samples()));
    check(tx.read_analytic(analytic)==analytic.size(),"incomplete differential test waveform");
    const auto padding=modem::pattern_pulse_padding_samples(c);
    const auto symbol=modem::symbol_sample_count(c);
    check(analytic.size()==bits.size()*symbol+2*padding,"differential receiver changed transmitted bit endpoint");
    std::vector<float> pcm(static_cast<std::size_t>((bits.size()+1)*symbol+16));
    std::mt19937_64 random(seed);std::normal_distribution<double> noise(0,1);
    for(std::size_t i=0;i<pcm.size();++i) {
        pcm[i]=static_cast<float>(noise(random));
        if(i>=bits.size()*symbol || kind==Signal::noise)continue;
        const auto within=i%symbol;
        if(kind==Signal::fragment && within>=symbol/4)continue;
        const auto t=static_cast<double>(i)/static_cast<double>(symbol);
        // Many rotations inside every historical quarter, with a varying
        // instantaneous frequency. The receiver knows neither trajectory nor
        // noise seed; the existing waveform's chips are completely unchanged.
        const auto phase=.73+2*std::numbers::pi*32*t+1.1*std::sin(2*std::numbers::pi*5*t);
        const auto value=kind==Signal::carrier?
            std::polar(std::sqrt(2*modem::nominal_signal_power),2*std::numbers::pi*c.carrier_hz*i/c.sample_rate):
            analytic[padding+i];
        pcm[i]+=static_cast<float>((1.2*std::polar(1.,phase)*value).real());
    }
    return pcm;
}
struct Result {Bytes bits;std::vector<modem::PatternBurst> events;std::vector<modem::PatternEvidence> candidates;std::size_t completions=0,working=0;};
template<class Receiver> Result run(Receiver& rx,const modem::Config& c,std::span<const float> pcm,std::size_t chunk,bool finish=false) {
    Result result;
    for(std::size_t offset=0;offset<pcm.size();) {
        const auto n=std::min(chunk,pcm.size()-offset);rx.push(pcm.subspan(offset,n));offset+=n;
        result.working=std::max(result.working,rx.working_bytes());check(result.working<=budget,"differential state exceeded DSP budget");
        for(auto e:rx.take_bursts()) {
            check(offset>=e.end_sample,"differential short window published a partial bit");
            if(e.complete) {
                check(offset>=e.end_sample+modem::pattern_absence_samples(c),"differential detector shortened physical absence");
                ++result.completions;
            }
            check(!e.missing_slots,"differential regression inserted an unknown bit");
            result.bits.insert(result.bits.end(),e.bits.begin(),e.bits.end());result.events.push_back(std::move(e));
        }
    }
    if(finish) {
        rx.finish();
        for(const auto& event:rx.take_bursts())check(!event.complete && event.bits.empty(),"EOF manufactured differential completion or a partial bit");
    }
    result.candidates=rx.candidates();return result;
}
Result receive(bool fft,const modem::Config& c,std::span<const float> pcm,bool enabled=true,std::size_t chunk=257,unsigned workers=1,bool finish=false,bool exact_start=false) {
    auto s=search(enabled,workers);
    if(exact_start)s.start_uncertainty_seconds=0;
    if(fft) {
        modem::PatternReceiver rx(c,budget,s);check(!rx.clock_windowed(),"FFT differential test silently used compact fallback");
        return run(rx,c,pcm,chunk,finish);
    }
    modem::PatternCorrelator rx(c,s,budget);return run(rx,c,pcm,chunk,finish);
}
void sampled_improvement() {
    const Bytes bits{0,0,1};
    for(bool keyed:{false,true}) {
        const auto c=config(keyed);const auto pcm=capture(c,bits,Signal::drifting);
        const auto noise=capture(c,bits,Signal::noise);
        double signal_energy=0,noise_energy=0;
        for(std::size_t i=0;i<bits.size()*modem::symbol_sample_count(c);++i) {
            const double s=pcm[i]-noise[i];signal_energy+=s*s;noise_energy+=noise[i]*noise[i];
        }
        const auto snr=10*std::log10(signal_energy/noise_energy);
        check(snr< -3,"sampled differential improvement requires below-noise PCM");
        for(bool fft:{false,true}) {
            const auto before=receive(fft,c,pcm,false),after=receive(fft,c,pcm);
            check(before.bits!=bits,"phase trajectory must defeat the previous four-quarter detector");
            if(after.bits!=bits) {
                for(const auto& e:after.candidates)std::cerr<<"candidate index "<<e.stream_symbol<<" bit "<<e.bit<<" score "<<e.score<<" threshold "<<e.admission_threshold<<'\n';
                throw Error(std::string(fft?"FFT":"compact")+(keyed?" keyed":" public")+
                    " differential receiver failed exact 001; bits="+std::to_string(after.bits.size()));
            }
            check(after.completions==1,"new detector failed whole-symbol physical completion");
            std::size_t polls=0;for(const auto& e:after.events)if(!e.bits.empty()) {++polls;check(e.bits.size()==1,"new detector batched pending bits");}
            check(polls==bits.size(),"missing immediate per-bit progress");
            std::cout<<(fft?"FFT":"compact")<<(keyed?" private":" public")<<" differential decoded 001 at "<<snr<<" dB sample SNR\n";
        }
    }
}
void rejection_and_boundaries() {
    const auto c=config();const Bytes bit{1};const auto symbol=modem::symbol_sample_count(c);
    for(bool fft:{false,true}) {
        for(auto kind:{Signal::noise,Signal::fragment,Signal::carrier}) {
            const auto out=receive(fft,c,capture(c,bit,kind));
            check(out.bits.empty()&&out.completions==0,"local differential detector admitted noise, a quarter fragment, or an unrelated carrier");
        }
        auto wrong=c;wrong.spreading_seed[0]^=1;
        check(receive(fft,wrong,capture(c,bit,Signal::drifting)).bits.empty(),"differential detector admitted a wrong key");
        auto pcm=capture(c,bit,Signal::drifting);
        const auto partial=receive(fft,c,std::span(pcm).first(static_cast<std::size_t>(symbol-1)),true,37,1,true,true);
        check(partial.bits.empty()&&partial.completions==0,"partial subpatterns or EOF manufactured a bit");
        const auto pending=receive(fft,c,std::span(pcm).first(static_cast<std::size_t>(symbol+6*c.sample_rate)),true,257,1,true);
        check(pending.bits==bit&&pending.completions==0,"six seconds or EOF completed a long differential bit");
    }
}
void chunk_worker_parity() {
    const auto c=config();const Bytes bits{0,1};const auto pcm=capture(c,bits,Signal::drifting);
    for(bool fft:{false,true}) {
        const auto small=receive(fft,c,pcm,true,37),large=receive(fft,c,pcm,true,2048,3);
        check(small.bits==bits&&large.bits==bits&&small.events.size()==large.events.size(),"worker or chunk boundaries changed differential progress");
        for(std::size_t i=0;i<small.events.size();++i) {
            const auto& a=small.events[i];const auto& b=large.events[i];
            check(std::tie(a.bits,a.first_sample,a.end_sample,a.complete,a.stream_first_sample,a.stream_first_symbol)==
                std::tie(b.bits,b.first_sample,b.end_sample,b.complete,b.stream_first_sample,b.stream_first_symbol),
                "worker or chunk boundaries changed differential stream identity");
            check(std::abs(a.score-b.score)<1e-8*std::max(1.,std::abs(a.score)),"differential score depends on chunking");
        }
    }
}
void bounded_long_symbol_state() {
    auto c=config();c.integration_seconds=100*3600.;
    auto s=search();s.differential_window_seconds=100;
    modem::PatternCorrelator hundred(c,s,budget);
    const auto samples=modem::detail::differential_window_samples(modem::symbol_sample_count(c),
        modem::pattern_chip_samples(c),c.sample_rate,s.differential_window_seconds);
    check(samples==100ULL*c.sample_rate,"a hundred-hour bit did not retain hundred-second local windows");
    c.integration_seconds*=10;
    modem::PatternCorrelator thousand(c,s,budget);
    check(hundred.working_bytes()==thousand.working_bytes()&&thousand.working_bytes()<budget,
          "differential receiver retained a growing list of local windows");

    c=config();const auto pcm=capture(c,{0},Signal::drifting);
    modem::PatternCorrelator enabled(c,search(),budget),baseline(c,search(false),budget);
    const auto prefix=static_cast<std::size_t>(modem::symbol_sample_count(c)/2);
    enabled.push(std::span(pcm).first(prefix));baseline.push(std::span(pcm).first(prefix));
    check(enabled.take_bursts().empty()&&baseline.take_bursts().empty(),"budget fixture unexpectedly finished a partial bit");
    const auto reduced=baseline.working_bytes();
    check(enabled.working_bytes()>reduced,"new local state was not accounted to the workspace");
    enabled.set_workspace_bytes(reduced);
    check(enabled.working_bytes()<=reduced&&enabled.drift_tolerant(),"dropping local state lost the existing four-section fit");
    enabled.push(std::span(pcm).subspan(prefix));baseline.push(std::span(pcm).subspan(prefix));
    check(enabled.take_bursts().size()==baseline.take_bursts().size(),"workspace shrink reset accumulated legacy evidence");
    const auto a=enabled.candidates(),b=baseline.candidates();
    check(a.size()==b.size(),"workspace shrink changed old detector diagnostics");
    for(std::size_t i=0;i<a.size();++i)check(a[i].score==b[i].score&&a[i].first_sample==b[i].first_sample&&a[i].end_sample==b[i].end_sample,
        "dropping differential state changed retained coherent/four-section evidence");
}
}
int main() {
    try {sampled_improvement();rejection_and_boundaries();chunk_worker_parity();bounded_long_symbol_state();
        std::cout<<"Sampled short-window differential receiver tests passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
