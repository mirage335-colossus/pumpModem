#include "datapump/pattern_search.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <set>

namespace datapump::modem {
namespace {
double occupied_half_band(const Config& config) {
    return pattern_pulse_enabled(config)?
        (1+pattern_pulse_rolloff)*config.sample_rate/(2.*static_cast<double>(pattern_chip_samples(config))):
        waveform_bandwidth_hz(config)/2;
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
    // Without a translation stage, only the delivered stream's Baseband
    // oscillator is active, including when legacy shared-reference metadata
    // remains attached to the configuration.
    const bool shared=policy.reference==OscillatorReference::shared_radio && policy.rf_shift_hz!=0;
    const bool rf_active=policy.rf_shift_hz!=0;
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

static OscillatorPatternSearch oscillator_pattern_search_domain(const Config& config,
                                                                PatternSearchResolution resolution={}) {
    validate(config);
    if(!config.oscillator_search)throw Error("oscillator pattern search requires an oscillator search policy");
    const auto& policy=*config.oscillator_search;
    validate_oscillator_search(policy);
    const auto physical=physical_carrier(config,policy);
    const bool shared=policy.reference==OscillatorReference::shared_radio && policy.rf_shift_hz!=0;
    const auto clock_bound=bounded_value(static_cast<long double>(policy.margin)*
        (shared?policy.rf.accuracy_ppm:policy.lf.accuracy_ppm),"oscillator clock allowance exceeds numeric range");
    const auto oscillator_rf_bound=shared?0.:bounded_value(static_cast<long double>(policy.margin)*
        policy.rf_shift_hz*policy.rf.accuracy_ppm*1e-6L,"RF search allowance exceeds numeric range");
    // The sign is in delivered real-tone coordinates; lower-sideband mixing
    // reverses the RF clock contribution while preserving the symbol rate.
    const auto original_coupling=shared && policy.rf_shift_hz!=0?sideband_sign(policy)*physical:config.carrier_hz;
    const auto coupling=resolution.clock_frequency_hz.value_or(original_coupling);
    const auto base_frequency_bound=bounded_value(std::abs(static_cast<long double>(coupling))*clock_bound*1e-6L+oscillator_rf_bound,
        "oscillator frequency allowance exceeds numeric range");
    // Sched_Hop frequency widening adds only residual carrier uncertainty. It
    // does not invent a less accurate sample clock or alter simulated truth.
    const auto extra_frequency_bound=bounded_value(static_cast<long double>(base_frequency_bound)*
        (config.frequency_search_multiplier-1),"expanded carrier allowance exceeds numeric range");
    const auto rf_bound=bounded_value(static_cast<long double>(oscillator_rf_bound)+extra_frequency_bound,
        "expanded residual carrier allowance exceeds numeric range");
    const auto frequency_bound=bounded_value(static_cast<long double>(base_frequency_bound)+extra_frequency_bound,
        "expanded oscillator frequency allowance exceeds numeric range");
    OscillatorPatternSearch result;
    auto& frequency=result.frequency;
    const auto symbol=symbol_sample_count(config);
    const auto aperture=std::max(symbol,resolution.physical_aperture_samples);
    auto maximum_frequency_step=.25*config.sample_rate/static_cast<double>(aperture);
    const auto original_clock_step=.25*static_cast<double>(pattern_chip_samples(config))/
        static_cast<double>(symbol)*1e6;
    const auto maximum_clock_step=resolution.maximum_clock_step_ppm==0?original_clock_step:
        std::min(original_clock_step,resolution.maximum_clock_step_ppm);
    // A coupled frequency lane also represents clock error. Refining phase
    // alone need not resolve a very low carrier's timing domain; use whichever
    // required spacing is finer without adding Cartesian clock alternatives.
    // K=0 removes carrier information about LF; retain the independently
    // resolved timing lanes instead of collapsing an unknown clock to q=0.
    const bool correlated=clock_bound==0 || (coupling!=0 &&
        ((shared && extra_frequency_bound==0) || rf_bound==0));
    if((resolution.physical_aperture_samples || resolution.maximum_clock_step_ppm || resolution.clock_frequency_hz) &&
       correlated && clock_bound>0)
        maximum_frequency_step=std::min(maximum_frequency_step,std::abs(coupling)*maximum_clock_step*1e-6);
    if(!std::isfinite(maximum_frequency_step) || maximum_frequency_step<=0)
        throw Error("oscillator search resolution is below numeric range");
    const auto base_frequency_steps=base_frequency_bound==0?0.L:
        std::ceil(static_cast<long double>(base_frequency_bound)/maximum_frequency_step);
    const auto base_frequency_step=base_frequency_bound==0?maximum_frequency_step:
        static_cast<double>(static_cast<long double>(base_frequency_bound)/base_frequency_steps);
    const auto requested_frequency_steps=config.frequency_search_multiplier==1?base_frequency_steps:
        (frequency_bound==0?0.L:std::ceil(static_cast<long double>(frequency_bound)/base_frequency_step));
    // Refine the lattice to reach the declared bound. Rounding a tiny GPSDO
    // region up to the usual phase bin would invent a much larger clock error,
    // particularly for short symbols and low PCM carriers.
    frequency.step_hz=config.frequency_search_multiplier==1?
        (frequency_bound==0?maximum_frequency_step:
            static_cast<double>(static_cast<long double>(frequency_bound)/requested_frequency_steps)):
        base_frequency_step;
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
        ((shared || resolution.clock_frequency_hz)?std::abs(static_cast<long double>(coupling)-half_band):config.carrier_hz-half_band)*epsilon-rf_bound;
    const auto required_upper=nominal_upper+
        ((shared || resolution.clock_frequency_hz)?std::abs(static_cast<long double>(coupling)+half_band):config.carrier_hz+half_band)*epsilon+rf_bound;
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
        if(clock_bound==0 || coupling==0 || offset==0)return 0.;
        const auto magnitude=std::abs(offset)==frequency_bound?clock_bound:
            std::min(std::abs(offset/coupling*1e6),clock_bound);
        return std::copysign(magnitude,offset/coupling);
    };
    // An independent conversion error describes a strip around the sampling
    // clock relation, not necessarily a separately resolved timing dimension.
    // For f=c*epsilon*1e-6+r, choosing epsilon_i=clamp(f_i/c*1e6,-A,A)
    // at the nearest frequency gives |epsilon-epsilon_i| <= (df/2+B)/c*1e6.
    // Keep one lane per frequency only when that entire strip fits the same
    // quarter-chip timing resolution as the independent rectangular grid.
    auto thin_clock_step=std::numeric_limits<double>::infinity();
    const auto strip_clock_step=coupling==0?std::numeric_limits<long double>::infinity():
        (static_cast<long double>(frequency.step_hz)+2.L*rf_bound)/std::abs(coupling)*1e6L;
    // Both the selected rate and every physical rate lie in [-A,A]. Their
    // distance is at most 2A, so twice the clipped cell radius is at most 4A.
    // A large independent RF error must not bypass the exact test below when
    // the entire sampling-clock domain is tiny (notably microwave GPSDO links).
    // This only broadens eligibility for that test; its outward-rounded result
    // still has to satisfy the original quarter-chip coverage requirement.
    const auto clipped_clock_step_bound=std::min(strip_clock_step,4.L*clock_bound);
    if(coupling!=0 && !correlated && clock_bound<=10000 && clipped_clock_step_bound<=maximum_clock_step) {
        long double radius=0;
        const auto clock_scale=1e6L/coupling;
        const auto frequency_radius=static_cast<long double>(frequency.step_hz)/2+rf_bound;
        for(const auto offset:offsets_for(steps)) {
            const auto rate=correlated_rate(offset);
            const auto a=(static_cast<long double>(offset)-frequency_radius)*clock_scale;
            const auto b=(static_cast<long double>(offset)+frequency_radius)*clock_scale;
            const auto lower=std::max(-static_cast<long double>(clock_bound),std::min(a,b));
            const auto upper=std::min(static_cast<long double>(clock_bound),std::max(a,b));
            radius=std::max({radius,std::abs(lower-rate),std::abs(upper-rate)});
        }
        // Bound the actual rounded public lane coordinates, including the
        // conversion to ppm, rather than assuming exact lattice arithmetic.
        thin_clock_step=std::nextafter(static_cast<double>(2*radius),
            std::numeric_limits<double>::infinity());
    }
    const bool thin_independent=thin_clock_step<=maximum_clock_step;
    if(correlated || thin_independent) {
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
        result.clock_step_ppm=thin_independent?thin_clock_step:
            clock_bound==0?0.:frequency.step_hz/std::abs(coupling)*1e6;
    } else {
        // A quarter-chip accumulated timing error is the independent clock
        // grid's resolution. The carrier grid independently resolves phase.
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
        const auto frequency_radius=rf_bound+std::abs(coupling)*result.clock_step_ppm*.5e-6+frequency.step_hz;
        // At a fixed rate both tests describe one interval in increasing
        // frequency order. Find its exact integer boundaries once, instead
        // of repeatedly scanning the whole rate/frequency rectangle while
        // choosing the pair cap. The direct comparisons preserve floating
        // point endpoint decisions made by the ordinary pair predicate.
        struct RateInterval {double rate;int first,last;};
        std::vector<RateInterval> intervals;intervals.reserve(rates.size());
        const auto outer=static_cast<int>(steps);
        const auto offset_at=[&](int index) {
            const auto magnitude=endpoint(static_cast<std::size_t>(std::abs(index)));
            return index<0?-magnitude:magnitude;
        };
        const auto first_true=[&](const auto& predicate) {
            int lower=-outer,upper=outer+1;
            while(lower<upper) {
                const auto middle=lower+(upper-lower)/2;
                if(predicate(offset_at(middle)))upper=middle;
                else lower=middle+1;
            }
            return lower;
        };
        for(auto rate:rates) {
            const auto center=coupling*rate*1e-6;
            const auto support=static_cast<long double>(half_band)*(1+static_cast<long double>(rate)*1e-6L);
            const auto first=first_true([&](double offset) {
                return offset-center>=-frequency_radius &&
                    static_cast<long double>(config.carrier_hz)+offset-support>=-passband_tolerance;
            });
            const auto last=first_true([&](double offset) {
                return !(offset-center<=frequency_radius &&
                    static_cast<long double>(config.carrier_hz)+offset+support<=nyquist+passband_tolerance);
            })-1;
            intervals.push_back({rate,first,last});
        }
        const auto pair_count=[&](std::size_t candidate_steps) {
            std::size_t count=0;
            const auto bound=static_cast<int>(candidate_steps);
            for(const auto& interval:intervals) {
                const auto first=std::max(interval.first,-bound),last=std::min(interval.last,bound);
                if(first<=last)count+=static_cast<std::size_t>(last-first+1);
            }
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
        result.hypotheses.reserve(pair_count(steps));
        const auto bound=static_cast<int>(steps);
        for(const auto& interval:intervals) {
            const auto first=std::max(interval.first,-bound),last=std::min(interval.last,bound);
            if(first>last)continue;
            const auto emit=[&](int index) {
                result.hypotheses.push_back({offset_at(index),interval.rate});
                result.clock_half_width_ppm=std::max(result.clock_half_width_ppm,std::abs(interval.rate));
            };
            if(first<=0 && last>=0)emit(0);
            // Merge the negative and positive halves in the historical
            // center,-1,+1,-2,+2 order, visiting only retained pairs.
            auto negative=std::max(1,-last),positive=std::max(1,first);
            const auto negative_end=-first,positive_end=last;
            while(negative<=negative_end || positive<=positive_end) {
                if(negative<=negative_end && (positive>positive_end || negative<=positive))emit(-negative++);
                else emit(positive++);
            }
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

namespace {
OscillatorPatternSearch expanded_oscillator_pattern_search(const Config& config,PatternSearchResolution resolution={}) {
    if(config.frequency_search_multiplier==1)return oscillator_pattern_search_domain(config,resolution);
    auto base_config=config;base_config.frequency_search_multiplier=1;
    auto base=oscillator_pattern_search_domain(base_config,resolution);
    auto expanded=oscillator_pattern_search_domain(config,resolution);
    std::set<std::pair<double,double>> pairs;
    std::set<double> frequencies;
    for(const auto lane:base.hypotheses) {
        pairs.emplace(lane.frequency_offset_hz,lane.clock_error_ppm);
        frequencies.insert(lane.frequency_offset_hz);
    }
    auto retained=base.hypotheses;
    for(const auto lane:expanded.hypotheses) {
        if(!pairs.emplace(lane.frequency_offset_hz,lane.clock_error_ppm).second)continue;
        frequencies.insert(lane.frequency_offset_hz);
        if(retained.size()==maximum_pattern_frequency_rate_hypotheses ||
           frequencies.size()>maximum_pattern_frequency_hypotheses) {
            // A wider request must never discard a required original lane to
            // fit the finite bank. Keep its exact original coverage and expose
            // the unsuccessful widening instead of coarsening either grid.
            base.frequency.requested_half_width_hz=expanded.frequency.requested_half_width_hz;
            base.frequency.limited=base.limited=true;
            return base;
        }
        retained.push_back(lane);
    }
    expanded.hypotheses=std::move(retained);
    expanded.frequency.count=frequencies.size();
    expanded.clock_half_width_ppm=std::max(expanded.clock_half_width_ppm,base.clock_half_width_ppm);
    expanded.clock_step_ppm=std::max(expanded.clock_step_ppm,base.clock_step_ppm);
    // Frequency/clock lanes are an explicit union; count is not a promise of a
    // uniformly spaced lattice after exact original endpoints are retained.
    return expanded;
}
}

double utc_transmit_rate_limit(const Config& config) {
    if(!config.oscillator_search)return 0;
    const auto& policy=*config.oscillator_search;
    validate_oscillator_search(policy);
    if(policy.reference==OscillatorReference::shared_radio && policy.rf_shift_hz!=0)return 0;
    return std::min(.001,policy.margin*policy.lf.accuracy_ppm*1e-6);
}

OscillatorPatternSearch oscillator_pattern_search(const Config& config,double correction) {
    return oscillator_pattern_search(config,correction,{});
}

OscillatorPatternSearch oscillator_pattern_search(const Config& config,double correction,
                                                 PatternSearchResolution resolution) {
    return oscillator_pattern_search(config,correction,resolution,PatternUtcClockGrid::preserve_original_lanes);
}

OscillatorPatternSearch oscillator_pattern_search(const Config& config,double correction,
                                                 PatternSearchResolution resolution,PatternUtcClockGrid grid) {
    if(grid!=PatternUtcClockGrid::preserve_original_lanes && grid!=PatternUtcClockGrid::complete_expanded_domain)
        throw Error("invalid UTC clock catalogue policy");
    if(!std::isfinite(correction) || correction<0 || correction>.001)
        throw Error("invalid transmit timing correction search allowance");
    if(!std::isfinite(resolution.maximum_clock_step_ppm) || resolution.maximum_clock_step_ppm<0 ||
       (resolution.clock_frequency_hz && !std::isfinite(*resolution.clock_frequency_hz)))
        throw Error("invalid oscillator search resolution");
    auto original=expanded_oscillator_pattern_search(config,resolution);
    if(correction==0)return original;
    const auto fallback=[&] {
        original.timing_correction_unavailable=true;
        return original;
    };
    const auto& policy=*config.oscillator_search;
    if(original.limited || (policy.reference==OscillatorReference::shared_radio && policy.rf_shift_hz!=0))
        return fallback();
    // Steering one transmitter does not consume an assumed spare portion of
    // the effective-link oscillator bound. Cover (1+a)(1+c) explicitly while
    // keeping the hardware oscillator description unchanged.
    const auto a=static_cast<long double>(policy.margin)*policy.lf.accuracy_ppm*1e-6L;
    const auto expanded=a+correction+a*correction;
    auto adjusted=config;
    const auto ppm=expanded*1e6L/policy.margin;
    if(ppm>10000)return fallback();
    adjusted.oscillator_search->lf.accuracy_ppm=std::nextafter(static_cast<double>(ppm),
        std::numeric_limits<double>::infinity());
    auto result=expanded_oscillator_pattern_search(adjusted,resolution);
    if(result.limited)return fallback();
    if(grid==PatternUtcClockGrid::complete_expanded_domain) {
        // The expanded symmetric bound A+C+A*C contains the entire original
        // A domain. This result already meets the same frequency/quarter-chip
        // cell coverage and passband checks. Retaining almost-identical old
        // numerical representatives adds work, not physical uncertainty.
        result.transmit_rate_correction=correction;return result;
    }
    std::set<std::pair<double,double>> pairs;
    std::set<double> frequencies;
    for(const auto lane:original.hypotheses) {
        pairs.emplace(lane.frequency_offset_hz,lane.clock_error_ppm);
        frequencies.insert(lane.frequency_offset_hz);
    }
    auto retained=original.hypotheses;
    for(const auto lane:result.hypotheses) {
        if(!pairs.emplace(lane.frequency_offset_hz,lane.clock_error_ppm).second)continue;
        frequencies.insert(lane.frequency_offset_hz);
        if(retained.size()==maximum_pattern_frequency_rate_hypotheses ||
           frequencies.size()>maximum_pattern_frequency_hypotheses)return fallback();
        retained.push_back(lane);
    }
    result.hypotheses=std::move(retained);
    result.frequency.count=frequencies.size();
    // This is the complete expanded lattice plus the original lanes, not a
    // claim that the union itself has uniform spacing. Consumers use pairs.
    result.transmit_rate_correction=correction;
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
        const auto search=oscillator_pattern_search(config);
        if(config.frequency_search_multiplier==1)
            return lattice(search.frequency.step_hz,search.frequency.count/2,search.frequency.half_width_hz,true);
        std::set<double> unique;
        for(const auto lane:search.hypotheses)unique.insert(lane.frequency_offset_hz);
        std::vector<double> offsets(unique.begin(),unique.end());
        std::sort(offsets.begin(),offsets.end(),[](double a,double b) {
            return std::abs(a)<std::abs(b) || (std::abs(a)==std::abs(b) && a<b);
        });
        return offsets;
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
