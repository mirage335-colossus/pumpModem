#include "datapump/fast/modem.hpp"
#include "datapump/fast/preset.hpp"
#include "datapump/fast/ldpc.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>

// Freeze this fixture's Gaussian PCM, as in test_gui_bitmaps.cpp.
// std::normal_distribution may use a different polar coordinate order or
// uniform-float conversion on another standard library with the same seed.
// Preserve the existing GNU float sequence, including its rejection rules.
class FixtureGaussian {
    float mean_, deviation_, saved_=0;
    bool has_saved_=false;
    static float uniform(std::mt19937& random) {
        return std::min(std::ldexp(static_cast<float>(random()), -32),
                        std::nextafter(1.0f, 0.0f));
    }
public:
    FixtureGaussian(float mean,float deviation):mean_(mean),deviation_(deviation) {}
    float operator()(std::mt19937& random) {
        float value;
        if(has_saved_) {value=saved_;has_saved_=false;}
        else {
            float x,y,radius;
            do {
                x=2.0f*uniform(random)-1.0f;
                y=2.0f*uniform(random)-1.0f;
                radius=x*x+y*y;
            } while(radius==0.0f||radius>1.0f);
            const float scale=std::sqrt(-2.0f*std::log(radius)/radius);
            saved_=x*scale;has_saved_=true;value=y*scale;
        }
        return value*deviation_+mean_;
    }
};

using namespace datapump::fast;
namespace {
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}

// Stream actual device-rate PCM rather than keeping minutes of low-baud audio
// in memory. One complete physical interval exercises acquisition, markers,
// pilots and fixed interval delivery. The noisy case carries one complete
// independently generated LDPC codeword, without a many-hour source cycle.
void sampled(Profile p,double reference_snr_db=100,double reference_bandwidth=2400) {
    const bool noisy=reference_snr_db<0;
    std::mt19937 random(61802);
    datapump::Bytes source;
    std::vector<std::uint8_t> sent;
    if(noisy) {
        source.resize(ldpc::data_bits(p.code_rate)/8);
        for(auto& value:source)value=static_cast<std::uint8_t>(random());
        sent=ldpc::interleave(ldpc::encode(source,p.code_rate));
        sent.resize((sent.size()+physical_interval_bits-1)/physical_interval_bits*physical_interval_bits,0);
    } else {
        sent.resize(physical_interval_bits);
        for(auto& bit:sent)bit=static_cast<std::uint8_t>(random()&1);
    }
    std::size_t source_at=0;
    Transmitter tx(p,[&](std::span<std::uint8_t> out) {
        require(out.size()==physical_interval_bits,"low-rate transmitter changed interval geometry");
        if(source_at==sent.size())return false;
        std::copy_n(sent.begin()+static_cast<std::ptrdiff_t>(source_at),out.size(),out.begin());
        source_at+=out.size();return true;
    });
    std::size_t intervals=0,errors=0,erasures=0,observations=0;
    bool finite_observations=true;
    std::vector<float> received;
    Receiver rx(p,[&](std::span<const float> soft) {
        require(soft.size()==physical_interval_bits,"low-rate receiver changed interval geometry");
        require(received.size()+soft.size()<=sent.size(),"low-rate receiver inserted an interval");
        for(std::size_t i=0;i<soft.size();++i) {
            errors+=(soft[i]>0)!=sent[received.size()+i];erasures+=soft[i]==0;
        }
        received.insert(received.end(),soft.begin(),soft.end());++intervals;
    },{},[&](std::complex<float> value) {
        finite_observations&=std::isfinite(value.real())&&std::isfinite(value.imag());
        ++observations;
    });
    const auto tx_workspace=tx.workspace_bytes(),rx_workspace=rx.workspace_bytes();
    require(rx_workspace<4*1024*1024,"low-rate RX allocated device-rate long filters");
    // Real white PCM noise has 2B/Fs of its power in a B-Hz positive-frequency
    // passband. SNR here refers to the original radio bandwidth, not the much
    // narrower matched filter. Lower received gain keeps actual PCM unclipped.
    const double gain=noisy?.01:1;
    const auto nominal_power=p.amplitude*p.amplitude*gain*gain/2;
    const auto sigma=noisy?std::sqrt(nominal_power*p.sample_rate/
        (2*reference_bandwidth*std::pow(10.,reference_snr_db/10))):0;
    FixtureGaussian noise(0,static_cast<float>(sigma));
    const auto corrupt=[&](std::span<float> block) {
        for(auto& value:block) {
            value=static_cast<float>(gain*value)+(noisy?noise(random):0);
            require(std::abs(value)<1,"low-rate test PCM clipped");
        }
    };
    std::array<float,4093> block{};
    corrupt(std::span<float>(block).first(137));rx.push(std::span<const float>(block).first(137));
    const auto started=std::chrono::steady_clock::now();
    while(!tx.finished()) {
        const auto count=tx.read(block);
        corrupt(std::span<float>(block).first(count));
        rx.push(std::span<const float>(block).first(count));
    }
    require(tx.samples_generated()==transmission_samples(p,sent.size()/physical_interval_bits),"low-rate waveform sample estimate differs");
    require(rx.progress().acquired,"low-rate preamble/marker acquisition failed");
    require(!rx.progress().physical_complete,"low-rate TX EOF fabricated physical completion");
    // Neither a partial silence nor an EOF can stand for six fully scored
    // seconds. Keep exactly the same noise density throughout the real tail.
    const auto partial=static_cast<std::uint64_t>(p.sample_rate*5.5);
    const auto feed_absence=[&](std::uint64_t count) {
        while(count) {
            const auto n=std::min<std::uint64_t>(block.size(),count);
            std::fill_n(block.begin(),n,0);corrupt(std::span<float>(block).first(n));
            rx.push(std::span<const float>(block).first(n));count-=n;
        }
    };
    feed_absence(partial);
    require(!rx.progress().physical_complete,"partial low-rate absence finished reception");
    feed_absence(end_silence_samples(p)-partial);
    rx.finish();
    const auto wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    std::cout<<"baud="<<p.symbol_rate<<" qam="<<p.constellation<<" reference_snr="<<reference_snr_db
        <<" narrowed_snr="<<reference_snr_db+10*std::log10(reference_bandwidth/occupied_bandwidth_hz(p))
        <<" intervals="<<intervals<<" errors="<<errors<<" erasures="<<erasures
        <<" complete="<<rx.progress().physical_complete<<" evm="<<rx.progress().evm
        <<" rx_workspace="<<rx_workspace<<" waveform_seconds="<<double(tx.samples_generated())/p.sample_rate
        <<" wall_seconds="<<wall<<'\n';
    require(rx.progress().physical_complete,"low-rate real trailing absence failed to complete");
    require(received.size()==sent.size()&&erasures==0,"low-rate physical interval was lost or erased");
    if(noisy) {
        received.resize(ldpc::coded_bits);
        const auto decoded=ldpc::decode(ldpc::deinterleave(received),p.code_rate);
        std::cout<<"negative-reference-SNR LDPC converged="<<decoded.converged
            <<" iterations="<<decoded.iterations<<'\n';
        require(decoded.converged&&decoded.bytes==source,"negative-reference-SNR physical LDPC frame failed");
    } else require(errors==0,"clean low-rate sampled interval changed bits");
    require(observations>2*preamble_symbols(p),"low-rate input observer stopped following physical symbols");
    require(finite_observations,"nonfinite low-rate display I/Q");
    require(rx.workspace_bytes()==rx_workspace&&tx.workspace_bytes()==tx_workspace,"low-rate workspace grew with airtime");
}

void minimum_baud_bounds() {
    auto p=capacity_profile(Channel::ssb);
    p.symbol_rate=1;p.sample_rate=192000;p.rolloff=.02;p.constellation=4;
    p.pilot_spacing_symbols=1024;validate(p);
    Transmitter tx(p,[](std::span<std::uint8_t>){return false;});
    Receiver rx(p,[](std::span<const float>){throw std::runtime_error("quiet input emitted an interval");});
    require(rx.workspace_bytes()<4*1024*1024,"one-baud receiver memory is not bounded");
    require(tx.workspace_bytes()<6*1024*1024,"one-baud transmitter memory is not bounded");
    require(end_silence_samples(p)>static_cast<std::uint64_t>(p.sample_rate)*1024,
        "one-baud sparse pilots need a complete observation window");
    std::array<float,8191> block{};require(tx.read(block)==block.size(),"one-baud streaming TX failed");
    block.fill(0);rx.push(block);rx.finish();
    require(!rx.progress().physical_complete,"one-baud EOF fabricated absence");
    bool rejected=false;try {rx.push(block);}catch(const std::logic_error&){rejected=true;}
    require(rejected,"low-rate receiver accepted PCM after EOF");
}
}
int main() {try {
    minimum_baud_bounds();
    auto p=capacity_profile(Channel::ssb);
    p.interleave_depth=1;p.constellation=16;p.rolloff=.2;
    p.marker_spacing_intervals=1;p.pilot_spacing_symbols=16;
    p.symbol_rate=5;sampled(p);
    p.symbol_rate=15;p.sample_rate=44100;
    p.marker_spacing_intervals=4;p.pilot_spacing_symbols=64;sampled(p);
    // Sparse pilots at the earlier 100-baud processing boundary need more
    // real absence than the original fixed 6.25 seconds. Both this boundary
    // and manually selected 500 baud must remain practical to stream.
    p=capacity_profile(Channel::wire);p.constellation=4;p.rolloff=.5;
    p.symbol_rate=100;p.pilot_spacing_symbols=256;p.marker_spacing_intervals=16;
    require(end_silence_samples(p)>p.sample_rate*8,"100-baud sparse-pilot absence estimate is too short");
    sampled(p);
    p.symbol_rate=500;sampled(p);
    // The narrow-baseband interpolation must also retain the precision of
    // the dense cable constellation when it is manually slowed down.
    p=capacity_profile(Channel::wire);p.symbol_rate=500;sampled(p);
    const auto weak=resolve_snr_preset(Channel::ssb,-10);
    require(!weak.profile.acoustic_ofdm&&weak.profile.symbol_rate<100,
        "negative-SNR preset did not select narrow single-carrier processing");
    sampled(weak.profile,weak.expected_snr_db,weak.reference_bandwidth_hz);
    std::cout<<"Fast low-rate sampled tests passed\n";
}catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}}
