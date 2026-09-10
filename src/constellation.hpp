#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>

namespace datapump::modem::detail {
inline constexpr double tau=2*std::numbers::pi;
inline constexpr std::array<unsigned,8> phase_steps{0,1,3,2,7,6,4,5};
inline unsigned nibble(std::complex<double> point,std::complex<double> previous,double gain) {
    const auto delta=point*std::conj(previous);
    const double x=delta.real(),y=delta.imag(),ax=std::abs(x),ay=std::abs(y);
    constexpr double tangent=0.4142135623730950488; // tan(pi/8)
    unsigned step=0;
    if(ay<tangent*ax)step=x<0?4U:0U;
    else if(ax<tangent*ay)step=y<0?6U:2U;
    else step=x<0?(y<0?5U:3U):(y<0?7U:1U);
    constexpr std::array<unsigned,8> inverse{0,1,3,2,6,7,5,4};
    const double threshold=.525*gain;
    return inverse[step]|(std::norm(point)>threshold*threshold?8U:0U);
}
}
