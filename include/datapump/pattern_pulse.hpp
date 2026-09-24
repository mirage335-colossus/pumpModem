#pragma once

#include "datapump/pattern_code.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace datapump::modem {

inline constexpr double pattern_pulse_rolloff = .25;
inline constexpr unsigned pattern_pulse_half_span = 8;
inline constexpr double pattern_pcm_radius_limit = .992;

inline bool pattern_pulse_enabled(const Config& config) {
    return config.pulse_shaping && config.spreading_mode == SpreadingMode::pattern &&
           symbol_sample_count(config) / pattern_chip_samples(config) >= 2 * pattern_pulse_half_span;
}
inline std::uint64_t pattern_pulse_padding_samples(const Config& config) {
    return pattern_pulse_enabled(config) ? pattern_pulse_half_span * pattern_chip_samples(config) : 0;
}

// A single shared bounded table avoids transcendental work in the clock bank.
// Normalize the truncated continuous pulse to unit energy in chip-time units.
inline double pattern_pulse(double chip_offset) {
    constexpr std::size_t resolution = 256;
    constexpr std::size_t count = pattern_pulse_half_span * resolution;
    static const auto table = [] {
        std::array<double, count + 1> values{};
        constexpr auto a = pattern_pulse_rolloff;
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

// Sum one finite chip sequence. A final partial chip retains its old energy
// and stream position rather than borrowing a chip from the next symbol.
template<class ChipValue>
std::complex<double> pattern_pulse_sum(double sample, std::uint64_t samples,
                                       std::uint64_t chip_samples, ChipValue value) {
    if(!chip_samples || !std::isfinite(sample))throw Error("invalid pulse sample coordinate or chip duration");
    const auto length=static_cast<double>(chip_samples);
    const auto support=pattern_pulse_half_span*length;
    if(!samples || sample < -support || sample > static_cast<double>(samples)+support)return {};
    const auto chips=samples/chip_samples+(samples%chip_samples!=0);
    const auto begin=std::max(0.,std::floor((sample-support)/length)-1);
    const auto end=std::min(static_cast<double>(chips),std::floor((sample+support)/length)+1);
    if(static_cast<long double>(begin)>=static_cast<long double>(chips))return {};
    std::complex<double> result{};
    // The explicit integer bound also protects a rounded double end coordinate
    // when an exceptionally long sequence exceeds exact double integer range.
    for(auto local=static_cast<std::uint64_t>(begin);local<chips &&
        static_cast<double>(local)<end;++local) {
        const auto first=local*chip_samples;
        const auto duration=std::min(chip_samples,samples-first);
        const auto center=static_cast<double>(first)+.5*static_cast<double>(duration);
        const auto coefficient=pattern_pulse((sample-center)/length)*
            std::sqrt(static_cast<double>(duration)/length);
        if(coefficient!=0)result+=coefficient*value(local);
    }
    return result;
}

// Overlapping pulses can exceed the original bounded-chip PCM headroom.
// Radial limiting preserves circular symmetry and requires no fixed backoff.
// Templates remain linear; the small limiter residual is receiver mismatch.
inline std::complex<double> pattern_limit_pcm(std::complex<double> value) {
    const auto power=std::norm(value);
    if(power>pattern_pcm_radius_limit*pattern_pcm_radius_limit)
        value*=pattern_pcm_radius_limit/std::sqrt(power);
    return value;
}

} // namespace datapump::modem
