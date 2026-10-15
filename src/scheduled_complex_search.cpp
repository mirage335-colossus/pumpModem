#include "datapump/scheduled_complex_search.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/pattern_pulse.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <set>

namespace datapump::modem {
namespace {
using Pair=PatternFrequencyRateHypothesis;
bool covers(double represented,double requested) {
    return represented>=requested || requested-represented<=
        32*std::numeric_limits<double>::epsilon()*std::max(represented,requested);
}
std::vector<Pair> explicit_pairs(const Config& config,const PatternSearch& search) {
    if(!search.hypotheses.empty())return search.hypotheses;
    if(search.clock_errors_ppm.empty() || search.clock_errors_ppm.size()>maximum_pattern_rate_hypotheses)
        throw Error("invalid scheduled complex explicit clock bank");
    for(const auto ppm:search.clock_errors_ppm)if(!std::isfinite(ppm) || std::abs(ppm)>10000)
        throw Error("invalid scheduled complex explicit clock alternative");
    auto frequencies=search.frequency_offsets_hz;
    auto coupled=search.couple_clock_to_carrier;auto prefix=search.uncoupled_frequency_count;
    if(frequencies.empty()) {
        if(config.oscillator_search && search.clock_errors_ppm==std::vector<double>{0} && !coupled)return {};
        if(search.expand_clock_search)frequencies=default_pattern_frequency_offsets(config);
        else {
            const auto step=.25*config.sample_rate/static_cast<double>(symbol_sample_count(config));
            frequencies={0,-step,step,-2*step,2*step};
            const auto limit=pattern_frequency_offset_limit(config);
            std::erase_if(frequencies,[&](double f){return std::abs(f)>limit;});
        }
        coupled=frequencies.size()>5 && config.spreading_mode==SpreadingMode::pattern &&
            search.clock_errors_ppm==std::vector<double>{0};
        if(coupled) {
            prefix=frequencies.size();const auto original=frequencies;
            frequencies.insert(frequencies.end(),original.begin(),original.end());
        } else prefix=0;
    }
    if(prefix>frequencies.size())
        throw Error("invalid scheduled complex uncoupled prefix");
    std::vector<Pair> pairs;
    if(search.clock_errors_ppm.size()>maximum_pattern_frequency_rate_hypotheses/
       frequencies.size())throw Error("original scheduled complex bank exceeds finite hypothesis limits");
    pairs.reserve(search.clock_errors_ppm.size()*frequencies.size());
    for(const auto rate:search.clock_errors_ppm)for(std::size_t f=0;f<frequencies.size();++f) {
        const auto frequency=frequencies[f];
        const auto ratio=coupled && f>=prefix?
            1+frequency/config.carrier_hz:1.;
        // Same flattening as the original FFT receiver, including the product
        // term between explicit clock error and carrier-coupled clock error.
        const auto ppm=ratio==1?rate:(ratio*(1+rate*1e-6)-1)*1e6;
        if(!std::isfinite(frequency) || !std::isfinite(ppm) || std::abs(ppm)>10000+1e-8)
            throw Error("invalid scheduled complex explicit frequency/rate alternative");
        pairs.push_back({frequency,std::clamp(ppm,-10000.,10000.)});
    }
    return pairs;
}
}

PatternScheduledComplexSearch scheduled_complex_search(const Config& config,const PatternSearch& input,
                                                       double correction) {
    if(!std::isfinite(correction) || correction<0 || correction>.001)
        throw Error("invalid scheduled complex timing correction allowance");
    PatternScheduledComplexSearch out;out.search=input;
    out.geometry=pattern_scheduled_complex_geometry(config,input);
    out.applicable=out.geometry.supported;
    out.original_hypotheses=input.hypotheses.empty()?
        input.frequency_offsets_hz.size()*input.clock_errors_ppm.size():input.hypotheses.size();
    out.refined_hypotheses=out.original_hypotheses;
    if(!out.applicable){out.reason=out.geometry.reason;return out;}
    const auto explicit_zero=input.admitted_frequency_half_width_hz && input.admitted_clock_half_width_ppm &&
        *input.admitted_frequency_half_width_hz==0 && *input.admitted_clock_half_width_ppm==0;
    const auto declared=input.admitted_frequency_half_width_hz.has_value() || input.admitted_clock_half_width_ppm.has_value();
    if((explicit_zero && !config.oscillator_search) || (!declared && !config.oscillator_search)) {
        out.reason="explicit diagnostic bank retained; no continuous frequency/clock domain declared";
        return out;
    }
    // A geometry certificate describes its declared domain, not the UTC
    // allowance of a later caller. Reusing a finalized bank must revalidate
    // that policy as well. Keep this coarse plan for refinement if necessary.
    std::optional<OscillatorPatternSearch> coarse;
    if(config.oscillator_search)coarse=oscillator_pattern_search(config,correction);
    const auto same_domain=!coarse || (!coarse->limited && !coarse->timing_correction_unavailable &&
        covers(out.geometry.admitted_frequency_half_width_hz,coarse->frequency.requested_half_width_hz) &&
        covers(coarse->frequency.requested_half_width_hz,out.geometry.admitted_frequency_half_width_hz) &&
        covers(out.geometry.admitted_clock_half_width_ppm,coarse->requested_clock_half_width_ppm) &&
        covers(coarse->requested_clock_half_width_ppm,out.geometry.admitted_clock_half_width_ppm));
    if(same_domain && !input.hypotheses.empty() && out.geometry.frequency_grid_qualified &&
       out.geometry.clock_grid_qualified && input.complex_frequency_coverage.value_or(false) &&
       input.complex_clock_coverage.value_or(false)) {
        // Live/strategy/wrapper pass this already prepared immutable bank through
        // several layers. Revalidate geometry/provenance, not its construction.
        out.complete_coverage=true;
        out.reason="supplied physical-aperture grid retained; complete joint provenance";
        return out;
    }
    const auto fail=[&](const char* reason) {
        out.limited=true;out.reason=reason;
        out.search.complex_frequency_coverage=false;out.search.complex_clock_coverage=false;
        return out;
    };
    std::vector<Pair> original=explicit_pairs(config,input),added;
    double frequency_width=out.geometry.admitted_frequency_half_width_hz;
    double clock_width=out.geometry.admitted_clock_half_width_ppm;
    if(config.oscillator_search) {
        if(original.empty())original=coarse->hypotheses;
        // Steering changes the slowest admitted physical clock and therefore
        // the aperture used for resolution. Derive that geometry BEFORE the
        // fine bank; a one-step-old aperture can miss a ceil boundary even when
        // its relative change is tiny. Keep all supplied exact original pairs.
        auto resolution=input;
        resolution.admitted_frequency_half_width_hz=std::max(frequency_width,coarse->frequency.requested_half_width_hz);
        resolution.admitted_clock_half_width_ppm=std::max(clock_width,coarse->requested_clock_half_width_ppm);
        out.geometry=pattern_scheduled_complex_geometry(config,resolution);
        if(!out.geometry.supported)return fail("corrected scheduled aperture geometry is unsupported");
        const auto plan=oscillator_pattern_search(config,correction,
            {out.geometry.physical_aperture_samples,out.geometry.clock_step_ppm});
        // An exhausted refined grid may return the unsteered fallback bank.
        // Report that limit before comparing its narrower fallback domain with
        // the caller's legitimate UTC-expanded domain. Neither is admissible.
        if(plan.limited || plan.timing_correction_unavailable)
            return fail("physical-aperture oscillator grid exceeds finite hypothesis/passband limits");
        if(!covers(plan.frequency.requested_half_width_hz,frequency_width) ||
           !covers(plan.requested_clock_half_width_ppm,clock_width))
            return fail("scheduled complex declared domain exceeds the supplied oscillator/UTC policy");
        frequency_width=std::max(frequency_width,plan.frequency.requested_half_width_hz);
        clock_width=std::max(clock_width,plan.requested_clock_half_width_ppm);
        added=plan.hypotheses;
    } else {
        if(original.empty())return fail("declared scheduled complex domain needs an explicit finite reference bank");
        if(!std::isfinite(frequency_width) || frequency_width<0 || !std::isfinite(clock_width) || clock_width<0)
            throw Error("invalid scheduled complex continuous search domain");
        const auto half_band=pattern_pulse_enabled(config)?
            (1+pattern_pulse_rolloff)*config.sample_rate/(2.L*pattern_chip_samples(config)):
            waveform_bandwidth_hz(config)/2.L;
        const auto support=half_band*(1+clock_width*1e-6L);
        const auto nyquist=config.sample_rate/2.;
        const auto tolerance=4*(std::nextafter(nyquist,std::numeric_limits<double>::infinity())-nyquist);
        if(clock_width>10000 || config.carrier_hz-frequency_width-support<-tolerance ||
           config.carrier_hz+frequency_width+support>nyquist+tolerance)
            return fail("declared scheduled complex rectangle exceeds joint sampled passband limits");
        // Without oscillator topology, a declared rectangle is deliberately
        // conservative. Compute its size before creating any alternatives.
        const auto fs=frequency_width?std::ceil(static_cast<long double>(frequency_width)/out.geometry.frequency_step_hz):0.L;
        const auto rs=clock_width?std::ceil(static_cast<long double>(clock_width)/out.geometry.clock_step_ppm):0.L;
        if(fs>(maximum_pattern_frequency_hypotheses-1)/2 ||
           rs>(maximum_pattern_rate_hypotheses-1)/2 ||
           (2*fs+1)*(2*rs+1)>maximum_pattern_frequency_rate_hypotheses)
            return fail("physical-aperture rectangular grid exceeds finite hypothesis limits");
        const auto fsteps=static_cast<std::size_t>(fs),rsteps=static_cast<std::size_t>(rs);
        const auto frequencies=2*fsteps+1,rates=2*rsteps+1;
        added.reserve(frequencies*rates);
        const auto coordinate=[](std::size_t i,std::size_t steps,double width) {
            if(i==0)return 0.;
            const auto magnitude=static_cast<double>((i+1)/2)*width/static_cast<double>(steps);
            return i%2?-magnitude:magnitude;
        };
        for(std::size_t r=0;r<rates;++r)for(std::size_t f=0;f<frequencies;++f)
            added.push_back({coordinate(f,fsteps,frequency_width),coordinate(r,rsteps,clock_width)});
    }
    out.original_hypotheses=original.size();
    if(original.size()>maximum_pattern_frequency_rate_hypotheses)
        return fail("original scheduled complex bank exceeds finite hypothesis limits");
    std::set<std::pair<double,double>> pairs;
    std::set<double> frequencies;
    for(const auto lane:original) {
        if(!std::isfinite(lane.frequency_offset_hz) || !std::isfinite(lane.clock_error_ppm))
            throw Error("scheduled complex bank contains nonfinite coordinates");
        pairs.emplace(lane.frequency_offset_hz,lane.clock_error_ppm);
        frequencies.insert(lane.frequency_offset_hz);
    }
    auto merged=original;
    for(const auto lane:added) {
        if(!pairs.emplace(lane.frequency_offset_hz,lane.clock_error_ppm).second)continue;
        frequencies.insert(lane.frequency_offset_hz);
        if(merged.size()==maximum_pattern_frequency_rate_hypotheses ||
           frequencies.size()>maximum_pattern_frequency_hypotheses)
            return fail("original plus refined scheduled complex alternatives exceed finite bank limits");
        merged.push_back(lane);
    }
    out.refined_hypotheses=merged.size();out.refined=merged!=original;
    out.search.hypotheses=std::move(merged);
    out.search.frequency_offsets_hz.clear();out.search.clock_errors_ppm={0};
    out.search.couple_clock_to_carrier=false;out.search.expand_clock_search=false;
    out.search.allow_local_clock_fallback=false;
    out.search.admitted_frequency_half_width_hz=frequency_width;
    out.search.admitted_clock_half_width_ppm=clock_width;
    // These certify the coupled builder (or deliberate complete rectangle),
    // rather than inferring joint coverage from marginal coordinate gaps.
    out.search.complex_frequency_coverage=true;out.search.complex_clock_coverage=true;
    out.geometry=pattern_scheduled_complex_geometry(config,out.search);
    out.complete_coverage=out.geometry.supported && out.geometry.frequency_grid_qualified && out.geometry.clock_grid_qualified;
    if(!out.complete_coverage)return fail("refined scheduled complex grid fails physical-aperture coverage check");
    out.reason=config.oscillator_search?"physical-aperture coupled oscillator grid; original exact pairs retained":
        "physical-aperture rectangular overcoverage; original exact pairs retained";
    return out;
}
} // namespace datapump::modem
