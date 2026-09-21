#include "datapump/fast/modem.hpp"
#include "datapump/fast/codec.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>
using namespace datapump;
using namespace datapump::fast;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Profile config() {
    auto p=capacity_profile();p.channel=Channel::acoustic;p.acoustic_ofdm=true;
    p.constellation=16;p.code_rate=CodeRate::three_quarters;p.interleave_depth=1;p.amplitude=.2;
    return p;
}
std::vector<float> transmit(Profile p,const std::vector<Bytes>& bits) {
    std::size_t index=0,calls=0;Transmitter tx(p,[&](std::span<std::uint8_t> out){
        ++calls;
        if(index==bits.size())return false;std::copy(bits[index].begin(),bits[index].end(),out.begin());++index;return true;
    });
    std::vector<float> pcm;std::array<float,977> chunk{};
    for(;;){auto n=tx.read(chunk);if(!n)break;pcm.insert(pcm.end(),chunk.begin(),chunk.begin()+static_cast<std::ptrdiff_t>(n));}
    const auto calls_at_end=calls;
    require(tx.finished()&&!tx.read(chunk)&&!tx.read(chunk)&&calls==calls_at_end,"OFDM source revived after transmitter completion");
    require(pcm.size()==transmission_samples(p,bits.size()),"OFDM exact sample estimate");return pcm;
}
std::vector<float> resample(const std::vector<float>& input,double ppm) {
    if(ppm==0)return input;
    const auto ratio=1+ppm*1e-6;std::vector<float> output(static_cast<std::size_t>(input.size()*ratio));
    const auto pi=std::acos(-1.);
    for(std::size_t i=0;i<output.size();++i) {
        const auto time=i/ratio;const auto center=static_cast<std::int64_t>(time);double sum=0,weight=0;
        for(int j=-23;j<=24;++j) {
            const auto k=center+j;if(k<0||k>=static_cast<std::int64_t>(input.size()))continue;
            const auto t=k-time;
            const auto w=(std::abs(t)<1e-12?1.:std::sin(pi*t)/(pi*t))*(.42+.5*std::cos(pi*t/24)+.08*std::cos(2*pi*t/24));
            sum+=input[static_cast<std::size_t>(k)]*w;weight+=w;
        }
        output[i]=static_cast<float>(sum/weight);
    }
    return output;
}
void raw_case(double echo=0,double noise=0,double ppm=0,std::size_t echo_delay=1703,Profile p=config()) {
    std::mt19937 rng(9817);
    std::vector<Bytes> input(cycle_intervals(p),Bytes(physical_interval_bits));
    for(auto& interval:input)for(auto& bit:interval)bit=static_cast<std::uint8_t>(rng()&1);
    auto signal=transmit(p,input);std::vector<float> pcm(1371,0);pcm.insert(pcm.end(),signal.begin(),signal.end());
    pcm.resize(pcm.size()+48000*8);
    std::normal_distribution<float> gaussian(0,static_cast<float>(noise));
    for(std::size_t i=pcm.size();i-->0;)pcm[i]+=static_cast<float>(i>=echo_delay?echo*pcm[i-echo_delay]:0)+gaussian(rng);
    pcm=resample(pcm,ppm);
    std::vector<Bytes> received;Receiver rx(p,[&](std::span<const float> llr){Bytes out;for(auto v:llr)out.push_back(v>0);received.push_back(std::move(out));});
    for(std::size_t i=0;i<pcm.size();i+=1201)rx.push(std::span(pcm).subspan(i,std::min<std::size_t>(1201,pcm.size()-i)));
    std::size_t errors=0;for(std::size_t i=0;i<std::min(input.size(),received.size());++i)for(std::size_t j=0;j<physical_interval_bits;++j)errors+=input[i][j]!=received[i][j];
    std::cout<<"OFDM raw echo="<<echo<<" noise="<<noise<<" ppm="<<ppm<<" acquired="<<rx.progress().acquired<<" intervals="<<received.size()<<" errors="<<errors<<" clock="<<rx.progress().clock_error_ppm<<" EVM="<<rx.progress().evm<<'\n';
    require(rx.progress().acquired,"OFDM acquisition");require(rx.progress().physical_complete,"OFDM physical completion");
    require(received.size()==input.size(),"OFDM spurious or missing intervals");require(errors==0,"OFDM raw recovery");
}
void coded_and_absence() {
    auto p=config();p.constellation=64;
    Bytes source(4096);std::mt19937 rng(1913);for(auto& b:source)b=static_cast<std::uint8_t>(rng());
    StreamEncoder encoder(p,std::nullopt,byte_source(source));std::vector<Bytes> bits;
    for(;;){Bytes interval(physical_interval_bits);if(!encoder.next_interval(interval))break;bits.push_back(std::move(interval));}
    auto pcm=transmit(p,bits);
    const auto peak=std::abs(*std::max_element(pcm.begin(),pcm.end(),[](auto a,auto b){return std::abs(a)<std::abs(b);}));
    require(peak<.35,"OFDM cycle padding causes coherent clipping peak");
    StreamDecoder decoder(p,std::nullopt);Receiver rx(p,[&](std::span<const float> v){decoder.push_interval(v);});
    rx.push(pcm);rx.push(std::vector<float>(48000*5));
    require(!rx.progress().physical_complete,"OFDM completed before six seconds of whole absent blocks");
    rx.push(std::vector<float>(48000*2));require(rx.progress().physical_complete,"OFDM missing physical completion");
    decoder.finish(rx.progress().physical_complete);
    require(decoder.snapshot().complete,"OFDM full coded recovery");
    require(std::equal(source.begin(),source.end(),decoder.result()->bytes().begin(),decoder.result()->bytes().end()),"OFDM source mismatch");
    require(rx.progress().intervals==bits.size(),"OFDM phantom tail intervals");
    require(rx.workspace_bytes()<8*1024*1024,"OFDM workspace scales with caller chunk");
    const auto retained=rx.workspace_bytes(),intervals=rx.progress().intervals,symbols=rx.progress().symbols;
    rx.push(std::vector<float>(48000*30));
    require(rx.workspace_bytes()==retained&&rx.progress().intervals==intervals&&rx.progress().symbols==symbols,
        "OFDM post-completion input changed state or retained PCM");
    bool invalid_rejected=false;
    try{const std::array<float,1> invalid{std::numeric_limits<float>::quiet_NaN()};rx.push(invalid);}catch(const std::exception&){invalid_rejected=true;}
    require(invalid_rejected,"OFDM post-completion input bypassed finiteness validation");
    Receiver truncated(p,[](std::span<const float>){});truncated.push(pcm);truncated.finish();
    require(!truncated.progress().physical_complete,"OFDM EOF fabricated absence");
    std::cout<<"OFDM codec64QAM peak="<<peak<<" exact bytes="<<source.size()<<'\n';
}
void changing_gain_and_noisy_refresh() {
    auto p=config();
    Bytes source(20000);std::mt19937 rng(0x6d756c74);
    for(auto& byte:source)byte=static_cast<std::uint8_t>(rng());
    StreamEncoder encoder(p,std::nullopt,byte_source(source));std::vector<Bytes> bits;
    for(;;) {Bytes interval(physical_interval_bits);if(!encoder.next_interval(interval))break;bits.push_back(std::move(interval));}
    auto signal=transmit(p,bits);const auto block=p.ofdm_fft_size+p.ofdm_prefix_samples;
    const auto cycle=transmission_samples(p,cycle_intervals(p))/block-preamble_symbols(p);
    const auto refresh_start=(preamble_symbols(p)+cycle)*block;
    std::vector<float> pcm(1371,0);pcm.insert(pcm.end(),signal.begin(),signal.end());
    pcm.resize(pcm.size()+48000*8);
    std::normal_distribution<float> noise(0,.006F);
    // Long delayed reflection, slowly varying gain, and one noisy full-band
    // refresh exercise maintained estimation beyond the initial preamble.
    for(std::size_t i=pcm.size();i-->0;) {
        const auto echo=i>=1703?.45*pcm[i-1703]:0.;
        pcm[i]=static_cast<float>((pcm[i]+echo)*(1+.08*std::sin(double(i)/48000*.7)));
        if(i>=1371+refresh_start&&i<1371+refresh_start+block)pcm[i]+=noise(rng);
    }
    StreamDecoder decoder(p,std::nullopt);Receiver receiver(p,[&](std::span<const float> soft){decoder.push_interval(soft);});
    for(std::size_t i=0;i<pcm.size();i+=1201)receiver.push(std::span(pcm).subspan(i,std::min<std::size_t>(1201,pcm.size()-i)));
    decoder.finish(receiver.progress().physical_complete);
    require(decoder.snapshot().complete&&decoder.result()&&
        Bytes(decoder.result()->bytes().begin(),decoder.result()->bytes().end())==source,
        "OFDM changing gain/echo and noisy refresh lost the multi-cycle source");
}
void bad_training() {
    auto p=config();std::vector<Bytes> bits(cycle_intervals(p),Bytes(physical_interval_bits,1));auto pcm=transmit(p,bits);
    pcm.resize(2*(p.ofdm_fft_size+p.ofdm_prefix_samples));pcm.insert(pcm.begin(),1371,0);pcm.resize(pcm.size()+48000*8,0);
    std::size_t delivered=0,input_points=0;Receiver rx(p,[&](std::span<const float>){++delivered;},{},[&](std::complex<float>){++input_points;});rx.push(pcm);
    require(!rx.progress().acquired&&!delivered,"OFDM rejected held-out marker admitted a stream");
    require(rx.workspace_bytes()<8*1024*1024,"OFDM failed acquisition retained unbounded PCM");
    std::vector<float> noise(48000*12);std::mt19937 rng(378);std::normal_distribution<float> normal(0,.1F);
    for(auto& value:noise)value=normal(rng);
    rx.push(noise);require(!rx.progress().acquired&&!delivered,"OFDM noise acquired");
    require(input_points>0,"OFDM unsynchronized input observations missing");
    require(rx.workspace_bytes()<8*1024*1024,"OFDM noise workspace unbounded");
}
void missing_block() {
    auto p=config();std::mt19937 rng(3882);
    std::vector<Bytes> bits(cycle_intervals(p),Bytes(physical_interval_bits));
    for(auto& interval:bits)for(auto& bit:interval)bit=static_cast<std::uint8_t>(rng()&1);
    auto pcm=transmit(p,bits);const auto length=p.ofdm_fft_size+p.ofdm_prefix_samples;
    const auto missing=preamble_symbols(p)+2;
    std::fill(pcm.begin()+static_cast<std::ptrdiff_t>(missing*length),pcm.begin()+static_cast<std::ptrdiff_t>((missing+1)*length),0);
    pcm.resize(pcm.size()+48000*8,0);
    std::size_t intervals=0,erased=0,wrong=0;
    Receiver rx(p,[&](std::span<const float> soft){
        require(intervals<bits.size(),"OFDM missing block created phantom intervals");
        for(std::size_t i=0;i<soft.size();++i){erased+=soft[i]==0;if(soft[i]!=0)wrong+=(soft[i]>0)!=bool(bits[intervals][i]);}
        ++intervals;
    });
    rx.push(pcm);
    require(rx.progress().physical_complete&&intervals==bits.size(),"OFDM missing block deleted fixed positions");
    require(erased>2048&&!wrong,"OFDM missing block corrupted later fixed coordinates");
}
int main(){try{raw_case();raw_case(.6,.0001);raw_case(.45,.0001,0,3360);raw_case(0,0,100);raw_case(0,0,-100);coded_and_absence();changing_gain_and_noisy_refresh();bad_training();missing_block();auto sparse=config();sparse.ofdm_fft_size=16384;sparse.ofdm_pilot_stride=16;sparse.constellation=64;raw_case(.45,.0001,100,3360,sparse);std::cout<<"acoustic OFDM tests passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
