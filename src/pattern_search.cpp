#include "datapump/pattern_search.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace datapump::modem {
namespace {
double occupied_half_band(const Config& config) {
    return pattern_pulse_enabled(config)?
        (1+pattern_pulse_rolloff)*config.sample_rate/(2.*static_cast<double>(pattern_chip_samples(config))):
        config.bandwidth_hz/2;
}
double offset_limit(const Config& config) {
    if(config.spreading_mode==SpreadingMode::tone) {
        const auto historical=config.bandwidth_hz/8;
        if(!config.oscillator_search)return historical;
        const auto quantized=config.sample_rate/(4.*static_cast<double>(pattern_chip_samples(config)));
        return std::min(historical,std::nextafter(quantized,0.));
    }
    const auto half_band=occupied_half_band(config);
    return std::max(0.,std::min(config.carrier_hz-half_band,
        config.sample_rate/2.-config.carrier_hz-half_band));
}
double sideband_sign(const OscillatorSearchConfig& policy) {
    return policy.sideband==OscillatorSideband::upper?1.:-1.;
}
double physical_carrier(const Config& config,const OscillatorSearchConfig& policy) {
    if(policy.rf_shift_hz==0)return config.carrier_hz;
    const auto value=static_cast<long double>(policy.rf_shift_hz)+sideband_sign(policy)*config.carrier_hz;
    if(!std::isfinite(value) || value<=0 || value>std::numeric_limits<double>::max())
        throw Error("RF LO and sideband must describe a finite positive on-air carrier");
    return static_cast<double>(value);
}
double bounded_value(long double value,const char* message) {
    if(!std::isfinite(value) || value<0 || value>std::numeric_limits<double>::max())throw Error(message);
    return static_cast<double>(value);
}
double lattice_endpoint(double step,std::size_t steps,double bound,bool complete) {
    return complete?bound:std::min(static_cast<double>(steps)*step,bound);
}
std::vector<double> lattice(double step,std::size_t steps,double bound=std::numeric_limits<double>::infinity(),
                            bool complete=false) {
    std::vector<double> values;values.reserve(1+2*steps);values.push_back(0);
    for(std::size_t i=1;i<=steps;++i) {
        const auto offset=lattice_endpoint(step,i,bound,complete && i==steps);
        values.push_back(-offset);
        values.push_back(offset);
    }
    return values;
}
std::size_t fitting_steps(double width,double step,std::size_t cap) {
    const auto possible=std::floor(static_cast<long double>(width)/step);
    auto count=possible>=static_cast<long double>(cap)?cap:static_cast<std::size_t>(possible);
    while(count && static_cast<double>(count)*step>width)--count;
    return count;
}
bool incomplete(double covered,double requested) {
    return covered<requested && requested-covered>16*std::numeric_limits<double>::epsilon()*requested;
}
}

void validate_oscillator_search(const OscillatorSearchConfig& policy) {
    const auto valid_model=[](const OscillatorModel& model) {
        return std::isfinite(model.accuracy_ppm) && model.accuracy_ppm>=0 && model.accuracy_ppm<=10000 &&
            std::isfinite(model.phase_noise_degrees_per_sqrt_second) &&
            model.phase_noise_degrees_per_sqrt_second>=0 && model.phase_noise_degrees_per_sqrt_second<=180;
    };
    if(!valid_model(policy.lf) || !valid_model(policy.rf))
        throw Error("oscillator accuracy must be 0..10000 ppm and phase diffusion 0..180 degrees/sqrt(s)");
    if(!std::isfinite(policy.rf_shift_hz) || policy.rf_shift_hz<0)
        throw Error("RF frequency shift must be finite and nonnegative");
    if(!std::isfinite(policy.margin) || policy.margin<1)
        throw Error("oscillator search margin must be finite and at least 1x");
    if(policy.reference!=OscillatorReference::independent_audio && policy.reference!=OscillatorReference::shared_radio)
        throw Error("unknown oscillator reference topology");
    if(policy.sideband!=OscillatorSideband::upper && policy.sideband!=OscillatorSideband::lower)
        throw Error("unknown RF sideband");
    bounded_value(static_cast<long double>(policy.margin)*policy.lf.accuracy_ppm,
        "LF oscillator margin exceeds numeric range");
    bounded_value(static_cast<long double>(policy.margin)*policy.rf.accuracy_ppm,
        "RF oscillator margin exceeds numeric range");
}

OscillatorEffects oscillator_effects(const Config& config) {
    validate(config);
    if(!config.oscillator_search)throw Error("oscillator effects require an oscillator search policy");
    const auto& policy=*config.oscillator_search;
    validate_oscillator_search(policy);
    OscillatorEffects result;result.physical_rf_hz=physical_carrier(config,policy);
    const bool shared=policy.reference==OscillatorReference::shared_radio;
    const bool rf_active=shared || policy.rf_shift_hz!=0;
    result.clock_error_ppm=shared?policy.rf.accuracy_ppm:policy.lf.accuracy_ppm;
    result.frequency_offset_hz=sideband_sign(policy)*bounded_value(static_cast<long double>(policy.rf_shift_hz)*
        policy.rf.accuracy_ppm*1e-6L,"RF oscillator offset exceeds numeric range");
    result.phase_noise_degrees_per_sqrt_second=shared?policy.rf.phase_noise_degrees_per_sqrt_second:
        rf_active?std::hypot(policy.lf.phase_noise_degrees_per_sqrt_second,
            policy.rf.phase_noise_degrees_per_sqrt_second):policy.lf.phase_noise_degrees_per_sqrt_second;
    if(result.phase_noise_degrees_per_sqrt_second>180)
        throw Error("combined oscillator phase diffusion exceeds simulation range");
    return result;
}

OscillatorPatternSearch oscillator_pattern_search(const Config& config) {
    validate(config);
    if(!config.oscillator_search)throw Error("oscillator pattern search requires an oscillator search policy");
    const auto& policy=*config.oscillator_search;
    validate_oscillator_search(policy);
    const auto physical=physical_carrier(config,policy);
    const bool shared=policy.reference==OscillatorReference::shared_radio;
    const auto clock_bound=bounded_value(static_cast<long double>(policy.margin)*
        (shared?policy.rf.accuracy_ppm:policy.lf.accuracy_ppm),"oscillator clock allowance exceeds numeric range");
    const auto rf_bound=shared?0.:bounded_value(static_cast<long double>(policy.margin)*
        policy.rf_shift_hz*policy.rf.accuracy_ppm*1e-6L,"RF search allowance exceeds numeric range");
    // The sign is in delivered real-tone coordinates; lower-sideband mixing
    // reverses the RF clock contribution while preserving the symbol rate.
    const auto coupling=shared && policy.rf_shift_hz!=0?sideband_sign(policy)*physical:config.carrier_hz;
    const auto frequency_bound=bounded_value(std::abs(static_cast<long double>(coupling))*clock_bound*1e-6L+rf_bound,
        "oscillator frequency allowance exceeds numeric range");
    OscillatorPatternSearch result;
    auto& frequency=result.frequency;
    const auto maximum_frequency_step=.25*config.sample_rate/static_cast<double>(symbol_sample_count(config));
    const auto requested_frequency_steps=frequency_bound==0?0.L:
        std::ceil(static_cast<long double>(frequency_bound)/maximum_frequency_step);
    // Refine the lattice to reach the declared bound. Rounding a tiny GPSDO
    // region up to the usual phase bin would invent a much larger clock error,
    // particularly for short symbols and low PCM carriers.
    frequency.step_hz=frequency_bound==0?maximum_frequency_step:
        static_cast<double>(static_cast<long double>(frequency_bound)/requested_frequency_steps);
    frequency.requested_half_width_hz=frequency_bound;
    result.requested_clock_half_width_ppm=clock_bound;
    const auto half_band=occupied_half_band(config);
    // Match the ordinary double-precision configuration boundary. A carrier
    // formed as Nyquist-half_band can differ from its long-double sum by an
    // ulp; that cannot invalidate the already validated nominal lane.
    const auto nyquist=config.sample_rate/2.;
    const auto passband_tolerance=4*(std::nextafter(nyquist,std::numeric_limits<double>::infinity())-nyquist);
    const auto valid_pair=[&](double offset,double ppm) {
        const auto support=static_cast<long double>(half_band)*(1+static_cast<long double>(ppm)*1e-6L);
        const auto carrier=static_cast<long double>(config.carrier_hz)+offset;
        return carrier-support>=-passband_tolerance && carrier+support<=nyquist+passband_tolerance;
    };
    // The declared domain must fit jointly: a positive sample-clock error
    // expands the waveform as well as shifting its center. Scalar carrier
    // headroom alone cannot establish complete oscillator coverage.
    const auto epsilon=static_cast<long double>(clock_bound)*1e-6L;
    const auto nominal_lower=static_cast<long double>(config.carrier_hz)-half_band;
    const auto nominal_upper=static_cast<long double>(config.carrier_hz)+half_band;
    const auto required_lower=nominal_lower-
        (shared?std::abs(static_cast<long double>(coupling)-half_band):config.carrier_hz-half_band)*epsilon-rf_bound;
    const auto required_upper=nominal_upper+
        (shared?std::abs(static_cast<long double>(coupling)+half_band):config.carrier_hz+half_band)*epsilon+rf_bound;
    const bool joint_limited=required_lower<-passband_tolerance || required_upper>nyquist+passband_tolerance;
    const auto headroom=offset_limit(config);
    constexpr auto maximum_steps=(maximum_pattern_frequency_hypotheses-1)/2;
    const auto requested_steps=requested_frequency_steps>=maximum_steps?maximum_steps:
        static_cast<std::size_t>(requested_frequency_steps);
    auto steps=frequency_bound<=headroom?requested_steps:
        std::min(requested_steps,fitting_steps(headroom,frequency.step_hz,maximum_steps));
    // Preserve exact declared endpoints when the complete refined lattice fits.
    // Multiplying the rounded step back up can otherwise lose one ulp of clock
    // coverage. A capped or passband-trimmed lattice never receives that endpoint.
    const auto endpoint=[&](std::size_t count) {
        return lattice_endpoint(frequency.step_hz,count,frequency_bound,count==requested_frequency_steps);
    };
    const auto offsets_for=[&](std::size_t count) {
        return lattice(frequency.step_hz,count,frequency_bound,count==requested_frequency_steps);
    };
    const auto correlated_rate=[&](double offset) {
        if(clock_bound==0 || offset==0)return 0.;
        const auto magnitude=std::abs(offset)==frequency_bound?clock_bound:
            std::min(std::abs(offset/coupling*1e6),clock_bound);
        return std::copysign(magnitude,offset/coupling);
    };
    const bool correlated=shared || rf_bound==0 || clock_bound==0;
    if(correlated) {
        if(clock_bound>10000)steps=std::min(steps,fitting_steps(std::abs(coupling)*.01,frequency.step_hz,maximum_steps));
        while(steps) {
            const auto offset=endpoint(steps);
            const auto ppm=correlated_rate(offset);
            if(valid_pair(offset,ppm) && valid_pair(-offset,-ppm))break;
            --steps;
        }
        const auto offsets=offsets_for(steps);
        result.hypotheses.reserve(offsets.size());
        for(auto offset:offsets) {
            const auto ppm=correlated_rate(offset);
            result.hypotheses.push_back({offset,ppm});
            result.clock_half_width_ppm=std::max(result.clock_half_width_ppm,std::abs(ppm));
        }
        result.clock_step_ppm=clock_bound==0?0.:frequency.step_hz/std::abs(coupling)*1e6;
    } else {
        // A quarter-chip accumulated timing error is the independent clock
        // grid's resolution. The carrier grid independently resolves phase.
        const auto maximum_clock_step=.25*static_cast<double>(pattern_chip_samples(config))/
            static_cast<double>(symbol_sample_count(config))*1e6;
        const auto requested_rate_steps=std::ceil(static_cast<long double>(clock_bound)/maximum_clock_step);
        result.clock_step_ppm=static_cast<double>(static_cast<long double>(clock_bound)/requested_rate_steps);
        constexpr auto maximum_rate_steps=(maximum_pattern_rate_hypotheses-1)/2;
        const auto requested_rate_count=requested_rate_steps>=maximum_rate_steps?maximum_rate_steps:
            static_cast<std::size_t>(requested_rate_steps);
        const auto rate_steps=clock_bound<=10000?requested_rate_count:
            std::min(requested_rate_count,fitting_steps(10000,result.clock_step_ppm,maximum_rate_steps));
        const auto rates=lattice(result.clock_step_ppm,rate_steps,clock_bound,rate_steps==requested_rate_steps);
        // Trim impossible corners of the independent-reference rectangle.
        // Include half a rate cell and one frequency cell for conservative
        // outward rounding; neither allowance is a new physical uncertainty.
        const auto frequency_radius=rf_bound+config.carrier_hz*result.clock_step_ppm*.5e-6+frequency.step_hz;
        const auto pair_count=[&](std::size_t candidate_steps) {
            std::size_t count=0;
            for(auto rate:rates)for(auto offset:offsets_for(candidate_steps))
                if(std::abs(offset-config.carrier_hz*rate*1e-6)<=frequency_radius && valid_pair(offset,rate))++count;
            return count;
        };
        if(pair_count(steps)>maximum_pattern_frequency_rate_hypotheses) {
            std::size_t lower=0,upper=steps;
            while(lower<upper) {
                const auto middle=lower+(upper-lower+1)/2;
                if(pair_count(middle)<=maximum_pattern_frequency_rate_hypotheses)lower=middle;
                else upper=middle-1;
            }
            steps=lower;
        }
        const auto offsets=offsets_for(steps);
        result.hypotheses.reserve(pair_count(steps));
        for(auto rate:rates)for(auto offset:offsets)
            if(std::abs(offset-config.carrier_hz*rate*1e-6)<=frequency_radius && valid_pair(offset,rate)) {
                result.hypotheses.push_back({offset,rate});
                result.clock_half_width_ppm=std::max(result.clock_half_width_ppm,std::abs(rate));
            }
        // Keep the reported frequency lattice contiguous and symmetric even
        // when only one joint passband edge loses all timing alternatives.
        std::vector<bool> positive(steps+1),negative(steps+1);
        for(auto pair:result.hypotheses) {
            const auto index=std::min(steps,static_cast<std::size_t>(std::llround(std::abs(pair.frequency_offset_hz)/frequency.step_hz)));
            (pair.frequency_offset_hz<0?negative:positive)[index]=true;
        }
        for(std::size_t i=1;i<=steps;++i)
            if(!negative[i] || !positive[i]) {steps=i-1;break;}
        const auto retained=endpoint(steps);
        std::erase_if(result.hypotheses,[&](auto pair){return std::abs(pair.frequency_offset_hz)>retained;});
        result.clock_half_width_ppm=0;
        for(auto pair:result.hypotheses)
            result.clock_half_width_ppm=std::max(result.clock_half_width_ppm,std::abs(pair.clock_error_ppm));
    }
    frequency.count=1+2*steps;
    frequency.half_width_hz=endpoint(steps);
    frequency.limited=incomplete(frequency.half_width_hz,frequency.requested_half_width_hz);
    result.limited=frequency.limited || incomplete(result.clock_half_width_ppm,clock_bound) || joint_limited;
    frequency.limited=result.limited;
    if(result.hypotheses.empty()) {
        // Empty has historical automatic-bank meaning to low-level callers.
        // A bounded declared policy must never silently fall through to that
        // broader policy, even at a numerically delicate passband endpoint.
        result.hypotheses.push_back({0,0});
        frequency.count=1;frequency.half_width_hz=0;
        result.clock_half_width_ppm=0;
        frequency.limited=result.limited=frequency_bound!=0 || clock_bound!=0;
    }
    return result;
}

double pattern_frequency_offset_limit(const Config& config) {
    validate(config);
    return offset_limit(config);
}

PatternFrequencySearch default_pattern_frequency_search(const Config& config) {
    validate(config);
    if(config.oscillator_search)return oscillator_pattern_search(config).frequency;
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
    if(config.oscillator_search) {
        const auto geometry=oscillator_pattern_search(config).frequency;
        return lattice(geometry.step_hz,geometry.count/2,geometry.half_width_hz,true);
    }
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
