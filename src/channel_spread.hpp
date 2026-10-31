#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>
#include <stop_token>

namespace datapump::modem::detail {
// Illustrative two-ray channel: a direct ray and a delayed ray have equal
// complex amplitudes 1/sqrt(2). The selected delay is the excess delay of the
// second ray, and the selected Doppler spread is the frequency separation of
// the rays. This is not an RMS-delay or measured scattering-spectrum model.
// A source preview regenerates the delayed ray without duration-dependent
// sample storage. Apply this before stateful post-channel perturbations.
class SpreadRay {
public:
    SpreadRay(double sample_rate,double doppler_hz,std::uint64_t seed)
        : doppler_hz_(doppler_hz) {
        auto bits=seed^0x42eb1f6ab7496fdbULL;
        bits+=0x9e3779b97f4a7c15ULL;
        bits=(bits^(bits>>30))*0xbf58476d1ce4e5b9ULL;
        bits=(bits^(bits>>27))*0x94d049bb133111ebULL;
        bits^=bits>>31;
        phase_=2*std::numbers::pi*(static_cast<double>(bits>>11)+.5)/9007199254740992.;
        rotation_=std::polar(1.,phase_);
        step_=std::polar(1.,2*std::numbers::pi*doppler_hz/sample_rate);
    }

    std::complex<double> process(std::complex<double> direct,std::complex<double> delayed) {
        constexpr double ray_gain=0.7071067811865475244;
        const auto result=ray_gain*(direct+rotation_*delayed);
        rotation_*=step_;
        if((++count_&4095U)==0)rotation_/=std::abs(rotation_);
        return result;
    }
    double phase_radians() const {return phase_;}
    double doppler_hz() const {return doppler_hz_;}
private:
    std::complex<double> rotation_{},step_{};
    double phase_=0,doppler_hz_=0;
    std::uint64_t count_=0;
};

// Use the same polyphase interpolation geometry as the direct streaming ray.
// preview_analytic_at reconstructs the actual source waveform at a physical
// source coordinate and does not advance the transmitter.
template<class Source,class CoefficientRow>
std::complex<double> preview_spread_ray(Source& source,long double position,
                                        std::uint64_t total,CoefficientRow coefficient_row,
                                        std::stop_token stop) {
    if(position<0 || position>=static_cast<long double>(total))return {};
    constexpr std::size_t taps=16,phases=256;
    const auto center=static_cast<std::uint64_t>(std::floor(position));
    const auto fractional=static_cast<double>(position-center)*phases;
    const auto phase=std::min(phases-1,static_cast<std::size_t>(fractional));
    const auto blend=fractional-static_cast<double>(phase);
    const auto first=coefficient_row(phase),second=coefficient_row(phase+1);
    std::complex<double> value{};
    for(std::size_t tap=0;tap<taps;++tap) {
        if((tap<7&&center<7-tap)||(tap>=7&&total-center<=tap-7))continue;
        const auto index=tap<7?center-(7-tap):center+(tap-7);
        const auto coefficient=first[tap]+blend*(second[tap]-first[tap]);
        value+=source.preview_analytic_at(index,stop)*coefficient;
    }
    return value;
}
} // namespace datapump::modem::detail
