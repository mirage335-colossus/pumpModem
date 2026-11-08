#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace datapump::triad::v3 {
inline constexpr double rrc_rolloff=.25;
inline constexpr unsigned rrc_half_span=8;
// Unit-energy, finite-support root-raised-cosine pulse shared by the source
// and its public templates. The bounded interpolation table is initialized once.
inline double rrc_pulse(double chip_offset) {
    constexpr std::size_t resolution = 256;
    constexpr std::size_t count = rrc_half_span * resolution;
    static const auto table = [] {
        std::array<double, count + 1> values{};
        constexpr auto a = rrc_rolloff;
        constexpr auto pi = std::numbers::pi;
        for(std::size_t i=0;i<=count;++i) {
            const auto t=static_cast<double>(i)/resolution;
            if(i==0) values[i]=1+a*(4/pi-1);
            else if(t==1/(4*a)) values[i]=a/std::sqrt(2.)*
                ((1+2/pi)*std::sin(pi/(4*a))+(1-2/pi)*std::cos(pi/(4*a)));
            else values[i]=(std::sin(pi*t*(1-a))+4*a*t*std::cos(pi*t*(1+a)))/
                (pi*t*(1-16*a*a*t*t));
        }
        double energy=values.front()*values.front()+values.back()*values.back();
        for(std::size_t i=1;i<count;++i)energy+=2*values[i]*values[i];
        const auto scale=std::sqrt(resolution/energy);
        for(auto& value:values)value*=scale;
        return values;
    }();
    const auto position=std::abs(chip_offset)*resolution;
    if(!(position<=count))return 0;
    const auto index=static_cast<std::size_t>(position);
    if(index==count)return table[count];
    return std::lerp(table[index],table[index+1],position-static_cast<double>(index));
}

} // namespace datapump::triad::v3
