#include "datapump/pattern_search.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace datapump::modem {
namespace {
double offset_limit(const Config& config) {
    if(config.spreading_mode==SpreadingMode::tone)return config.bandwidth_hz/8;
    const auto half_band=pattern_pulse_enabled(config)?
        (1+pattern_pulse_rolloff)*config.sample_rate/(2.*static_cast<double>(pattern_chip_samples(config))):
        config.bandwidth_hz/2;
    return std::max(0.,std::min(config.carrier_hz-half_band,
        config.sample_rate/2.-config.carrier_hz-half_band));
}
}

double pattern_frequency_offset_limit(const Config& config) {
    validate(config);
    return offset_limit(config);
}

PatternFrequencySearch default_pattern_frequency_search(const Config& config) {
    validate(config);
    PatternFrequencySearch result;
    const auto samples=symbol_sample_count(config);
    result.step_hz=.25*config.sample_rate/static_cast<double>(samples);
    result.requested_half_width_hz=2*result.step_hz;
    std::size_t steps=2;
    if(config.spreading_mode==SpreadingMode::pattern && samples>=16ULL*config.sample_rate) {
        result.requested_half_width_hz=std::max(result.requested_half_width_hz,
            config.carrier_hz*default_clock_uncertainty_ppm*1e-6);
        constexpr auto maximum_steps=(maximum_pattern_frequency_hypotheses-1)/2;
        // Clamp before converting to an integer: very long symbols can need
        // many more hypotheses than fit an integer, even though the duration
        // itself fits the modem's sample coordinates.
        const auto requested_steps=std::ceil(result.requested_half_width_hz/result.step_hz);
        steps=requested_steps>=static_cast<double>(maximum_steps)?maximum_steps:
            static_cast<std::size_t>(requested_steps);
    }
    if(config.spreading_mode==SpreadingMode::pattern) {
        // Valid shaped carriers may touch DC or Nyquist at any symbol rate.
        // Retain every feasible default pair and always keep the center;
        // short profiles must not reject the whole receiver at these edges.
        const auto headroom=offset_limit(config);
        const auto available_steps=std::floor(headroom/result.step_hz);
        if(available_steps<static_cast<double>(steps))steps=static_cast<std::size_t>(available_steps);
        // Floating division may round up at a passband endpoint. Every emitted
        // frequency must still satisfy the same direct comparison as a caller.
        while(steps && static_cast<double>(steps)*result.step_hz>headroom)--steps;
    }
    result.count=1+2*steps;
    result.half_width_hz=static_cast<double>(steps)*result.step_hz;
    result.limited=result.half_width_hz<result.requested_half_width_hz;
    return result;
}

std::vector<double> default_pattern_frequency_offsets(const Config& config) {
    const auto geometry=default_pattern_frequency_search(config);
    std::vector<double> result;
    result.reserve(geometry.count);
    result.push_back(0);
    for(std::size_t step=1;step<=geometry.count/2;++step) {
        const auto offset=static_cast<double>(step)*geometry.step_hz;
        result.push_back(-offset);
        result.push_back(offset);
    }
    return result;
}

std::uint64_t pattern_projection_bin_samples(const Config& config,double maximum_offset_hz) {
    validate(config);
    if(!std::isfinite(maximum_offset_hz) || maximum_offset_hz<0 || maximum_offset_hz>config.sample_rate/4.)
        throw Error("projection carrier offset must be finite and within 0..sample_rate/4");
    const auto chip=pattern_chip_samples(config);
    const auto original=std::gcd(std::gcd(chip,symbol_sample_count(config)),std::max<std::uint64_t>(1,chip/2));
    if(maximum_offset_hz==0)return original;
    const auto limit=.25*config.sample_rate/maximum_offset_hz;
    if(static_cast<double>(original)<=limit)return original;
    // Here limit is smaller than a validated chip count, so converting its
    // floor is bounded even when the supplied offset is extremely small.
    const auto maximum=static_cast<std::uint64_t>(std::floor(limit));
    std::uint64_t selected=1;
    for(std::uint64_t divisor=1;divisor<=original/divisor;++divisor) {
        if(original%divisor)continue;
        if(divisor<=maximum)selected=std::max(selected,divisor);
        const auto paired=original/divisor;
        if(paired<=maximum)selected=std::max(selected,paired);
    }
    return selected;
}

} // namespace datapump::modem
