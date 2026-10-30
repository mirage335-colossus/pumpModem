#pragma once

#include "datapump/helper_carrier.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <optional>
#include <numbers>
#include <utility>

namespace datapump::test {

// Isolate the actual Generator's auxiliary component, then restore the
// historical component amplitude so existing noise/coverage assertions retain
// their original SNR. This is component coverage, not composite-pilot isolation.
class AuxiliaryFixture {
    helper::Generator composite_,pilot_;
    std::uint64_t epoch_;
    double auxiliary_hz_,amplitude_;
    helper::Options options_;
    std::optional<Crypto> key_;
    bool paired_;
    mutable std::optional<std::uint64_t> legacy_ticks_;
    mutable helper::MetadataPacket legacy_packet_;
public:
    AuxiliaryFixture(helper::Options options,double auxiliary_hz,double amplitude,double data_gain,
                     std::uint64_t epoch,bool punctuate=true,std::optional<Crypto> key={},bool paired=false)
        :composite_(options,{auxiliary_hz+helper::auxiliary_pilot_spacing_hz,amplitude,data_gain,
                            options.power_w,auxiliary_hz},epoch,punctuate,key),
         pilot_(options,{auxiliary_hz+helper::auxiliary_pilot_spacing_hz,amplitude,data_gain,
                        options.power_w},epoch,false,key),epoch_(epoch),auxiliary_hz_(auxiliary_hz),amplitude_(amplitude),
         options_(options),key_(std::move(key)),paired_(paired) {}

    std::complex<double> analytic(long double seconds,std::uint32_t rate) const {
        if(!paired_)return (composite_.mix_analytic({},seconds,rate)-pilot_.mix_analytic({},seconds,rate))/
            helper::auxiliary_amplitude_fraction;
        // Preserve the original paired-auxiliary fixture independently of
        // the production Generator's 160ms single-reversal waveform.
        auto local=std::fmod(static_cast<long double>(epoch_%2)+seconds,2.L);
        if(local<0)local+=2;
        local-=std::floor(local/.4L)*.4L;
        const auto smooth=[](long double position) {
            const auto u=static_cast<double>(std::clamp(position,0.L,1.L));
            return u*u*u*(10+u*(-15+6*u));
        };
        const auto shaped=[](long double position) {
            const auto u=static_cast<double>(std::clamp(position,0.L,1.L));
            return u*u*u*u*(35+u*(-84+u*(70-20*u)));
        };
        auto envelope=smooth(std::min(local,.4L-local)/.008L)*
            (1-2*shaped((local-.236L)/.008L))*(1-2*smooth((local-.276L)/.008L));
        std::complex<double> modulation{1,0};
        if((options_.timecode||!options_.callsign.empty())&&local>=.316L) {
            const auto dwell=static_cast<long double>(epoch_)+seconds-local;
            const auto ticks=static_cast<std::uint64_t>(std::round(dwell*65536));
            if(legacy_ticks_!=ticks) {
                legacy_packet_=helper::metadata_packet(ticks,options_.callsign,key_?&*key_:nullptr);
                legacy_ticks_=ticks;
            }
            const auto position=(local-.316L)/.008L;
            const auto index=static_cast<std::size_t>(std::floor(position));
            if(index<legacy_packet_.count) {
                const auto previous=index?helper::apsk_point(legacy_packet_.symbols[index-1]):std::complex<double>{1,0};
                modulation=previous+(helper::apsk_point(legacy_packet_.symbols[index])-previous)*shaped(position-index);
            } else if(index==legacy_packet_.count) {
                const auto last=helper::apsk_point(legacy_packet_.symbols[index-1]);
                modulation=last+(std::complex<double>{1,0}-last)*shaped(position-index);
            }
        }
        const auto cycles=std::fmod(static_cast<long double>(auxiliary_hz_)*seconds,1.L);
        const auto value=amplitude_*envelope*modulation*
            std::polar(1.,2*std::numbers::pi*static_cast<double>(cycles));
        return value;
    }
    float mix(long double seconds,std::uint32_t rate) const {
        return static_cast<float>(analytic(seconds,rate).real());
    }
};

} // namespace datapump::test
