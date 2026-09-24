#pragma once
// Regression fixture only: never linked into the GUI, CLI or an audio session.
#include "datapump/legacy/modem.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <stdexcept>
namespace legacy_regression {
inline constexpr double reference_bandwidth_hz=3000;
// Fixed regression reference, not a measured RF sensitivity claim. The
// sampled PSK125 complete-text transition falls between -5 and +5 dB.
inline constexpr double psk125_design_failure_db=0;
inline constexpr double minimum_snr_db=psk125_design_failure_db-25;
inline constexpr double maximum_snr_db=psk125_design_failure_db+25;
class Gaussian {
    std::uint64_t state_;
    double uniform() {
        state_^=state_>>12;state_^=state_<<25;state_^=state_>>27;
        return (static_cast<double>((state_*2685821657736338717ULL)>>11)+0.5)/9007199254740992.0;
    }
public:
    explicit Gaussian(std::uint64_t seed):state_(seed?seed:1){}
    double next(){const auto a=uniform(),b=uniform();return std::sqrt(-2*std::log(a))*std::cos(2*std::numbers::pi*b);}
};
inline double noise_sigma(double signal_power,double snr_db) {
    if(!std::isfinite(snr_db)||snr_db<minimum_snr_db||snr_db>maximum_snr_db)
        throw std::invalid_argument("Legacy regression SNR must stay within 25 dB of the PSK125 design point");
    if(!std::isfinite(signal_power)||signal_power<0)throw std::invalid_argument("Invalid signal power");
    return std::sqrt(signal_power*std::pow(10,-snr_db/10));
}
// FLDigi LinSim's fixed 400..3400 Hz audio noise band. A normalized
// windowed-sinc FIR keeps the requested output noise power independent of its
// finite transition bands. This filter and capture geometry are never controls.
// Reference: https://www.w1hkj.org/files/test_suite/guide.html
class AudioNoise {
    Gaussian gaussian_;
    std::array<double,257> taps_{},history_{};
    std::size_t position_=0;
public:
    explicit AudioNoise(std::uint64_t seed):gaussian_(seed) {
        constexpr double lo=400./datapump::legacy::sample_rate,hi=3400./datapump::legacy::sample_rate;
        double energy=0;
        for(std::size_t i=0;i<taps_.size();++i) {
            const auto t=static_cast<double>(i)-128;
            const auto sinc=t==0?2*(hi-lo):(std::sin(2*std::numbers::pi*hi*t)-std::sin(2*std::numbers::pi*lo*t))/(std::numbers::pi*t);
            taps_[i]=sinc*(.54-.46*std::cos(2*std::numbers::pi*i/256));energy+=taps_[i]*taps_[i];
        }
        for(auto& tap:taps_)tap/=std::sqrt(energy);
        for(std::size_t i=0;i<history_.size();++i)(void)next();
    }
    double next() {
        history_[position_]=gaussian_.next();double out=0;
        for(std::size_t i=0;i<taps_.size();++i)out+=taps_[i]*history_[(position_+history_.size()-i)%history_.size()];
        position_=(position_+1)%history_.size();return out;
    }
};
struct Result {std::string received;double measured_noise_power=0,signal_power=0;};
inline Result simulate(datapump::legacy::Config config,const std::string& text,double snr_db,std::uint64_t seed=1) {
    using namespace datapump::legacy;
    (void)noise_sigma(1,snr_db);
    std::vector<float> pcm;std::array<float,317> block{};Transmitter tx(config,text);
    for(auto n=tx.read(block);n;n=tx.read(block))pcm.insert(pcm.end(),block.begin(),block.begin()+static_cast<std::ptrdiff_t>(n));
    Result out;for(auto sample:pcm)out.signal_power+=sample*sample;
    out.signal_power/=static_cast<double>(pcm.size());
    const auto sigma=noise_sigma(out.signal_power,snr_db);AudioNoise noise(seed);
    Receiver rx(config,[&](std::string_view chars){out.received+=chars;});
    // Fixed unrelated capture epoch and fixed tail; neither is a modem header
    // nor a received length. Only SNR varies between channel fixtures.
    const auto total=137+pcm.size()+3*sample_rate;
    for(std::size_t first=0;first<total;first+=block.size()) {
        const auto count=std::min(block.size(),total-first);
        for(std::size_t i=0;i<count;++i) {
            const auto at=first+i;const auto n=sigma*noise.next();out.measured_noise_power+=n*n;
            block[i]=static_cast<float>((at>=137&&at-137<pcm.size()?pcm[at-137]:0)+n);
        }
        rx.push(std::span(block).first(count));
    }
    out.measured_noise_power/=static_cast<double>(total);
    return out;
}
}
