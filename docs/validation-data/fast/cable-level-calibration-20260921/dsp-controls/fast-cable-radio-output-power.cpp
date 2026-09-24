#include <chrono>
#include "datapump/fast/modem.hpp"
#include "datapump/fast/preset.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace datapump;
using namespace datapump::fast;
namespace {
void require(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}
struct Power {
    double rms=0,peak=0;
    std::uint64_t samples=0;
};
Power settled_power(const Profile& profile) {
    // The real transmitter consumes deterministic, uniformly distributed coded
    // bits. Keep supplying complete intervals so EOF, source padding and filter
    // tails cannot lower the measured average power.
    std::uint32_t random=0x74be139d;
    Transmitter transmitter(profile,[&](std::span<std::uint8_t> bits) {
        for(auto& bit:bits) {
            random^=random<<13;random^=random>>17;random^=random<<5;
            bit=static_cast<std::uint8_t>(random&1);
        }
        return true;
    });
    std::uint64_t skip=0,count=0;
    if(profile.acoustic_ofdm) {
        const auto block=std::uint64_t(profile.ofdm_fft_size)+profile.ofdm_prefix_samples;
        skip=preamble_symbols(profile)*block;
        count=4*block;
    } else {
        // Leave the training and first marker completely behind the finite RRC
        // before measuring. 4096 data/pilot symbols give a stable energy mean
        // while the below-1000-baud path still uses its production interpolator.
        const auto samples_per_symbol=profile.sample_rate/profile.symbol_rate;
        skip=static_cast<std::uint64_t>(std::ceil((preamble_symbols(profile)+
            pulse_tail_symbols(profile)+sync_symbols)*samples_per_symbol));
        count=static_cast<std::uint64_t>(std::ceil(4096*samples_per_symbol));
    }
    std::array<float,4096> buffer{};
    std::uint64_t position=0;double squares=0;
    Power result;
    while(position<skip+count) {
        const auto wanted=static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(),skip+count-position));
        const auto received=transmitter.read(std::span(buffer).first(wanted));
        require(received==wanted,"Random-source transmitter stopped before the power window");
        for(std::size_t i=0;i<received;++i)if(position+i>=skip) {
            const auto value=static_cast<double>(buffer[i]);
            require(std::isfinite(value),"Transmitter produced nonfinite PCM");
            squares+=value*value;result.peak=std::max(result.peak,std::abs(value));++result.samples;
        }
        position+=received;
    }
    require(result.samples==count,"Output-power window has an incorrect sample count");
    result.rms=std::sqrt(squares/count);
    require(result.rms>0,"Transmitter produced a silent power window");
    return result;
}
void compare(const char* name,const Power& value,const Power& reference) {
    const auto difference=20*std::log10(value.rms/reference.rms);
    std::cout<<name<<" RMS "<<value.rms<<", peak "<<value.peak
        <<", relative power "<<difference<<" dB, samples "<<value.samples<<'\n';
    if(std::abs(difference)>.5)
        throw std::runtime_error(std::string(name)+" changed acoustic output power by more than 0.5 dB");
}
}

int main() {try {
    const auto started=std::chrono::steady_clock::now();
    auto cable=profile(Channel::wire);cable.amplitude=.30;
    auto radio=profile(Channel::ssb);radio.amplitude=.30;
    std::cout<<std::setprecision(12);
    std::cout<<"label,qam,baud,rolloff,carrier_hz,amplitude,rms,peak,crest_db,rms_vs_nominal_db,window_samples,window_seconds\n";
    const auto emit=[&](const char* label,const Profile& p) {
        const auto result=settled_power(p);
        const auto nominal=p.amplitude/std::sqrt(2.);
        std::cout<<label<<','<<p.constellation<<','<<p.symbol_rate<<','<<p.rolloff<<','<<p.carrier_hz<<','
            <<p.amplitude<<','<<result.rms<<','<<result.peak<<','<<20*std::log10(result.peak/result.rms)<<','
            <<20*std::log10(result.rms/nominal)<<','<<result.samples<<','<<double(result.samples)/p.sample_rate<<'\n';
    };
    emit("cable_full",cable);
    cable.symbol_rate/=10;emit("cable_tenth",cable);
    cable.symbol_rate/=10;emit("cable_hundredth",cable);
    emit("radio_full_64qam",radio);
    radio.symbol_rate/=10;radio.constellation=16;emit("radio_tenth_16qam",radio);
    const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    std::cerr<<"seed=0x74be139d; settled_window_symbols=4096; elapsed_seconds="<<elapsed<<'\n';
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
