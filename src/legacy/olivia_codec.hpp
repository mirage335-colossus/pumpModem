#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace datapump::legacy::detail::olivia {
// Olivia's published 64-symbol, biorthogonal Walsh code. These equations are
// implemented independently; no FLDigi implementation is incorporated here.
inline constexpr std::uint64_t scrambler = 0xE257E6D0291574ECULL;
using Tones = std::array<unsigned char,64>;
inline Tones encode(std::array<unsigned char,2> characters) {
    Tones tones{};
    for (unsigned t=0;t<64;++t) {
        unsigned symbol=0;
        for (unsigned lane=0;lane<2;++lane) {
            const unsigned c=characters[lane];
            const unsigned negative=(std::popcount((c&63U)&(63U^t))&1U)
                ^((c>>6U)&1U)^unsigned((scrambler>>((t+13U*lane)&63U))&1U);
            symbol|=negative<<((lane+t)&1U);
        }
        tones[t]=static_cast<unsigned char>(symbol^(symbol>>1U));
    }
    return tones;
}
struct Decoded {std::array<unsigned char,2> characters{};double confidence=1;};
inline Decoded decode(const std::array<std::array<double,2>,64>& bits) {
    Decoded result;
    for (unsigned lane=0;lane<2;++lane) {
        std::array<double,64> coefficients{};
        double magnitude=0;
        for (unsigned t=0;t<64;++t) {
            const double bit=bits[t][(lane+t)&1U];
            magnitude+=std::abs(bit);
            coefficients[t]=((scrambler>>((t+13U*lane)&63U))&1U)?-bit:bit;
        }
        // Ordinary Sylvester transform; the protocol's character convention is
        // accounted for by the row-parity adjustment below.
        for (unsigned width=1;width<64;width*=2) {
            for (unsigned base=0;base<64;base+=2*width) {
                for (unsigned i=0;i<width;++i) {
                    const double a=coefficients[base+i],b=coefficients[base+i+width];
                    coefficients[base+i]=a+b;
                    coefficients[base+i+width]=a-b;
                }
            }
        }
        unsigned strongest=0;
        for (unsigned i=1;i<64;++i)
            if (std::abs(coefficients[i])>std::abs(coefficients[strongest])) strongest=i;
        const double signed_peak=(std::popcount(strongest)&1)?-coefficients[strongest]:coefficients[strongest];
        result.characters[lane]=static_cast<unsigned char>(strongest+(signed_peak<0?64:0));
        result.confidence=std::min(result.confidence,std::abs(signed_peak)/(magnitude+1e-30));
    }
    return result;
}
}
