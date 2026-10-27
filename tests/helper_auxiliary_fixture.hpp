#pragma once

#include "datapump/helper_carrier.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <optional>

namespace datapump::test {

// Isolate the actual Generator's auxiliary component, then restore the
// historical component amplitude so existing noise/coverage assertions retain
// their original SNR. This is component coverage, not composite-pilot isolation.
class AuxiliaryFixture {
    helper::Generator composite_,pilot_;
    std::uint64_t epoch_;
    bool paired_;
public:
    AuxiliaryFixture(helper::Options options,double auxiliary_hz,double amplitude,double data_gain,
                     std::uint64_t epoch,bool punctuate=true,std::optional<Crypto> key={},bool paired=false)
        :composite_(options,{auxiliary_hz+helper::auxiliary_pilot_spacing_hz,amplitude,data_gain,
                            options.power_w,auxiliary_hz},epoch,punctuate,key),
         pilot_(options,{auxiliary_hz+helper::auxiliary_pilot_spacing_hz,amplitude,data_gain,
                        options.power_w},epoch,false,key),epoch_(epoch),paired_(paired) {}

    std::complex<double> analytic(long double seconds,std::uint32_t rate) const {
        auto value=(composite_.mix_analytic({},seconds,rate)-pilot_.mix_analytic({},seconds,rate))/
            helper::auxiliary_amplitude_fraction;
        if(paired_) {
            // Explicit paired-auxiliary clock compatibility fixture only. The
            // optional shaped restoration never modulates the pure pilot and
            // makes no claim about production two-tone clock qualification.
            auto local=std::fmod(static_cast<long double>(epoch_%2)+seconds,2.L);
            if(local<0)local+=2;
            local-=std::floor(local/.4L)*.4L;
            const auto u=static_cast<double>(std::clamp((local-.276L)/.008L,0.L,1.L));
            value*=1-2*u*u*u*(10+u*(-15+6*u));
        }
        return value;
    }
    float mix(long double seconds,std::uint32_t rate) const {
        return static_cast<float>(analytic(seconds,rate).real());
    }
};

} // namespace datapump::test
