#include "datapump/pattern_correlator.hpp"
#include "datapump/pattern_code.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <random>

using namespace datapump;
namespace {
void check(bool condition,const char* message){if(!condition)throw Error(message);}
template<class Action>void rejects(Action action,const char* message){try{action();}catch(const Error&){return;}throw Error(message);}
modem::Config config() {
    modem::Config c;c.pattern_symbols=true;c.constellation_bits=1;c.spreading_factor=128;
    c.scramble=true;c.dsss=true;c.stream_epoch=8312;
    c.spreading_seed[3]=51;c.dsss_seed[13]=171;return c;
}
std::vector<float> waveform(const Bytes& bits,const modem::Config& c,std::size_t delay,double rate=1) {
    modem::PatternTransmitter source(bits,c,c.stream_epoch,0,false);
    std::vector<std::complex<double>> analytic(static_cast<std::size_t>(source.total_samples()));source.read_analytic(analytic);
    const auto count=delay+static_cast<std::size_t>(std::ceil(static_cast<double>(analytic.size())/rate))+2*modem::symbol_sample_count(c);
    std::vector<float> pcm(count);
    std::mt19937 random(529);std::normal_distribution<double> noise(0,.025);
    const auto phase=std::polar(1.7,.83);
    for(std::size_t i=0;i<count;++i) {
        std::complex<double> sample{};
        if(i>=delay) {
            const auto position=static_cast<double>(i-delay)*rate;
            const auto index=static_cast<std::size_t>(position);
            if(index<analytic.size()) {
                const auto fraction=position-static_cast<double>(index);
                sample=analytic[index]*(1-fraction);
                if(index+1<analytic.size())sample+=analytic[index+1]*fraction;
            }
        }
        pcm[i]=static_cast<float>((phase*sample).real()+noise(random));
    }
    return pcm;
}
std::vector<modem::PatternBurst> capture(const std::vector<float>& samples,const modem::Config& c,
                                      modem::PatternSearch search,std::size_t chunk) {
    modem::PatternCorrelator receiver(c,search,4*1024*1024);
    for(std::size_t offset=0;offset<samples.size();) {
        const auto n=std::min(chunk,samples.size()-offset);receiver.push(std::span(samples).subspan(offset,n));offset+=n;
        check(receiver.working_bytes()<=4*1024*1024,"streaming correlator exceeded its workspace");
    }
    receiver.finish();check(!receiver.synchronized(),"silence/end must clear correlator synchronization");
    return receiver.take_bursts();
}
const modem::PatternBurst& best(const std::vector<modem::PatternBurst>& bursts) {
    check(!bursts.empty(),"clock-window search did not detect the sampled bit burst");
    return *std::max_element(bursts.begin(),bursts.end(),[](const auto& a,const auto& b){return a.score<b.score;});
}
void sampled_bits_and_rates() {
    const auto c=config();const Bytes bits{0,0,1};const auto samples=waveform(bits,c,137);
    modem::PatternSearch search;search.start_offset_seconds=.023;search.start_uncertainty_seconds=.003;
    search.frequency_offsets_hz={0};
    const auto all=capture(samples,c,search,samples.size()),chunks=capture(samples,c,search,37);
    check(best(all).bits==bits && best(chunks).bits==bits,"unknown phase and non-chip-aligned three-bit burst must recover exactly");
    check(best(all).first_sample==best(chunks).first_sample,"input chunk boundaries must not select a different clock hypothesis");
    auto rate_search=search;rate_search.clock_errors_ppm={0,5000};rate_search.frequency_offsets_hz={0,c.carrier_hz*.005};
    const Bytes longer{0,1,1,0,1,0};
    const auto altered=capture(waveform(longer,c,137,1.005),c,rate_search,173);
    check(best(altered).bits==longer,"finite clock-rate and frequency hypotheses must jointly recover physical rate error");
    auto wrong=c;wrong.spreading_seed[0]^=0x5a;
    check(capture(samples,wrong,search,128).empty(),"incorrect pattern key must not acquire a strong waveform");
}
void late_clock_fragment() {
    const auto c=config();const Bytes bits{1,1,0,1,0};
    const auto full=waveform(bits,c,0);
    const auto crop=2*modem::symbol_sample_count(c);
    std::vector<float> fragment(full.begin()+static_cast<std::ptrdiff_t>(crop),full.end());
    modem::PatternSearch search;search.start_offset_seconds=-static_cast<double>(crop)/c.sample_rate;
    search.start_uncertainty_seconds=1./c.sample_rate;search.frequency_offsets_hz={0};
    const auto result=capture(fragment,c,search,211);
    check(best(result).bits==Bytes({0,1,0}) && best(result).first_stream_symbol==2,
          "clock-derived cropped reception must use the surviving symbols' actual stream positions");
}
void weak_prefix_does_not_borrow_confidence() {
    auto c=config();c.scramble=false;c.dsss=false;c.spreading_factor=64;
    constexpr std::size_t delay=137;
    const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    auto samples=waveform({1,0,0,1},c,delay);
    std::mt19937_64 random(197);std::normal_distribution<double> noise(0,3.5);
    for(std::size_t i=0;i<delay+symbol;++i)samples[i]+=static_cast<float>(noise(random));
    modem::PatternSearch search;search.start_offset_seconds=static_cast<double>(delay)/c.sample_rate;
    search.start_uncertainty_seconds=0;search.frequency_offsets_hz={0};
    modem::PatternCorrelator prefix(c,search,4*1024*1024);
    prefix.push(std::span(samples).first(delay+symbol+2));
    check(prefix.acquiring() && !prefix.synchronized(),
          "weak-prefix fixture must retain a candidate without admitting it");
    const auto result=capture(samples,c,search,127);
    check(best(result).bits==Bytes({0,0,1}) && best(result).first_sample>=delay+symbol,
          "a strong clock-window symbol cannot confirm an earlier weak noise candidate");
}
void bounded_hours_and_noise() {
    auto c=config();c.integration_seconds=4*3600;
    modem::PatternSearch search;search.start_offset_seconds=.03;search.start_uncertainty_seconds=.002;
    search.clock_errors_ppm={-100,0,100};search.frequency_offsets_hz={0};
    modem::PatternCorrelator long_receiver(c,search,1024*1024);
    const auto initial=long_receiver.working_bytes();
    check(long_receiver.diagnostics().pattern_score.has_value() && long_receiver.diagnostics().snr_db==0,
          "pattern evidence must not be reported as an SNR measurement");
    std::array<float,4096> noise{};std::mt19937 rng(8927);std::normal_distribution<float> normal(0,.1F);
    for(auto& sample:noise)sample=normal(rng);
    for(unsigned i=0;i<6;++i)long_receiver.push(noise);
    check(long_receiver.working_bytes()==initial && initial<256*1024,
          "hour-long symbol accumulation must retain only the finite hypothesis bank");
    long_receiver.finish();check(long_receiver.take_bursts().empty(),"a short prefix cannot fabricate a completed hour-long symbol");
    auto enormous=search;enormous.start_uncertainty_seconds=1000000;
    rejects([&]{modem::PatternCorrelator rejected(c,enormous,1024*1024);},"unaffordable half-chip coverage must reject, not silently coarsen");
    auto no_clock=search;no_clock.start_offset_seconds.reset();
    rejects([&]{modem::PatternCorrelator rejected(c,no_clock,1024*1024);},"constant-memory long search must require its clock coverage");
    auto seconds_window=search;seconds_window.start_uncertainty_seconds=2;
    seconds_window.frequency_offsets_hz.clear();seconds_window.clock_errors_ppm={0};
    modem::PatternCorrelator seconds_receiver(c,seconds_window,8*1024*1024);
    check(seconds_receiver.working_bytes()<8*1024*1024,
          "a four-hour symbol with the full five-frequency +/-2-second clock bank fits eight MiB");
    seconds_window.clock_errors_ppm={-100,0,100};
    rejects([&]{modem::PatternCorrelator rejected(c,seconds_window,8*1024*1024);},
            "three clock rates must not be silently admitted inside a one-rate memory budget");
    modem::PatternCorrelator three_rates(c,seconds_window,32*1024*1024);
    check(three_rates.working_bytes()<32*1024*1024,"finite three-rate seconds-wide coverage has an explicit affordable ceiling");
    c=config();modem::PatternCorrelator receiver(c,search,1024*1024);
    for(unsigned i=0;i<6;++i)receiver.push(noise);
    receiver.finish();check(receiver.take_bursts().empty(),"noise-only clock search must not manufacture a bit burst");
    modem::PatternCorrelator cancelled(c,search,1024*1024);std::stop_source stop;stop.request_stop();
    rejects([&]{cancelled.push(noise,stop.get_token());},"streaming long search must honor cancellation");
}
}
int main(){try{sampled_bits_and_rates();late_clock_fragment();weak_prefix_does_not_borrow_confidence();bounded_hours_and_noise();std::cout<<"Streaming clock-window pattern correlator tests passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
