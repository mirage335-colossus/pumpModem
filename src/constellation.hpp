#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>
#include <span>
#include <cstdint>

namespace datapump::modem::detail {
inline constexpr double tau=2*std::numbers::pi;
inline constexpr std::array<unsigned,8> phase_steps{0,1,3,2,7,6,4,5};
inline unsigned phase_bits(unsigned bits) {return std::min(bits-1,3U);}
inline unsigned rings(unsigned bits) {return 1U<<(bits-phase_bits(bits));}
inline double radius_step(unsigned bits) {
    const double count=rings(bits);
    // Identical mean power for every equiprobable constellation, including
    // the original two-ring 16APSK training (.35 and .70).
    return std::sqrt(.30625/((count+1)*(2*count+1)/6));
}
inline unsigned gray_decode(unsigned gray) {unsigned result=gray;while(gray>>=1)result^=gray;return result;}
inline double radius(unsigned value,unsigned bits) {
    return radius_step(bits)*(1+gray_decode(value>>phase_bits(bits)));
}
inline unsigned phase_step(unsigned value,unsigned bits) {
    const auto width=phase_bits(bits),mask=(1U<<width)-1;
    // Keep the existing four-bit training mapping exactly unchanged.
    return width==3?phase_steps[value&mask]:gray_decode(value&mask);
}
inline std::complex<double> mapped(unsigned value,unsigned bits,std::complex<double> previous) {
    const auto phases=1U<<phase_bits(bits);
    const auto prior=std::abs(previous)>1e-20?previous/std::abs(previous):std::complex<double>{1,0};
    return prior*std::polar(radius(value,bits),tau*phase_step(value,bits)/phases);
}
inline unsigned decision(std::complex<double> point,std::complex<double> previous,double gain,unsigned bits) {
    const auto delta=point*std::conj(previous);
    const double x=delta.real(),y=delta.imag(),ax=std::abs(x),ay=std::abs(y);
    const auto width=phase_bits(bits);
    unsigned phase=0;
    if(width==1)phase=x<0?1U:0U;
    else if(width==2) {
        const auto step=ax>ay?(x<0?2U:0U):(y<0?3U:1U);
        phase=step^(step>>1);
    } else {
        constexpr double tangent=0.4142135623730950488;
        unsigned step=0;
        if(ay<tangent*ax)step=x<0?4U:0U;
        else if(ax<tangent*ay)step=y<0?6U:2U;
        else step=x<0?(y<0?5U:3U):(y<0?7U:1U);
        constexpr std::array<unsigned,8> inverse{0,1,3,2,6,7,5,4};
        phase=inverse[step];
    }
    const auto levels=rings(bits);
    unsigned ring=0;
    if(levels==2) {const auto threshold=.525*gain;ring=std::norm(point)>threshold*threshold?1U:0U;}
    else {
        const auto scaled=std::abs(point)/(radius_step(bits)*gain);
        ring=static_cast<unsigned>(std::clamp(scaled+.5,1.,static_cast<double>(levels)))-1;
    }
    return phase|((ring^(ring>>1))<<width);
}
inline unsigned nibble(std::complex<double> point,std::complex<double> previous,double gain) {return decision(point,previous,gain,4);}
inline unsigned read_bits(std::span<const std::uint8_t> bytes,std::size_t bit,unsigned count) {
    unsigned value=0;
    for(unsigned i=0;i<count;++i,++bit) {
        value<<=1;
        if(bit/8<bytes.size())value|=(bytes[bit/8]>>(7-bit%8))&1U;
    }
    return value;
}
}
