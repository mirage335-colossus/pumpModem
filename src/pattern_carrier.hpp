#pragma once

#include <cmath>
#include <cstdint>

namespace datapump::modem::detail {

// Config validation supplies a positive rate <= 120 MHz and a finite carrier
// within Nyquist, so sample * whole_hz fits uint64_t. Compute modulo one cycle
// before converting a long sample cursor into an angle. In particular, this
// must not depend on long double being wider than double (it is not on MSVC).
inline double pattern_carrier_cycles(std::uint64_t cursor,std::uint32_t rate,double carrier) {
    auto seconds=cursor/rate;
    const auto sample=cursor%rate;
    const auto whole_hz=static_cast<std::uint64_t>(carrier);
    auto fraction=carrier-static_cast<double>(whole_hz);
    auto cycles=static_cast<double>((sample*whole_hz)%rate)/rate+
        static_cast<double>(sample)/rate*fraction;
    // Whole seconds times whole Hz vanish modulo one. Multiply the fractional
    // Hz by the integer seconds using bounded modular doubling, at most 64
    // steps; integer-Hz carriers skip this loop entirely.
    while(seconds&&fraction!=0) {
        if(seconds&1U)cycles=std::remainder(cycles+fraction,1.);
        fraction=std::remainder(fraction*2,1.);
        seconds>>=1;
    }
    return std::remainder(cycles,1.);
}

} // namespace datapump::modem::detail
