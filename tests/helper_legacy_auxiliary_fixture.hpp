#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>
#include <stdexcept>

namespace datapump::test {

// Fixed original N0CALL waveform for the legacy weak-frequency regression.
// See fixtures/helper-legacy-auxiliary/README.md for the source binding and
// comparison against the actual original Generator. This is deliberately
// independent of the current metadata encoder and constellation.
class LegacyAuxiliaryFixture {
    double frequency_,amplitude_;
    bool encrypted_time_;
    static constexpr double tau=2*std::numbers::pi;
    static constexpr double pilot_spacing_hz=400;
    static constexpr double low_relative_power=.501187233627272285;
    static constexpr double auxiliary_relative_power=.125892541179416721;
    static constexpr double steady_power=1+low_relative_power+auxiliary_relative_power;
    inline static const double pilot_amplitude_fraction=std::sqrt(1/steady_power);
    inline static const double auxiliary_amplitude_fraction=std::sqrt(auxiliary_relative_power/steady_power);
    static constexpr std::array<std::uint32_t,3> words{108336,1106124,1261312};
    static constexpr std::array<std::array<std::uint32_t,3>,50> encrypted_words=
#include "fixtures/helper-legacy-auxiliary/encrypted-timecode.inc"
    ;
    static double smooth(long double position) {
        const auto u=static_cast<double>(std::clamp(position,0.L,1.L));
        return u*u*u*(10+u*(-15+6*u));
    }
    static double auxiliary_smooth(long double position) {
        const auto u=static_cast<double>(std::clamp(position,0.L,1.L));
        return u*u*u*u*(35+u*(-84+u*(70-20*u)));
    }
    static std::complex<double> point(std::uint32_t word) {
        return std::polar(.9+.1*static_cast<double>(word>>11)/1023,
                          tau*static_cast<double>(word&2047U)/2048);
    }
    std::complex<double> original_extract(long double time) const {
        auto local=std::fmod(time,2.L);
        if(local<0)local+=2;
        local-=std::floor(local/.4L)*.4L;
        const auto dwell=static_cast<std::size_t>(std::llround((time-local)*2.5L));
        if(encrypted_time_&&dwell>=encrypted_words.size())throw std::runtime_error("legacy encrypted fixture duration exceeded");
        const auto& symbols=encrypted_time_?encrypted_words[dwell]:words;
        const auto envelope=smooth(std::min(local,.4L-local)/.004L);
        const auto auxiliary_envelope=smooth(std::min(local,.4L-local)/.008L);
        std::complex<double> modulation{1,0};
        constexpr auto begin=.388L-words.size()*.008L;
        if(local>=begin) {
            const auto position=(local-begin)/.008L;
            const auto index=static_cast<std::size_t>(std::floor(position));
            if(index<symbols.size()) {
                const auto previous=index?point(symbols[index-1]):std::complex<double>{1,0};
                modulation=previous+(point(symbols[index])-previous)*auxiliary_smooth(position-index);
            }else if(index==symbols.size()) {
                const auto last=point(symbols.back());
                modulation=last+(std::complex<double>{1,0}-last)*auxiliary_smooth(position-index);
            }
        }
        const auto high_cycles=std::fmod(static_cast<long double>(frequency_+pilot_spacing_hz)*time,1.L);
        const auto high=amplitude_*envelope*(pilot_amplitude_fraction*
            std::polar(1.,tau*static_cast<double>(high_cycles)));
        auto composite=high;
        const auto cycles=std::fmod(static_cast<long double>(frequency_)*time,1.L);
        composite+=amplitude_*auxiliary_envelope*(auxiliary_amplitude_fraction*modulation*
            std::polar(1.,tau*static_cast<double>(cycles)));
        auto value=(composite-high)/auxiliary_amplitude_fraction;
        const auto fixture_local=std::fmod(time,.4L),edge=std::min(fixture_local,.4L-fixture_local);
        const auto gate=smooth(edge/.008L);
        if(gate>0)value*=smooth(edge/.004L)/gate;
        return value;
    }
public:
    LegacyAuxiliaryFixture(double frequency,double amplitude,bool encrypted_time=false)
        :frequency_(frequency),amplitude_(amplitude),encrypted_time_(encrypted_time) {}
    std::complex<double> analytic(long double time) const {
        auto value=original_extract(time);
        const auto local=std::fmod(time,.4L);
        if(local>=.332L&&local<.396L) {
            const auto position=(local-.332L)/.008L,index=std::floor(position);
            const auto h=smooth(position-index);
            const auto begin=time-local+.332L+index*.008L,end=begin+.008L;
            const auto oscillator=[&](long double at) {
                return std::polar(1.,tau*static_cast<double>(std::fmod(frequency_*at,1.L)));
            };
            value=((1-h)*original_extract(begin)/oscillator(begin)+h*original_extract(end)/oscillator(end))*oscillator(time);
        }
        const auto first=static_cast<double>(std::clamp((local-.236L)/.008L,0.L,1.L));
        const auto second=static_cast<double>(std::clamp((local-.276L)/.008L,0.L,1.L));
        value*=(1-2*first*first*first*(10+first*(-15+6*first)))*
               (1-2*second*second*second*(10+second*(-15+6*second)));
        return value;
    }
    float mix(long double time) const {return static_cast<float>(analytic(time).real());}
};

} // namespace datapump::test
