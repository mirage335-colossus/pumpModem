#include "datapump/helper_assistance.hpp"
#include "datapump/sched_hop.hpp"
#include "datapump/symbol_schedule.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace datapump::helper {
namespace {
std::vector<double> original_rates(const modem::Config& c,const modem::PatternSearch& s) {
    std::vector<double> result;
    if(!s.hypotheses.empty()) {
        for(const auto& h:s.hypotheses)result.push_back(h.clock_error_ppm);
    } else if(c.oscillator_search && s.frequency_offsets_hz.empty() &&
              s.clock_errors_ppm==std::vector<double>{0} && !s.couple_clock_to_carrier) {
        for(const auto& h:modem::oscillator_pattern_search(c).hypotheses)result.push_back(h.clock_error_ppm);
    } else {
        auto frequencies=s.frequency_offsets_hz;
        auto coupled=s.couple_clock_to_carrier;
        auto uncoupled=s.uncoupled_frequency_count;
        if(frequencies.empty() && s.expand_clock_search) {
            frequencies=modem::default_pattern_frequency_offsets(c);
            coupled=frequencies.size()>5 && c.spreading_mode==modem::SpreadingMode::pattern &&
                s.clock_errors_ppm==std::vector<double>{0};
            if(coupled) {
                uncoupled=frequencies.size();
                const auto original=frequencies;
                frequencies.insert(frequencies.end(),original.begin(),original.end());
            }
        }
        if(frequencies.empty())frequencies={0};
        for(auto ppm:s.clock_errors_ppm)for(std::size_t i=0;i<frequencies.size();++i) {
            const auto coupling=coupled && i>=uncoupled?1+frequencies[i]/c.carrier_hz:1.;
            result.push_back(coupling==1?ppm:(coupling*(1+ppm*1e-6)-1)*1e6);
        }
    }
    std::sort(result.begin(),result.end());
    result.erase(std::unique(result.begin(),result.end()),result.end());
    return result;
}
}
bool prefer_compact_assistance(const modem::Config& c,const modem::PatternSearch& search) {
    if(!c.sample_rate || !std::isfinite(c.carrier_hz) || c.carrier_hz<=0 || search.hypotheses.empty())return false;
    const auto limit=modem::pattern_frequency_offset_limit(c);
    for(const auto& h:search.hypotheses)
        if(!std::isfinite(h.frequency_offset_hz) || std::abs(h.frequency_offset_hz)>limit ||
           !std::isfinite(h.clock_error_ppm) || std::abs(h.clock_error_ppm)>10000)return false;
    // Preserve an explicitly selected existing backend. A long FFT fill is
    // not evidence that enumerating every private phase in a compact
    // correlator costs less CPU; automatic switching can stall payload work.
    return search.compact_clock_search;
}
AssistancePlan plan_assistance(const modem::Config& c,const modem::PatternSearch& original,
                               const Reference& reference,const AssistanceRequest& request) {
    AssistancePlan result;
    const auto reject=[&](const char* message){result.reason=message;return result;};
    const auto frequency_sample=reference.frequency_sample?reference.frequency_sample:reference.sample;
    if(!reference.frequency_valid || !c.sample_rate || reference.sample_rate!=c.sample_rate ||
       !std::isfinite(reference.frequency_hz) || !std::isfinite(reference.frequency_uncertainty_hz) ||
       reference.frequency_uncertainty_hz<0 || !std::isfinite(request.expected_helper_hz) ||
       request.expected_helper_hz<=0 || request.current_sample<reference.sample || frequency_sample>reference.sample ||
       !std::isfinite(request.maximum_age_seconds) || request.maximum_age_seconds<=0 ||
       static_cast<long double>(request.current_sample-frequency_sample)/c.sample_rate>request.maximum_age_seconds)
        return reject("helper frequency reference is unavailable, stale or on another input clock");
    if(!request.reference_sequence || !request.maximum_hypotheses ||
       request.maximum_hypotheses>modem::maximum_pattern_frequency_rate_hypotheses ||
       request.maximum_timing_alternatives>64 ||
       !std::isfinite(request.residual_clock_uncertainty_ppm) || request.residual_clock_uncertainty_ppm<0 ||
       request.residual_clock_uncertainty_ppm>10000 || !std::isfinite(c.carrier_hz) || c.carrier_hz<=0)
        return reject("helper assistance bounds are invalid");
    result.helper_offset_hz=reference.frequency_hz-request.expected_helper_hz;
    result.frequency_uncertainty_hz=reference.frequency_uncertainty_hz+
        std::abs(c.carrier_hz-request.expected_helper_hz)*request.residual_clock_uncertainty_ppm*1e-6;
    const auto symbol_samples=modem::symbol_sample_count(c);
    if(!symbol_samples)return reject("helper assistance has no finite observation aperture");
    // Nondeferred illustrative FHSS only gates the unchanged physical symbol;
    // it does not pause its source clock. Split/deferred calendars need their
    // existing physical-aperture resolver and remain outside this planner.
    if((c.scheduled_hopping && c.scheduled_hopping->enabled() && !c.scheduled_hopping->continuous()) ||
       c.symbol_burst.enabled() || (c.fake_hopping && c.fake_hopping->defer_symbols))
        return reject("helper assistance for split physical symbols requires a resolved aperture");
    const auto step=.25*c.sample_rate/static_cast<double>(symbol_samples);
    const auto intervals=std::ceil(result.frequency_uncertainty_hz/step);
    if(!std::isfinite(intervals) || intervals>request.maximum_hypotheses)
        return reject("helper residual uncertainty exceeds the optional lane limit");
    const auto side=static_cast<std::size_t>(intervals);
    const auto rates=original_rates(c,original);
    const auto per_rate=1+2*side;
    if(rates.empty() || rates.size()>request.maximum_hypotheses/per_rate)
        return reject("all original clock alternatives cannot fit the optional helper bank");
    auto search=original;
    search.hypotheses.clear();search.frequency_offsets_hz.clear();
    search.hypotheses.reserve(rates.size()*per_rate);
    const auto limit=modem::pattern_frequency_offset_limit(c);
    for(const auto ppm:rates) {
        if(!std::isfinite(ppm) || std::abs(ppm)>10000)
            return reject("original clock alternatives are invalid");
        // ΔfH=δLO+epsilon*fH; ΔfD=δLO+epsilon*fD. A tone
        // observation alone must not remove independent audio-clock lanes.
        const auto center=result.helper_offset_hz+ppm*1e-6*(c.carrier_hz-request.expected_helper_hz);
        if(!std::isfinite(center) || std::abs(center)+result.frequency_uncertainty_hz>limit)
            return reject("helper frequency alternatives exceed the original PCM passband");
        search.hypotheses.push_back({center,ppm});
        for(std::size_t i=1;i<=side;++i) {
            const auto offset=i==side?result.frequency_uncertainty_hz:i*step;
            search.hypotheses.push_back({center-offset,ppm});
            search.hypotheses.push_back({center+offset,ppm});
        }
    }
    const auto sequence=static_cast<long double>(request.reference_sequence);
    const auto alpha=static_cast<long double>(original.false_alarm_probability)*.001L/(sequence*(sequence+1));
    if(!std::isfinite(alpha) || alpha<=std::numeric_limits<double>::min() || alpha>=1)
        return reject("helper optional-search significance allowance is exhausted");
    search.false_alarm_probability=static_cast<double>(alpha);
    search.couple_clock_to_carrier=false;search.uncoupled_frequency_count=0;
    search.clock_errors_ppm={0};search.expand_clock_search=false;search.allow_local_clock_fallback=false;
    // Original coverage certificates describe the original bank, not this
    // measured, conditional strip. They must not survive by accident.
    search.complex_frequency_coverage.reset();search.complex_clock_coverage.reset();
    result.phase_reference_available=reference.timing_valid && reference.phase_valid && std::isfinite(reference.phase_radians) &&
        std::isfinite(reference.phase_uncertainty_radians) && reference.phase_uncertainty_radians>=0;
    if(request.data_start_after_dwell_seconds && reference.timing_valid &&
       std::isfinite(*request.data_start_after_dwell_seconds) &&
       std::isfinite(reference.dwell_start_seconds) && std::isfinite(reference.dwell_uncertainty_seconds) &&
       reference.dwell_uncertainty_seconds>=0 && reference.dwell_uncertainty_seconds<.2 &&
       original.start_offset_seconds && std::isfinite(*original.start_offset_seconds) &&
       std::isfinite(original.start_uncertainty_seconds) && original.start_uncertainty_seconds>=0) {
        const auto center=static_cast<long double>(*original.start_offset_seconds);
        const auto width=static_cast<long double>(original.start_uncertainty_seconds);
        const auto radius=static_cast<long double>(reference.dwell_uncertainty_seconds);
        const auto dwell=static_cast<long double>(reference.dwell_start_seconds)-
            static_cast<long double>(request.receiver_input_origin_samples)/c.sample_rate;
        const auto offset=static_cast<long double>(*request.data_start_after_dwell_seconds);
        const auto minimum_rate=1+(static_cast<long double>(rates.front())-request.residual_clock_uncertainty_ppm)*1e-6L;
        const auto maximum_rate=1+(static_cast<long double>(rates.back())+request.residual_clock_uncertainty_ppm)*1e-6L;
        // The 400 ms dwell is a TRANSMIT-clock interval. Preserve all original
        // sampling-clock alternatives when locating its aliases at reception.
        const auto lo=center-width-radius-dwell,hi=center+width+radius-dwell;
        const auto first=std::ceil((std::min(lo*minimum_rate,lo*maximum_rate)-offset)/.4L);
        const auto last=std::floor((std::max(hi*minimum_rate,hi*maximum_rate)-offset)/.4L);
        if(std::isfinite(first) && std::isfinite(last) && last>=first &&
           last-first<request.maximum_timing_alternatives) {
            const auto count=static_cast<std::size_t>(last-first)+1;
            result.timing_alternatives.reserve(count);
            for(std::size_t i=0;i<count;++i) {
                const auto source=offset+(first+i)*.4L;
                const auto a=dwell+source/minimum_rate,b=dwell+source/maximum_rate;
                const auto low=std::max(center-width,std::min(a,b)-radius);
                const auto high=std::min(center+width,std::max(a,b)+radius);
                if(low>high)continue;
                result.timing_alternatives.push_back({(low+high)/2*c.sample_rate,1,(high-low)/2*c.sample_rate});
            }
            if(result.timing_alternatives.size()==1) {
                const auto& window=result.timing_alternatives.front();
                search.start_offset_seconds=static_cast<double>(window.epoch_origin_samples/c.sample_rate);
                search.start_uncertainty_seconds=static_cast<double>(window.half_width_samples/c.sample_rate);
                // Do not claim a private canonical phase/UTC map merely from
                // a measured data-symbol-zero relation.
                search.qualified_start_window.reset();
            }
        }
    }
    if(request.qualify_epoch_timing && reference.timing_valid &&
       std::isfinite(reference.dwell_start_seconds) && std::isfinite(reference.dwell_uncertainty_seconds) &&
       reference.dwell_uncertainty_seconds>=0 && reference.dwell_uncertainty_seconds<.2 &&
       original.start_offset_seconds && std::isfinite(*original.start_offset_seconds) &&
       std::isfinite(original.start_uncertainty_seconds) && original.start_uncertainty_seconds>=0) {
        auto low_ppm=rates.front()-request.residual_clock_uncertainty_ppm;
        auto high_ppm=rates.back()+request.residual_clock_uncertainty_ppm;
        if(original.admitted_clock_half_width_ppm && std::isfinite(*original.admitted_clock_half_width_ppm) &&
           *original.admitted_clock_half_width_ppm>=0) {
            low_ppm=std::min(low_ppm,-*original.admitted_clock_half_width_ppm);
            high_ppm=std::max(high_ppm,*original.admitted_clock_half_width_ppm);
        }
        if(low_ppm>=-10000 && high_ppm<=10000) {
            const auto rlo=1+static_cast<long double>(low_ppm)*1e-6L;
            const auto rhi=1+static_cast<long double>(high_ppm)*1e-6L;
            const auto ulo=1/rhi,uhi=1/rlo,umid=(ulo+uhi)/2,du=(uhi-ulo)/2;
            const auto phase_upper=original.search_stream_phases?modem::canonical_phase_extent(c)-1:c.stream_phase_samples;
            const auto phase_lower=original.search_stream_phases?0:c.stream_phase_samples;
            const auto center=original.qualified_start_window?original.qualified_start_window->epoch_origin_samples/c.sample_rate:
                static_cast<long double>(*original.start_offset_seconds);
            const auto width=original.qualified_start_window?
                (original.qualified_start_window->half_width_samples+
                 std::abs(original.qualified_start_window->phase_scale-1)*phase_upper)/c.sample_rate:
                static_cast<long double>(original.start_uncertainty_seconds);
            const auto dwell=static_cast<long double>(reference.dwell_start_seconds)-
                static_cast<long double>(request.receiver_input_origin_samples)/c.sample_rate;
            const auto radius=static_cast<long double>(reference.dwell_uncertainty_seconds);
            const auto parity=.2L*(c.stream_epoch%2);
            const auto lo=center-width-radius-dwell,hi=center+width+radius-dwell;
            const auto first=std::ceil((std::min(lo*rlo,lo*rhi)-parity)/.4L);
            const auto last=std::floor((std::max(hi*rlo,hi*rhi)-parity)/.4L);
            if(std::isfinite(first) && std::isfinite(last) && last>=first && last-first<request.maximum_timing_alternatives) {
                result.timing_alternatives.clear();
                for(std::size_t i=0;i<static_cast<std::size_t>(last-first)+1;++i) {
                    const auto a=parity+(first+i)*.4L;
                    const auto maximum=std::max(std::abs(a+static_cast<long double>(phase_lower)/c.sample_rate),
                        std::abs(a+static_cast<long double>(phase_upper)/c.sample_rate));
                    result.timing_alternatives.push_back({c.sample_rate*(dwell+a*umid),umid,
                        c.sample_rate*(radius+du*maximum)+2});
                }
                // A single optional receiver chooses the nearest alias. Every
                // other alias and the full original epoch/phase/rate search
                // remain the ordinary receiver's responsibility.
                const auto selected=std::min_element(result.timing_alternatives.begin(),result.timing_alternatives.end(),
                    [&](const auto& a,const auto& b){return std::abs(a.epoch_origin_samples/c.sample_rate-center)<
                        std::abs(b.epoch_origin_samples/c.sample_rate-center);});
                search.qualified_start_window=*selected;
            }
        }
    }
    result.search=std::move(search);result.reason="optional helper frequency/clock search; original search remains required";
    return result;
}
} // namespace datapump::helper
