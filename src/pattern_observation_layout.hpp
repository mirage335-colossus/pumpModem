#pragma once
// EXACT constructor geometry shared by runtime and metadata-only observation
// planning. No FFT plans/templates/Fits/PCM are allocated by this helper.
#include "datapump/pattern_receiver.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/pattern_pulse.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
namespace datapump::modem::detail {
struct PatternObservationLayout {
    std::size_t bin_samples=0,length=0,transform=0,hop=0;
    bool long_symbol=false;
};
inline std::size_t observation_power_two(std::size_t value) {
    std::size_t result=1;
    while(result<value) {
        if(result>std::numeric_limits<std::size_t>::max()/2)throw Error("pattern transform size overflow");
        result*=2;
    }
    return result;
}
inline std::size_t pattern_observation_bin_samples(const Config& config,double maximum_offset) {
    auto bin_samples=static_cast<std::size_t>(pattern_projection_bin_samples(config,maximum_offset));
    const auto symbol=symbol_sample_count(config);
    const auto omega=(2*std::numbers::pi)*config.carrier_hz/config.sample_rate;
    const auto sine=std::sin(omega);
    const auto image=std::abs(sine)>1e-12?std::abs(std::sin(static_cast<double>(bin_samples)*omega)/sine):static_cast<double>(bin_samples);
    if(symbol<=256 && ((!config.scramble && !config.dsss) || image>1e-10*static_cast<double>(bin_samples)))bin_samples=1;
    return bin_samples;
}
inline PatternObservationLayout pattern_observation_layout(const Config& config,
        std::size_t bin_samples,double minimum_rate) {
    PatternObservationLayout out;out.bin_samples=bin_samples;
    const auto symbol=symbol_sample_count(config);
    out.long_symbol=symbol>=16ULL*config.sample_rate;
    if(symbol/out.bin_samples>std::numeric_limits<std::size_t>::max()-2)throw Error("pattern integration exceeds address space");
    const auto observation_samples=static_cast<long double>(symbol)/minimum_rate;
    if(observation_samples/out.bin_samples>std::numeric_limits<std::size_t>::max()-2)
        throw Error("pattern clock integration exceeds address space");
    out.length=static_cast<std::size_t>(std::ceil(observation_samples/out.bin_samples));
    if(out.length<4)out.length=4;
    if(out.length>std::numeric_limits<std::size_t>::max()/4)throw Error("pattern integration exceeds address space");
    const auto nominal_length=static_cast<std::size_t>(symbol/out.bin_samples+(symbol%out.bin_samples!=0));
    out.transform=observation_power_two(2*std::max<std::size_t>(4,nominal_length));
    while(out.transform<out.length) {
        if(out.transform>std::numeric_limits<std::size_t>::max()/2)throw Error("pattern transform size overflow");
        out.transform*=2;
    }
    if(out.long_symbol) {
        // A full 2N transform can postpone the first decision until well
        // after both a long bit and its absent successor have arrived.
        // Use a smaller overlap-save transform when it still covers a
        // useful range of new starts; avoid near-zero hops at powers of two.
        // Tracking's five timing fits also need four bins of scratch.
        const auto compact=observation_power_two(out.length+4);
        if(compact-out.length+1>=std::max<std::size_t>(4,out.length/4))
        out.transform=std::min(out.transform,compact);
    }
    out.hop=out.transform-out.length+1;
    if(out.long_symbol)out.hop=std::min(out.hop,std::max<std::size_t>(1,nominal_length/2));
    return out;
}
inline bool absence_duration_complete(long double duration,std::uint32_t sample_rate) {
    return duration>=static_cast<long double>(pattern_absence_seconds)*sample_rate;
}
// Exact predicate used by the scheduled correlator; planned gaps are excluded.
inline bool scheduled_failed_duration_complete(std::uint64_t failed,std::uint64_t symbol,
        long double rate,std::uint32_t sample_rate) {
    return absence_duration_complete(static_cast<long double>(failed)*symbol/rate,sample_rate);
}
// Continuous/packed FFT uses repeated addition. Keep this predicate paired with
// its owner state; this helper does not supply or classify any observation.
inline bool fft_failed_duration_complete(long double absent_samples,
        std::uint64_t unconfirmed,std::uint64_t symbol,std::uint32_t sample_rate,bool variable_clock) {
    return variable_clock?absent_samples>=pattern_absence_seconds*sample_rate:
        static_cast<long double>(unconfirmed)*symbol>=
            static_cast<long double>(pattern_absence_seconds)*sample_rate;
}
} // namespace datapump::modem::detail
