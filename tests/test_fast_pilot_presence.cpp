#include "datapump/fast/modem.hpp"
#include "datapump/fast/preset.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <optional>
#include <random>
#include <stdexcept>
#include <vector>

using namespace datapump::fast;
namespace {
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}

void interrupted_pilot(Profile p,bool noisy_tail,bool phase_step=false) {
    constexpr std::size_t count=4;
    std::mt19937 random(71731);
    std::vector<std::uint8_t> source(count*physical_interval_bits);
    for(auto& bit:source)bit=static_cast<std::uint8_t>(random()&1);
    std::size_t next=0;
    Transmitter tx(p,[&](std::span<std::uint8_t> output) {
        if(next==source.size())return false;
        std::copy_n(source.begin()+static_cast<std::ptrdiff_t>(next),output.size(),output.begin());
        next+=output.size();return true;
    });
    std::vector<float> received;
    Receiver rx(p,[&](std::span<const float> soft) {
        require(received.size()+soft.size()<=source.size(),"pilot recovery invented a physical interval");
        received.insert(received.end(),soft.begin(),soft.end());
    });
    std::optional<Receiver> eof_probe;
    if(noisy_tail)eof_probe.emplace(p,[](std::span<const float>){});
    bool eof_checked=!noisy_tail;
    // Mute only the short waveform section centered on the first pilot word.
    // The 64 preceding payload symbols and all subsequent groups remain live.
    // A 5-baud word tests that a <6-second disturbance cannot turn its much
    // longer payload-plus-pilot group into six seconds of observed absence.
    const double pulse_delay=std::ceil(6.4/p.rolloff);
    const auto pilot=preamble_symbols(p)+sync_symbols+p.pilot_spacing_symbols;
    const auto midpoint=(pilot+2+pulse_delay)*p.sample_rate/p.symbol_rate;
    const auto half_width=5*p.sample_rate/p.symbol_rate;
    require(2*half_width/p.sample_rate<6,"pilot fixture disturbance exceeded physical-end threshold");
    std::array<float,4093> block{};std::uint64_t at=0;
    const double gain=noisy_tail?.05:1;
    while(!tx.finished()) {
        const auto n=tx.read(block);
        for(std::size_t j=0;j<n;++j) {
            block[j]=static_cast<float>(gain*block[j]);
            if(phase_step) {
                if(double(at+j)>=midpoint-half_width)block[j]=-block[j];
            } else if(std::abs(double(at+j)-midpoint)<half_width)block[j]=0;
        }
        rx.push(std::span<const float>(block).first(n));at+=n;
        if(eof_probe) {
            eof_probe->push(std::span<const float>(block).first(n));
            if(eof_probe->progress().acquired&&double(at)/p.sample_rate>
                (pilot+2*(p.pilot_spacing_symbols+pilot_symbols))/p.symbol_rate) {
                eof_probe->finish();
                require(!eof_probe->progress().physical_complete,"input EOF manufactured pilot-stream completion");
                eof_probe.reset();eof_checked=true;
            }
        }
        if(rx.progress().physical_complete)
            std::cerr<<"premature pilot end: baud="<<p.symbol_rate<<" seconds="<<double(at)/p.sample_rate<<'\n';
        require(!rx.progress().physical_complete,"short pilot disturbance ended a continuing signal");
    }
    require(rx.progress().acquired,"pilot fixture never acquired initial marker");
    require(eof_checked,"acquired-stream EOF control was not exercised");
    // A realistic noise floor after the last transmitted symbol must still
    // reach physical end. Its power is 13 dB below the signal in the actual
    // occupied audio band; it is not silently replaced by zero PCM.
    const auto power=p.amplitude*p.amplitude*gain*gain/2;
    const auto sigma=noisy_tail?std::sqrt(power*p.sample_rate/
        (2*occupied_bandwidth_hz(p)*std::pow(10.,13./10))):0;
    std::normal_distribution<float> noise(0,static_cast<float>(sigma));
    const auto feed=[&](std::uint64_t samples) {
        while(samples) {
            const auto n=std::min<std::uint64_t>(samples,block.size());
            for(std::size_t j=0;j<n;++j)block[j]=noisy_tail?noise(random):0;
            rx.push(std::span<const float>(block).first(n));samples-=n;
        }
    };
    const auto partial=static_cast<std::uint64_t>(p.sample_rate*5.5);
    feed(partial);
    require(!rx.progress().physical_complete,"pilot presence counted fewer than six absent seconds");
    require(received.size()==source.size(),"bad pilot changed fixed interval positions");
    std::size_t erased_first=0,later_errors=0,later_erasures=0;
    for(std::size_t j=0;j<physical_interval_bits;++j)erased_first+=received[j]==0;
    for(std::size_t j=physical_interval_bits;j<source.size();++j) {
        later_errors+=(received[j]>0)!=bool(source[j]);later_erasures+=received[j]==0;
    }
    require(erased_first>0,"damaged pilot did not leave explicit timed erasures");
    require(later_errors==0&&later_erasures==0,"later good pilot groups did not resume exact decoding before another marker");
    feed(end_silence_samples(p)-partial);
    rx.finish();
    require(rx.progress().physical_complete,"trailing silence/noise did not finish pilot recovery fixture");
    std::cout<<"pilot recovery baud="<<p.symbol_rate<<" erased_first="<<erased_first
        <<" exact_later_intervals="<<count-1<<" noisy_tail="<<noisy_tail<<" phase_step="<<phase_step<<'\n';
}

void noise_only(Profile p) {
    std::mt19937 random(88216);std::normal_distribution<float> noise(0,.015F);
    std::size_t intervals=0;Receiver rx(p,[&](std::span<const float>){++intervals;});
    std::array<float,4093> block{};
    auto left=static_cast<std::uint64_t>(p.sample_rate)*14;
    while(left) {
        const auto n=std::min<std::uint64_t>(left,block.size());
        for(std::size_t j=0;j<n;++j)block[j]=noise(random);
        rx.push(std::span<const float>(block).first(n));left-=n;
    }
    rx.finish();
    require(!rx.progress().acquired&&!rx.progress().physical_complete&&intervals==0,
        "noise-only input manufactured acquisition or a completed reception");
}

void interrupted_marker(Profile p) {
    constexpr std::size_t count=12;
    require(p.symbol_rate==5&&p.marker_spacing_intervals==4,
        "marker phase fixture must cover a twelve-second scheduled marker");
    std::mt19937 random(57931);
    std::vector<std::uint8_t> source(count*physical_interval_bits);
    for(auto& bit:source)bit=static_cast<std::uint8_t>(random()&1);
    std::size_t next=0;
    Transmitter tx(p,[&](std::span<std::uint8_t> output) {
        if(next==source.size())return false;
        std::copy_n(source.begin()+static_cast<std::ptrdiff_t>(next),output.size(),output.begin());
        next+=output.size();return true;
    });
    std::vector<float> received;
    Receiver rx(p,[&](std::span<const float> soft) {
        require(received.size()+soft.size()<=source.size(),"marker recovery invented a physical interval");
        received.insert(received.end(),soft.begin(),soft.end());
    });
    // The negative controls share a valid acquired prefix, then lose the
    // transmitter at this same scheduled marker. Noise/energy alone must not
    // satisfy the extra presence test intended for a coherent phase step.
    Receiver silence_probe(p,[](std::span<const float>){});
    Receiver noise_probe(p,[](std::span<const float>){});
    const auto marker_start=(preamble_symbols(p)+total_interval_symbols(p,4)+
        std::ceil(6.4/p.rolloff))*p.sample_rate/p.symbol_rate;
    const auto midpoint=marker_start+sync_symbols*.5*p.sample_rate/p.symbol_rate;
    constexpr double gain=.05;
    const auto power=p.amplitude*p.amplitude*gain*gain/2;
    const auto sigma=std::sqrt(power*p.sample_rate/
        (2*occupied_bandwidth_hz(p)*std::pow(10.,13./10)));
    std::normal_distribution<float> noise(0,static_cast<float>(sigma));
    std::array<float,4093> block{},silent{},noisy{};
    std::uint64_t at=0;
    while(!tx.finished()) {
        const auto n=tx.read(block);
        for(std::size_t j=0;j<n;++j) {
            block[j]=static_cast<float>(gain*block[j]);
            const bool lost=double(at+j)>=marker_start;
            silent[j]=lost?0:block[j];noisy[j]=lost?noise(random):block[j];
            if(double(at+j)>=midpoint)block[j]=-block[j];
        }
        rx.push(std::span<const float>(block).first(n));
        if(!silence_probe.progress().physical_complete)
            silence_probe.push(std::span<const float>(silent).first(n));
        if(!noise_probe.progress().physical_complete)
            noise_probe.push(std::span<const float>(noisy).first(n));
        at+=n;
        if(rx.progress().physical_complete)
            std::cerr<<"premature marker end: baud="<<p.symbol_rate<<" seconds="<<double(at)/p.sample_rate<<'\n';
        require(!rx.progress().physical_complete,"mid-marker phase step ended a continuing signal");
    }
    require(silence_probe.progress().physical_complete&&noise_probe.progress().physical_complete,
        "scheduled marker silence/noise manufactured continuing presence");
    // Leave the final pulse and equalizer lookahead enough time to drain, but
    // do not let an EOF substitute for the physical absence observation.
    const auto partial=static_cast<std::uint64_t>(p.sample_rate*5.5);
    const auto feed_noise=[&](std::uint64_t samples) {
        while(samples) {
            const auto n=std::min<std::uint64_t>(samples,block.size());
            for(std::size_t j=0;j<n;++j)block[j]=noise(random);
            rx.push(std::span<const float>(block).first(n));samples-=n;
        }
    };
    feed_noise(partial);
    require(!rx.progress().physical_complete,"marker phase fixture counted fewer than six absent seconds");
    require(received.size()==source.size(),"failed full marker changed fixed interval positions");
    for(std::size_t j=0;j<source.size();++j) {
        if(j>=4*physical_interval_bits&&j<8*physical_interval_bits)
            require(received[j]==0,"failed full marker admitted payload instead of timed erasures");
        else require(received[j]!=0&&(received[j]>0)==bool(source[j]),
            "next valid full marker did not resume exact payload at its original position");
    }
    feed_noise(end_silence_samples(p)-partial);
    rx.finish();
    require(rx.progress().physical_complete,"noise tail did not finish marker phase fixture");
    std::cout<<"marker phase recovery baud="<<p.symbol_rate
        <<" erased_intervals=4 exact_intervals=8 acquired_silence_noise_controls=2\n";
}
}
int main() {try {
    const auto p=resolve_snr_preset(Channel::acoustic,-10).profile;
    require(!p.acoustic_ofdm&&p.symbol_rate>70&&p.symbol_rate<80&&p.marker_spacing_intervals==4,
        "acoustic pilot regression no longer covers the reported preset");
    interrupted_pilot(p,true);
    interrupted_pilot(p,false,true);
    auto slow=p;slow.symbol_rate=5;interrupted_pilot(slow,false);
    interrupted_marker(slow);
    noise_only(p);
    std::cout<<"Fast pilot presence tests passed\n";
}catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}}
