#pragma once

#include "datapump/helper_frontend.hpp"
#include "datapump/helper_carrier.hpp"
#include "datapump/channel.hpp"
#include "datapump/helper_filter.hpp"
#include "datapump/helper_receiver.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_receiver.hpp"
#include "datapump/resampler.hpp"
#include "live_helper.hpp"
#include "helper_reference_search.hpp"
#include "live_helper_frequency_publication.hpp"
#include "live_helper_prior.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <new>
#include <numeric>
#include <optional>
#include <span>
#include <vector>

namespace datapump::live::detail {

// One contiguous real FIR rejects both the pure pilot and the auxiliary
// 400 Hz below it. The +/-150-Hz modulation support has 75-Hz transition
// margins inside the fixed notch. Legacy conditional views keep their kernel.
inline constexpr double realtime_helper_notch_offset_hz=200;
inline constexpr double realtime_helper_notch_half_width_hz=425;
inline constexpr double realtime_helper_group_low_hz=-550,realtime_helper_group_high_hz=150;

// A real capture cannot distinguish the two signs of its analytic image.
// Each admissible branch has its own immutable NCO/input provenance. These
// facts describe a conditional common link, never full ordinary RF coverage.
struct HelperInputView {
    std::uint64_t id=0,budget_serial=0;
    std::uint32_t physical_rate=0,logical_rate=0;
    double helper_hz=0,clock_center=0,clock_lower=0,clock_upper=0;
    double correction_hz=0,translated_helper_hz=0;
    bool reference_passthrough=false;
    // Latched q=0 basis: preserve a tighter ordinary RF prior for this view's
    // whole lifetime. Later precision may select raw cells, never retune PCM.
    bool original_prior_retained=false;
    helper::CaptureBand input_band{},payload_output_band{};
    bool conjugate=false;
};
static_assert(offsetof(HelperInputView,input_band)==
    (offsetof(HelperInputView,reference_passthrough)+sizeof(bool)+alignof(helper::CaptureBand)-1)/
        alignof(helper::CaptureBand)*alignof(helper::CaptureBand));
struct HelperViewBlock {
    std::shared_ptr<const HelperInputView> view;
    std::uint64_t first_sample=0;
    std::vector<float> samples;
    std::optional<helper::Reference> reference;
    // Independent streaming dwell-spacing measurement, in the original
    // source-clock convention. It supplies no epoch or carrier phase.
    struct ClockMeasurement {
        modem::PatternHelperClockInterval interval;
        std::uint64_t first_sample=0,last_sample=0,captured_end_sample=0;
        std::uint32_t sample_rate=0;
        enum class Source : std::uint8_t {punctuation_cadence,shared_radio_frequency,shared_frequency_and_punctuation};
        Source source=Source::punctuation_cadence;
        const char* source_name()const noexcept {
            switch(source) {
                case Source::punctuation_cadence:return "punctuation cadence";
                case Source::shared_radio_frequency:return "shared-radio frequency";
                case Source::shared_frequency_and_punctuation:return "shared-radio frequency intersected with punctuation cadence";
            }
            return "unknown";
        }
        bool valid(std::uint32_t expected_rate)const noexcept {
            return interval.valid() && sample_rate && sample_rate==expected_rate && first_sample<=last_sample &&
                last_sample<captured_end_sample && captured_end_sample-1-last_sample<=
                    static_cast<std::uint64_t>(.55*sample_rate);
        }
    };
    std::optional<ClockMeasurement> clock;
    // Optional frequency-only source identity survives the raw observer ring.
    // Separately owned so metadata pressure can release it before payload PCM.
    std::unique_ptr<const HelperFrequencyPublication> frequency_publication;
    // Exclusive ACTUAL raw ingress frontier. Frequency alone may condition
    // already retained PCM; this never advances a payload/phase/time frontier.
    std::uint64_t raw_captured_end=0;
    std::optional<HelperFrequencyInterval> frequency_interval;
    bool frequency_model_rejected=false;
    // The upstream NCO has applied an independently fitted pilot phase to
    // some PCM in this block. This is executed correction, not a timing prior.
    bool common_phase_corrected=false;
    // Correction-domain RF uncertainty for the actual produced PCM. Raw
    // Reference remains independently dated and unchanged for clock mapping.
    std::optional<double> corrected_rf_uncertainty_hz;
    helper::CaptureBand applied_correction_hz{};
    std::size_t bytes()const noexcept{return samples.capacity()*sizeof(float)+sizeof(*this)+
        (frequency_publication?sizeof(HelperFrequencyPublication):0);}
};
struct HelperInputRecipe {
    std::shared_ptr<const HelperInputView> view;
    helper::Reference reference;
    std::optional<HelperFrequencyInterval> frequency_interval;
    modem::PatternClockInterval clock_domain;
    std::size_t original_pairs=0,selected_pairs=0;
};

namespace shared_helper_detail {
using helper_prior_detail::Interval;
inline Interval plus(Interval a,Interval b)noexcept {
    return {helper_prior_detail::sum(a.lower,b.lower).lower,
        helper_prior_detail::sum(a.upper,b.upper).upper};
}
inline Interval times(Interval a,Interval b)noexcept {
    const std::array products{helper_prior_detail::product(a.lower,b.lower),
        helper_prior_detail::product(a.lower,b.upper),helper_prior_detail::product(a.upper,b.lower),
        helper_prior_detail::product(a.upper,b.upper)};
    auto low=products[0].lower,high=products[0].upper;
    for(const auto& p:products){low=std::min(low,p.lower);high=std::max(high,p.upper);}
    return {low,high};
}
inline Interval coefficient(double shift,double audio)noexcept {
    return helper_prior_detail::sum(shift,audio);
}
inline std::optional<Interval> clock(double shift,double expected,double measured,double uncertainty,int image)noexcept {
    if(!helper_prior_detail::environment() || !std::isfinite(measured) || !std::isfinite(uncertainty) ||
       uncertainty<0 || (image!=1&&image!=-1))return {};
    const auto denominator=coefficient(shift,expected);
    if(denominator.lower<=0&&denominator.upper>=0)return {};
    auto numerator=helper_prior_detail::sum(image*measured,-expected);
    numerator={helper_prior_detail::sum(numerator.lower,-uncertainty).lower,
        helper_prior_detail::sum(numerator.upper,uncertainty).upper};
    const std::array divided{numerator.lower/denominator.lower,numerator.lower/denominator.upper,
        numerator.upper/denominator.lower,numerator.upper/denominator.upper};
    const auto infinity=std::numeric_limits<double>::infinity();
    const Interval result{std::nextafter(*std::min_element(divided.begin(),divided.end()),-infinity),
        std::nextafter(*std::max_element(divided.begin(),divided.end()),infinity)};
    if(!helper_prior_detail::finite(result))return {};
    return result;
}
inline helper::CaptureBand band(double shift,double center,double half,Interval epsilon)noexcept {
    const auto edge=[&](double side) {
        const auto c=helper_prior_detail::sum(center,side*half);
        return plus(c,times(plus(coefficient(shift,center),{side*half,side*half}),epsilon));
    };
    const auto low=edge(-1),high=edge(1);
    return {low.lower,high.upper};
}
}

inline std::array<std::optional<HelperInputView>,2> shared_helper_input_views(
        const modem::Config& config,double expected,const helper::Reference& reference,
        std::uint32_t physical_rate,double physical_passband) {
    std::array<std::optional<HelperInputView>,2> result;
    if(!config.oscillator_search || !reference.frequency_valid || reference.sample_rate!=physical_rate ||
       !std::isfinite(reference.frequency_hz) || !std::isfinite(reference.frequency_uncertainty_hz) ||
       reference.frequency_hz<=0 || reference.frequency_uncertainty_hz<0)return result;
    const auto& policy=*config.oscillator_search;
    if(policy.reference!=modem::OscillatorReference::shared_radio || policy.rf_shift_hz<=0)return result;
    if(modem::utc_transmit_rate_limit(config)!=0 || !helper_prior_detail::environment())return result;
    const auto sign=policy.sideband==modem::OscillatorSideband::upper?1.L:-1.L;
    const auto kh=sign*policy.rf_shift_hz+expected,kd=sign*policy.rf_shift_hz+config.carrier_hz;
    if(!std::isfinite(kh)||!std::isfinite(kd)||kh==0||kd==0)return result;
    const auto maximum=static_cast<long double>(policy.rf.accuracy_ppm)*policy.margin*1e-6L;
    const auto half=helper::occupied_bandwidth(config)/2;
    for(unsigned branch=0;branch<2;++branch) {
        const auto image=branch? -1.L:1.L;
        const auto center=(image*reference.frequency_hz-expected)/kh;
        const auto measured=shared_helper_detail::clock(static_cast<double>(sign*policy.rf_shift_hz),expected,
            reference.frequency_hz,reference.frequency_uncertainty_hz,branch?-1:1);
        if(!measured)continue;
        const auto qualified_low=std::max(-maximum,static_cast<long double>(measured->lower));
        const auto qualified_high=std::min(maximum,static_cast<long double>(measured->upper));
        if(qualified_low>qualified_high || center<-maximum || center>maximum)continue;
        const auto correction=static_cast<double>(-kd*center);
        const auto fixed_helper=static_cast<double>(image*reference.frequency_hz+correction);
        // Choose a STATIC subdomain before freezing the NCO/FIR. Accuracy is
        // not a slew rate and the first measured CI is not a lifetime anchor.
        // Approximate clipping only proposes endpoints: the directed band
        // calculation below and the actual full-band FIR certificate remain
        // authoritative. Endpoints are moved inward, never outward, here.
        long double lower=-maximum,upper=maximum;
        const auto clip=[&](long double a,long double b,long double lo,long double hi) {
            const auto pad=1024*std::numeric_limits<double>::epsilon()*
                std::max({1.L,std::abs(a),std::abs(lo),std::abs(hi)});
            lo+=pad;hi-=pad;
            if(b==0)return a>=lo&&a<=hi;
            auto x=(lo-a)/b,y=(hi-a)/b;if(x>y)std::swap(x,y);
            lower=std::max(lower,x);upper=std::min(upper,y);return lower<=upper;
        };
        const auto physical_lo=branch?-physical_passband:10.L,physical_hi=branch?-10.L:physical_passband;
        bool admissible=true;
        for(const auto side:{-1.L,1.L}) {
            admissible&=clip(config.carrier_hz+side*half,kd+side*half,physical_lo,physical_hi);
            admissible&=clip(expected+side*150,kh+side*150,physical_lo,physical_hi);
            admissible&=clip(config.carrier_hz+side*half+correction,kd+side*half,0,.42L*config.sample_rate);
        }
        // Retain the full 300-Hz helper metadata band in the same fixed notch.
        const auto notch_room=75-150*maximum;
        admissible&=clip(expected+correction,kh,fixed_helper-notch_room,fixed_helper+notch_room);
        if(config.carrier_hz<fixed_helper)
            admissible&=clip(config.carrier_hz+half+correction,kd+half,-physical_passband,fixed_helper-325);
        else admissible&=clip(config.carrier_hz-half+correction,kd-half,fixed_helper+325,physical_passband);
        const auto low=std::nextafter(static_cast<double>(lower),std::numeric_limits<double>::infinity());
        const auto high=std::nextafter(static_cast<double>(upper),-std::numeric_limits<double>::infinity());
        if(!admissible || low>qualified_low || high<qualified_high || low>high)continue;
        const auto shift=static_cast<double>(sign*policy.rf_shift_hz);
        const auto data=shared_helper_detail::band(shift,config.carrier_hz,static_cast<double>(half),{low,high});
        const auto helper=shared_helper_detail::band(shift,expected,150,{low,high});
        const auto signed_low=std::min(data.lower_hz,helper.lower_hz),signed_high=std::max(data.upper_hz,helper.upper_hz);
        const auto input_low=branch?-signed_high:signed_low,input_high=branch?-signed_low:signed_high;
        const auto out_low=helper_prior_detail::sum(data.lower_hz,correction).lower;
        const auto out_high=helper_prior_detail::sum(data.upper_hz,correction).upper;
        // The approximate clip is only a domain proposal. Certify the FULL
        // clock-scaled metadata band using the stored binary64 NCO and an
        // inward enclosure of the immutable filter's +/-225-Hz stopband.
        const auto helper_out_low=helper_prior_detail::sum(helper.lower_hz,correction).lower;
        const auto helper_out_high=helper_prior_detail::sum(helper.upper_hz,correction).upper;
        const auto notch_low=helper_prior_detail::sum(fixed_helper,-225).upper;
        const auto notch_high=helper_prior_detail::sum(fixed_helper,225).lower;
        if(input_low<=10 || input_high>=physical_passband ||
           out_low<=0 || out_high>=.42L*config.sample_rate ||
           helper_out_low<notch_low || helper_out_high>notch_high)continue;
        HelperInputView view;
        view.id=branch+1;view.physical_rate=physical_rate;view.logical_rate=config.sample_rate;
        view.helper_hz=expected;view.clock_center=static_cast<double>(center);
        view.clock_lower=static_cast<double>(low);view.clock_upper=static_cast<double>(high);
        view.correction_hz=correction;
        view.translated_helper_hz=fixed_helper;
        view.input_band={std::nextafter(static_cast<double>(input_low),-std::numeric_limits<double>::infinity()),
            std::nextafter(static_cast<double>(input_high),std::numeric_limits<double>::infinity())};
        view.payload_output_band={std::nextafter(static_cast<double>(out_low),-std::numeric_limits<double>::infinity()),
            std::nextafter(static_cast<double>(out_high),std::numeric_limits<double>::infinity())};
        view.conjugate=branch!=0;result[branch]=view;
    }
    return result;
}

// Explicit nonzero-centered LF catalogue. It is certified on the measured
// shared-clock interval itself; the zero-centered original-cell mask is not
// applicable to this conditional domain. Original baseline limits stay visible.
inline bool shared_helper_search(const modem::Config& config,const HelperInputView& view,
        const helper::Reference& raw_reference,modem::PatternSearch& search,
        const HelperFrequencyInterval* effective=nullptr) {
    if(!config.oscillator_search || !raw_reference.frequency_valid ||
       raw_reference.sample_rate!=view.physical_rate || !std::isfinite(raw_reference.frequency_hz) ||
       !std::isfinite(raw_reference.frequency_uncertainty_hz) || raw_reference.frequency_uncertainty_hz<0)return false;
    const auto& policy=*config.oscillator_search;
    const auto sign=policy.sideband==modem::OscillatorSideband::upper?1.L:-1.L;
    const auto kh=sign*policy.rf_shift_hz+view.helper_hz,kd=sign*policy.rf_shift_hz+config.carrier_hz;
    if(policy.reference!=modem::OscillatorReference::shared_radio || !std::isfinite(kh)||kh==0||!std::isfinite(kd)||kd==0)return false;
    if(modem::utc_transmit_rate_limit(config)!=0)return false;
    const auto frequency=effective?effective->frequency_hz:raw_reference.frequency_hz;
    const auto uncertainty=effective?effective->uncertainty_hz:raw_reference.frequency_uncertainty_hz;
    if(effective && (!effective->phase_mean || effective->first_sample>effective->last_sample ||
        effective->last_sample!=raw_reference.sample))return false;
    const auto measured=shared_helper_detail::clock(static_cast<double>(sign*policy.rf_shift_hz),view.helper_hz,
        frequency,uncertainty,view.conjugate?-1:1);
    if(!measured)return false;
    const auto maximum=static_cast<long double>(policy.rf.accuracy_ppm)*policy.margin*1e-6L;
    const auto lower=std::max(-maximum,static_cast<long double>(measured->lower));
    const auto upper=std::min(maximum,static_cast<long double>(measured->upper));
    if(lower>upper || lower<view.clock_lower || upper>view.clock_upper)return false;
    // New independently measured intervals may move within the frozen physical
    // certificate. Never intersect with the first fit, retune an accepted NCO,
    // or silently discard a part of a current interval outside this view.
    const auto half=helper::occupied_bandwidth(config)/2;
    const auto band_at=[&](long double epsilon,bool upper_edge) {
        return static_cast<long double>(config.carrier_hz)+kd*epsilon+
            (upper_edge?half:-half)*(1+epsilon);
    };
    const auto signed_low=std::min(band_at(lower,false),band_at(upper,false));
    const auto signed_high=std::max(band_at(lower,true),band_at(upper,true));
    const auto helper_a=static_cast<long double>(view.helper_hz)+kh*lower;
    const auto helper_b=static_cast<long double>(view.helper_hz)+kh*upper;
    const auto helper_low=std::min(helper_a,helper_b)-150*(1+upper);
    const auto helper_high=std::max(helper_a,helper_b)+150*(1+upper);
    const auto all_low=std::min(signed_low,helper_low),all_high=std::max(signed_high,helper_high);
    const auto physical_low=view.conjugate?-all_high:all_low;
    const auto physical_high=view.conjugate?-all_low:all_high;
    if(physical_low<view.input_band.lower_hz || physical_high>view.input_band.upper_hz ||
       signed_low+view.correction_hz<view.payload_output_band.lower_hz ||
       signed_high+view.correction_hz>view.payload_output_band.upper_hz)return false;
    const auto symbol=modem::symbol_sample_count(config),chip=modem::pattern_chip_samples(config);
    if(!symbol || !chip)return false;
    const auto frequency_radius=.125L*config.sample_rate/symbol;
    const auto clock_radius=static_cast<long double>(chip)/(8*symbol);
    const auto step=std::min(frequency_radius/std::abs(kd),clock_radius);
    if(!(step>0))return false;
    const auto divisions=std::max(1.L,std::ceil((upper-lower)/step));
    if(divisions+1>modem::maximum_pattern_frequency_rate_hypotheses)return false;
    std::vector<modem::PatternFrequencyRateHypothesis> pairs;
    pairs.reserve(static_cast<std::size_t>(divisions)+1);
    for(std::size_t i=0;i<=static_cast<std::size_t>(divisions);++i) {
        const auto epsilon=i==static_cast<std::size_t>(divisions)?upper:
            lower+(upper-lower)*static_cast<long double>(i)/divisions;
        pairs.push_back({static_cast<double>(kd*epsilon+static_cast<long double>(view.correction_hz)),
            static_cast<double>(epsilon*1e6L)});
    }
    search.hypotheses=std::move(pairs);search.expand_clock_search=false;search.couple_clock_to_carrier=false;
    search.complex_frequency_coverage=true;search.complex_clock_coverage=true;
    search.admitted_frequency_half_width_hz=static_cast<double>(std::max(
        std::abs(kd*lower+static_cast<long double>(view.correction_hz)),
        std::abs(kd*upper+static_cast<long double>(view.correction_hz))));
    search.qualified_clock_interval=modem::PatternClockInterval{
        std::nextafter(static_cast<double>(lower*1e6L),-std::numeric_limits<double>::infinity()),
        std::nextafter(static_cast<double>(upper*1e6L),std::numeric_limits<double>::infinity()),
        static_cast<double>(clock_radius*1e6L)};
    return true;
}

// A q=0 real-PCM view cannot remove a common RF drift by analytic mixing:
// it deliberately preserves the existing raw image convention. Its ONE
// bandstop FIR may nevertheless follow the measured helper in place. The
// original full DATA LF/UTC domain remains authoritative for the passband.
struct RawHelperNotchDomain {
    helper_prior_detail::Interval data_minus_helper{},one_plus_clock{};
    double physical_passband=0;
    bool valid=false;
};
struct RawHelperNotchBands {
    helper::CaptureBand data{},helper{};
    bool valid=false;
};
inline RawHelperNotchDomain raw_helper_notch_domain(const modem::Config& config,
        double expected,double passband,double utc_rate_limit)noexcept {
    using namespace helper_prior_detail;
    RawHelperNotchDomain result;
    try {
    if(!config.oscillator_search || !environment() || !std::isfinite(expected) ||
       !std::isfinite(passband) || passband<=10)return result;
    const auto bound=std::numeric_limits<double>::max();
    const auto clock=modem::helper_reference_data_clock_interval(config,{-bound,bound},utc_rate_limit,
        modem::HelperReferenceClockRelation::common_corrected_source);
    if(!clock)return result;
    const auto fraction=ppm({clock->lower_ppm,clock->upper_ppm});
    result.one_plus_clock={sum(1,fraction.lower).lower,sum(1,fraction.upper).upper};
    const auto delta=sum(config.carrier_hz,-expected);
    const auto half=helper::occupied_bandwidth(config)/2;
    result.data_minus_helper={sum(delta.lower,-half).lower,sum(delta.upper,half).upper};
    result.physical_passband=passband;
    result.valid=finite(result.one_plus_clock) && result.one_plus_clock.lower>0 &&
        finite(result.data_minus_helper) && result.data_minus_helper.lower<=result.data_minus_helper.upper;
    return result;
    }catch(...) {return {};}
}
inline RawHelperNotchBands raw_helper_notch_bands(const RawHelperNotchDomain& domain,
        helper_prior_detail::Interval measured)noexcept {
    using namespace helper_prior_detail;
    using shared_helper_detail::plus;
    using shared_helper_detail::times;
    RawHelperNotchBands result;
    if(!domain.valid || !environment() || !finite(measured) || measured.lower<=0 ||
       measured.lower>measured.upper)return result;
    const auto signed_data=plus(measured,times(domain.data_minus_helper,domain.one_plus_clock));
    // A folded real waveform contains both signs. Certify their COMPLETE
    // absolute-frequency union; there is no claimed inverse analytic image.
    const auto absolute=[](Interval band) {
        return helper::CaptureBand{band.lower<=0 && band.upper>=0?0.:
            std::min(std::abs(band.lower),std::abs(band.upper)),
            std::max(std::abs(band.lower),std::abs(band.upper))};
    };
    result.data=absolute(signed_data);
    const auto metadata=plus(measured,times({realtime_helper_group_low_hz,realtime_helper_group_high_hz},domain.one_plus_clock));
    result.helper={metadata.lower,metadata.upper};
    result.valid=finite(signed_data) && finite(metadata) &&
        std::isfinite(result.data.upper_hz) && result.data.lower_hz>=0 &&
        result.data.upper_hz<domain.physical_passband && result.helper.lower_hz>10 &&
        result.helper.upper_hz<domain.physical_passband;
    return result;
}

class HelperInputFrontend {
    static std::size_t converter_reservation(std::uint32_t input,std::uint32_t output) {
        if(input==output)return sizeof(audio::Resampler);
        if(static_cast<std::uint64_t>(input)>4ULL*output) {
            auto middle=output;while(4ULL*middle<input)middle*=4;
            return sizeof(audio::Resampler)+2048+converter_reservation(input,middle)+converter_reservation(middle,output);
        }
        const auto ratio=std::min(1.,static_cast<double>(output)/input);
        const auto taps=2*static_cast<std::size_t>(std::ceil(48/ratio))+1;
        // Include both retained coefficients/ring and a replacement allocation
        // margin before constructing any optional converter.
        return sizeof(audio::Resampler)+2*(258*taps+2)*sizeof(float);
    }
    struct Branch {
        struct Certificate {
            std::uint64_t first=0,end=0;
            double radius=0,q_low=0,q_high=0;
            bool valid=false,phase=false;
        };
        struct Certificates {
            std::array<Certificate,512> records{};
            std::size_t head=0,count=0;
            void append(Certificate value)noexcept {
                if(value.first>=value.end)return;
                if(count==records.size()){head=(head+1)%records.size();--count;}
                records[(head+count)%records.size()]=value;++count;
            }
            Certificate summarize(std::uint64_t first,std::uint64_t end,bool all_phase)const noexcept {
                Certificate out{first,end,0,0,0,true,all_phase};
                auto cursor=first;bool have_q=false;
                for(std::size_t i=0;i<count && cursor<end;++i) {
                    const auto& r=records[(head+i)%records.size()];
                    if(r.end<=cursor)continue;
                    if(r.first>cursor){out.valid=out.phase=false;return out;}
                    out.valid=out.valid && r.valid;out.radius=std::max(out.radius,r.radius);
                    out.phase=all_phase?(out.phase && r.phase):(out.phase || r.phase);
                    out.q_low=have_q?std::min(out.q_low,r.q_low):r.q_low;
                    out.q_high=have_q?std::max(out.q_high,r.q_high):r.q_high;have_q=true;
                    cursor=std::min(end,r.end);
                }
                if(cursor!=end)out.valid=out.phase=false;
                return out;
            }
        } raw_certificates,output_certificates;
        std::shared_ptr<const HelperInputView> view;
        helper::AnalyticFrequencyTranslator translator;
        audio::Resampler converter;
        helper::PayloadFilter filter;
        std::uint64_t first_raw=0,next_raw=0,origin=0,first=0,eligible=0;
        std::vector<float> pending;
        RawHelperNotchDomain raw_domain;
        helper_prior_detail::Interval raw_certified_frequency{};
        const char* tracking_decline=nullptr;
        double last_notch_transition_l1=0;
        bool physical_notch_=false,two_tone_=false;
        std::uint64_t phase_support_first_=0,phase_support_end_=0;
        double current_correction_hz_=0;
        helper::CaptureBand helper_relative_band_{};
        double certified_helper_uncertainty_=-1;
        bool filter_before_converter()const noexcept {
            return physical_notch_;
        }
        static std::uint64_t logical_origin(const HelperInputView& v,std::uint64_t at,std::size_t delay) {
            if(at>UINT64_MAX-delay)throw Error("helper filter sample origin exceeds the capture counter");
            at+=delay;
            const auto whole=at/v.physical_rate,tail=(at%v.physical_rate)*v.logical_rate/v.physical_rate;
            if(whole>(UINT64_MAX-tail)/v.logical_rate)
                throw Error("helper filter sample origin exceeds the logical counter");
            return whole*v.logical_rate+tail;
        }
        Branch(HelperInputView v,std::uint64_t at,bool realtime=false,bool physical_notch=false,
               bool two_tone=false,helper::CaptureBand helper_relative_band={},double helper_uncertainty=0):view(std::make_shared<const HelperInputView>(v)),
            translator({v.physical_rate,v.correction_hz,v.input_band,at,v.conjugate,realtime},
                       helper::AnalyticFrequencyTranslator::workspace_required()),
            converter(v.physical_rate,v.logical_rate),
            filter(physical_notch?v.physical_rate:v.logical_rate,
                v.translated_helper_hz-(two_tone?realtime_helper_notch_offset_hz:0),
                two_tone?realtime_helper_notch_half_width_hz:225),
            first_raw(at),next_raw(at),origin(physical_notch?logical_origin(v,at,filter.delay()):
                at/v.physical_rate*v.logical_rate+(at%v.physical_rate)*v.logical_rate/v.physical_rate),
            first(origin+(physical_notch?0:filter.delay())),physical_notch_(physical_notch),two_tone_(two_tone),
            current_correction_hz_(v.correction_hz),helper_relative_band_(helper_relative_band) {
            if(physical_notch) {
                // Filter output zero is the raw center at at+M, not at or
                // at+2M. Admission puts that center on the exact logical grid.
                const auto quantum=v.physical_rate/std::gcd(v.physical_rate,v.logical_rate);
                if((at+filter.delay())%quantum)
                    throw Error("physical helper filter center is off the logical sample grid");
                const auto warmup=static_cast<long double>(
                    2*helper::AnalyticFrequencyTranslator::group_delay)/v.physical_rate+
                    static_cast<long double>(converter.source_lookahead_seconds());
                const auto samples=std::ceil(warmup*v.logical_rate)+1;
                if(samples>UINT64_MAX-origin)throw Error("helper filter warmup exceeds the sample counter");
                eligible=origin+static_cast<std::uint64_t>(samples);
            }else eligible=origin+static_cast<std::uint64_t>(std::ceil(
                static_cast<long double>(2*helper::AnalyticFrequencyTranslator::group_delay)*v.logical_rate/v.physical_rate))+
                filter.delay()+1;
            // Independent of notch placement, conversion must retain the
            // ENTIRE raw DATA image hull. The immutable converter rolls off
            // before its output Nyquist; a physical helper notch cannot make
            // DATA in that transition/passband gap recoverable.
            if(v.reference_passthrough && v.payload_output_band.upper_hz>=converter.passband_hz())
                throw Error("raw DATA band lies outside the immutable converter passband");
            // Fund the maximum queued capture lead before any ordinary owner
            // can depend on this view. No later PCM push grows this capacity.
            pending.reserve(2ULL*v.logical_rate);
            if(filter.maximum_amplitude_error(v.payload_output_band.lower_hz,v.payload_output_band.upper_hz)>.001)
                throw Error("conditional helper filter does not preserve the payload passband");
            if(two_tone && !v.reference_passthrough && !certify_helper_group(helper_uncertainty))
                throw Error("two-tone helper filter cannot certify the complete auxiliary and pilot stopband");
        }
        bool certify_helper_group(double uncertainty)noexcept {
            if(!two_tone_ || !std::isfinite(uncertainty) || uncertainty<0)return !two_tone_;
            if(uncertainty<=certified_helper_uncertainty_)return true;
            const auto certify=[&](double width) {
                const auto low=helper_prior_detail::sum(view->translated_helper_hz,
                    helper_prior_detail::sum(helper_relative_band_.lower_hz,-width).lower).lower;
                const auto high=helper_prior_detail::sum(view->translated_helper_hz,
                    helper_prior_detail::sum(helper_relative_band_.upper_hz,width).upper).upper;
                if(filter.maximum_response(low,high)>.001)return false;
                certified_helper_uncertainty_=width;return true;
            };
            // Qualify the actual stored FIR over an outward whole-Hz domain
            // once, so small observation jitter does not repeat this expensive
            // bound every poll. Near a limit, the exact interval may still fit.
            const auto envelope=std::max(1.,std::ceil(uncertainty));
            return certify(envelope) || (envelope!=uncertainty && certify(uncertainty));
        }
        bool track_raw_notch(const helper::Reference& reference)noexcept {
            using namespace helper_prior_detail;
            const auto decline=[&](const char* reason){tracking_decline=reason;return false;};
            if(!raw_domain.valid || !environment() || !reference.frequency_valid ||
               !std::isfinite(reference.frequency_hz) || !std::isfinite(reference.frequency_uncertainty_hz) ||
               reference.frequency_uncertainty_hz<0 || reference.sample_rate!=view->physical_rate)
                return decline("raw helper notch lacks a finite original LF/UTC or measured frequency domain");
            // The tracker already enforces its explicit .5-Hz/s operational
            // following gate. This is NOT a slew inferred from ppm accuracy or
            // a lease on future Doppler. Include the actual queued-source age
            // and symmetric filter support in this local engineering envelope.
            const auto distance=reference.sample>next_raw?reference.sample-next_raw:next_raw-reference.sample;
            const auto filter_rate=filter_before_converter()?view->physical_rate:view->logical_rate;
            const auto support=static_cast<double>(distance)/view->physical_rate+
                2.*helper::AnalyticFrequencyTranslator::group_delay/view->physical_rate+
                2.*converter.source_lookahead_seconds()+2.*filter.delay()/filter_rate;
            const auto radius=sum(reference.frequency_uncertainty_hz,.5*support).upper;
            const Interval current{sum(reference.frequency_hz,-radius).lower,
                sum(reference.frequency_hz,radius).upper};
            if(!finite(current))return decline("raw helper notch support exceeds its finite numeric domain");
            if(raw_certified_frequency.lower<=current.lower && current.upper<=raw_certified_frequency.upper) {
                tracking_decline=nullptr;return true;
            }
            // A one-Hz hold domain avoids expensive whole-band recertification
            // on every 20-ms observation. At .5 Hz/s sustained drift its center
            // needs renewal roughly every two seconds, independent of total
            // accumulated drift. Every accepted current interval is contained.
            const Interval proposal{sum(current.lower,-1).lower,sum(current.upper,1).upper};
            const auto bands=raw_helper_notch_bands(raw_domain,proposal);
            if(!bands.valid)return decline("raw helper or DATA band moved outside physical capture");
            if(bands.data.upper_hz>=converter.passband_hz())
                return decline("raw DATA band moved outside the immutable converter passband");
            if(!HelperInputFrontend::logical_notch_supported(filter_rate,reference.frequency_hz))
                return decline("raw helper moved outside the immutable physical/logical notch placement");
            const auto previous=filter.center_hz();double transition=0;
            if(!filter.retune(reference.frequency_hz-realtime_helper_notch_offset_hz,&transition))
                return decline("raw helper notch retuning is unsupported in the current FP environment");
            const auto preserved=filter.maximum_amplitude_error(bands.data.lower_hz,bands.data.upper_hz)<=.001;
            const auto stopped=filter.maximum_response(bands.helper.lower_hz,bands.helper.upper_hz)<=.001;
            if(!preserved || !stopped) {
                // Same rate, formula, length and FP environment: restore old
                // coefficient bits BEFORE any PCM push. No ring, delay, NCO,
                // pending output or absolute sample coordinate was changed.
                (void)filter.retune(previous);
                return decline("raw helper notch cannot certify full DATA passband and helper metadata stopband");
            }
            raw_certified_frequency=proposal;last_notch_transition_l1=transition;
            tracking_decline=nullptr;return true;
        }
        bool qualify_source_band(const helper::Reference& reference)noexcept {
            // Configured live oscillator models retain their original LF/UTC
            // differential geometry. An absent model is the existing explicit
            // laboratory path, not an inferred independent clock relation.
            if(!raw_domain.valid)return true;
            using namespace helper_prior_detail;
            if(!std::isfinite(reference.frequency_uncertainty_hz) || reference.frequency_uncertainty_hz<0)
                return false;
            const auto bands=raw_helper_notch_bands(raw_domain,
                {sum(reference.frequency_hz,-reference.frequency_uncertainty_hz).lower,
                 sum(reference.frequency_hz,reference.frequency_uncertainty_hz).upper});
            const auto input=translator.input_band();
            if(!bands.valid || bands.data.lower_hz<input.lower_hz || bands.data.upper_hz>input.upper_hz ||
               bands.helper.lower_hz<input.lower_hz || bands.helper.upper_hz>input.upper_hz) {
                tracking_decline="current DATA or complete helper group lies outside its immutable analytic capture certificate";
                return false;
            }
            return true;
        }
        std::uint64_t raw_at_logical(std::uint64_t logical)const noexcept {
            const auto whole=logical/view->logical_rate,tail=(logical%view->logical_rate)*view->physical_rate/view->logical_rate;
            return whole>(UINT64_MAX-tail)/view->physical_rate?UINT64_MAX:whole*view->physical_rate+tail;
        }
        Certificate produced_certificate(std::uint64_t first,std::uint64_t end)const noexcept {
            const auto filter_rate=filter_before_converter()?view->physical_rate:view->logical_rate;
            const auto pad=static_cast<std::uint64_t>(std::ceil((
                static_cast<double>(filter.delay())/filter_rate+converter.source_lookahead_seconds())*view->physical_rate))+2;
            const auto raw=raw_at_logical(first);
            const auto lower=raw>pad?raw-pad:0;
            // Production APIs expose only fully observed centered windows.
            // next_raw is the actual upper support frontier at production;
            // including its extra samples conservatively encloses q changes.
            auto result=raw_certificates.summarize(lower,next_raw,true);
            result.first=first;result.end=end;return result;
        }
        std::size_t bytes()const noexcept{return sizeof(*this)+sizeof(HelperInputView)+4*sizeof(void*)+translator.workspace_bytes()+
            converter.workspace_bytes()+filter.allocated_bytes()+pending.capacity()*sizeof(float);}
    };
    std::array<std::unique_ptr<Branch>,2> branches_;
    std::size_t grant_=0;
    std::uint64_t budget_serial_=0;
public:
    explicit HelperInputFrontend(std::size_t grant,std::uint64_t serial=0):grant_(grant),budget_serial_(serial){}
    // Reserve BOTH real-capture image branches before the first ordinary row
    // can depend on one. This is a lifetime ceiling, including the full fixed
    // pending capacity; actual bytes remain a separate telemetry quantity.
    static bool logical_notch_supported(std::uint32_t rate,double helper_hz)noexcept {
        return rate && std::isfinite(helper_hz) && helper_hz>625 && helper_hz+225<rate/2.;
    }
    static std::size_t physical_notch_taps(std::uint32_t rate) {
        const auto count=static_cast<std::size_t>(std::ceil(rate*.04))|1U;
        if(count<3||count>4097)throw Error("physical helper filter exceeds its optional 4097-tap bound");
        return count;
    }
    static std::size_t reservation_bound(std::uint32_t physical_rate,std::uint32_t logical_rate,
            double realtime_helper_hz=0) {
        if(!physical_rate || !logical_rate || logical_rate>120000000 || physical_rate>120000000)
            throw Error("conditional helper reservation has an invalid sample clock");
        auto taps=static_cast<std::size_t>((static_cast<std::uint64_t>(logical_rate)+24)/25+4)|1U;
        if(realtime_helper_hz>0 && !logical_notch_supported(logical_rate,realtime_helper_hz))
            taps=std::max(taps,physical_notch_taps(physical_rate));
        const auto branch=sizeof(Branch)+sizeof(HelperInputView)+4*sizeof(void*)+
            helper::AnalyticFrequencyTranslator::workspace_required()+converter_reservation(physical_rate,logical_rate)+
            taps*(sizeof(double)+sizeof(float))+2ULL*logical_rate*sizeof(float)+4096;
        if(branch>(SIZE_MAX-sizeof(HelperInputFrontend))/2)
            throw Error("conditional helper reservation exceeds the platform range");
        return sizeof(HelperInputFrontend)+2*branch;
    }
    std::size_t bytes()const noexcept {
        auto result=sizeof(*this);for(const auto& branch:branches_)if(branch)result+=branch->bytes();return result;
    }
    bool active()const noexcept{return branches_[0] || branches_[1];}
    bool reference_passthrough()const noexcept {
        return branches_[0] && branches_[0]->view->reference_passthrough;
    }
    bool original_prior_retained()const noexcept {
        return branches_[0] && branches_[0]->view->original_prior_retained;
    }
    const char* tracking_decline_reason()const noexcept {
        return branches_[0]?branches_[0]->tracking_decline:nullptr;
    }
    double last_notch_transition_l1()const noexcept {
        return branches_[0]?branches_[0]->last_notch_transition_l1:0;
    }
    void reset()noexcept {branches_={};}
    // Manually enabled reference mode has one upstream converter and one
    // ordinary receiver. Positive real-PCM capture is the supported image;
    // bands crossing DC/Nyquist decline instead of adding a second receiver.
    void admit_realtime(const modem::Config& config,double expected,const helper::Reference& reference,
            std::uint32_t physical_rate,double passband,std::uint64_t next_raw,double utc_rate_limit=0) {
        if(active())return;
        if(!reference.frequency_valid || reference.sample_rate!=physical_rate ||
           !std::isfinite(reference.frequency_hz) || !std::isfinite(reference.frequency_uncertainty_hz) ||
           reference.frequency_uncertainty_hz<0 || reference.frequency_hz<=0)return;
        auto correction=expected-reference.frequency_hz;
        const auto half=helper::occupied_bandwidth(config)/2;
        const auto uncertainty=std::max(32.,4*reference.frequency_uncertainty_hz);
        const bool folded=config.carrier_hz-half-uncertainty<=10;
        const bool preserve=modem::helper_reference_prefers_original_prior(config,
            reference.frequency_uncertainty_hz,expected,utc_rate_limit,
            modem::HelperReferenceClockRelation::common_corrected_source);
        if(folded || preserve)correction=0; // Preserve the existing raw input basis.
        const auto data_input=config.carrier_hz-correction;
        const auto low=folded?10.001:std::min(data_input-half,reference.frequency_hz-625)-uncertainty;
        const auto high=std::max(data_input+half,reference.frequency_hz+225)+uncertainty;
        if(low<=10 || high>=passband || low+correction<=10 ||
           high+correction>=physical_rate/2.-10)
            throw Error("helper reference or payload lies outside the captured positive passband");
        HelperInputView view;view.id=1;view.budget_serial=budget_serial_;
        view.physical_rate=physical_rate;view.logical_rate=config.sample_rate;
        view.helper_hz=expected;view.correction_hz=correction;
        view.reference_passthrough=folded || preserve;view.original_prior_retained=preserve;
        view.translated_helper_hz=view.reference_passthrough?reference.frequency_hz:expected;
        view.input_band={low,high};
        view.payload_output_band={folded?0.:config.carrier_hz-half-uncertainty,config.carrier_hz+half+uncertainty};
        RawHelperNotchDomain raw_domain;
        helper::CaptureBand helper_relative_band{realtime_helper_group_low_hz,realtime_helper_group_high_hz};
        if(config.oscillator_search) {
            raw_domain=raw_helper_notch_domain(config,expected,passband,utc_rate_limit);
            if(!raw_domain.valid)throw Error("two-tone helper has no valid original clock domain");
            const auto band=shared_helper_detail::times(
                {realtime_helper_group_low_hz,realtime_helper_group_high_hz},raw_domain.one_plus_clock);
            helper_relative_band={band.lower,band.upper};
        }
        if(view.reference_passthrough) {
            const auto bands=raw_helper_notch_bands(raw_domain,
                {helper_prior_detail::sum(reference.frequency_hz,-reference.frequency_uncertainty_hz).lower,
                 helper_prior_detail::sum(reference.frequency_hz,reference.frequency_uncertainty_hz).upper});
            if(!bands.valid)throw Error("raw helper notch has no valid original LF/UTC and capture-band certificate");
            // q=0/conjugate=false is only the existing fixed delay, without a
            // Hilbert analytic inversion. Its declared positive band need not
            // pretend a folded DATA image is positive complex baseband.
            view.input_band={10.001,passband};view.payload_output_band=bands.data;
        }
        const bool physical_notch=!logical_notch_supported(view.logical_rate,view.translated_helper_hz);
        const auto delay=physical_notch?physical_notch_taps(physical_rate)/2:0;
        if(physical_notch && reservation_bound(physical_rate,config.sample_rate,view.translated_helper_hz)>grant_)
            throw Error("physical helper filter was not prepaid by its frontend workspace grant");
        if(physical_notch && !logical_notch_supported(physical_rate,view.translated_helper_hz))
            throw Error("helper notch is outside both physical and logical passbands");
        const auto quantum=physical_rate/std::gcd(physical_rate,config.sample_rate);
        if(next_raw>UINT64_MAX-delay)return;
        const auto remainder=(next_raw+delay)%quantum;
        if(remainder && next_raw>UINT64_MAX-(quantum-remainder))return;
        const auto first=remainder?next_raw+quantum-remainder:next_raw;
        if(first>UINT64_MAX-delay)return;
        auto branch=std::make_unique<Branch>(view,first,!folded && !preserve,physical_notch,true,
            helper_relative_band,reference.frequency_uncertainty_hz);
        branch->raw_domain=raw_domain;
        if(view.reference_passthrough) {
            if(!branch->track_raw_notch(reference))throw Error(branch->tracking_decline);
        }
        if(sizeof(*this)+branch->bytes()>grant_)throw Error("realtime helper converter exceeds its workspace grant");
        branches_[0]=std::move(branch);
    }
    bool track_frequency(const helper::Reference& reference,double expected)noexcept {
        if(!reference.frequency_valid || !std::isfinite(reference.frequency_hz))return false;
        auto& branch=branches_[0];
        // A prior-preserving view never changes its q=0 oscillator slope or
        // phase, even if later observations become more precise. Existing
        // corrected views retain their old continuous-NCO policy unchanged.
        if(branch && branch->view->reference_passthrough)
            return branch->track_raw_notch(reference);
        const auto accepted=branch && reference.sample_rate==branch->view->physical_rate &&
            branch->qualify_source_band(reference) &&
            branch->certify_helper_group(reference.frequency_uncertainty_hz) &&
            branch->translator.track_frequency(expected-reference.frequency_hz);
        if(accepted) {
            branch->current_correction_hz_=expected-reference.frequency_hz;
            branch->tracking_decline=nullptr;
        }
        return accepted;
    }
    bool track_pilot(const helper::AnalyticFrequencyTranslator::PilotObservation& observation)noexcept {
        auto& branch=branches_[0];
        // A folded/prior-preserving mono branch has no analytic phase image.
        // Its immutable raw frequency origin remains independently useful.
        if(!branch || branch->view->reference_passthrough || branch->view->conjugate ||
           observation.first_sample>UINT64_MAX-2*helper::AnalyticFrequencyTranslator::group_delay ||
           branch->translator.next_sample()<observation.first_sample+2*helper::AnalyticFrequencyTranslator::group_delay ||
           !branch->translator.track_pilot(observation))return false;
        branch->phase_support_first_=branch->translator.next_sample();
        branch->phase_support_end_=observation.end_sample;
        branch->current_correction_hz_=observation.expected_hz-observation.tracking_frequency_hz.value_or(observation.observed_hz);
        return true;
    }
    double current_correction_hz()const noexcept{return branches_[0]?branches_[0]->current_correction_hz_:0;}
    void admit(const modem::Config& config,double expected,const helper::Reference& reference,
               std::uint32_t physical_rate,double passband,std::uint64_t next_raw) {
        if(active())return;
        auto views=shared_helper_input_views(config,expected,reference,physical_rate,passband);
        for(auto& view:views)if(view)view->budget_serial=budget_serial_;
        const auto quantum=physical_rate/std::gcd(physical_rate,config.sample_rate);
        const auto remainder=next_raw%quantum;
        if(remainder && next_raw>UINT64_MAX-(quantum-remainder))return;
        const auto first=remainder?next_raw+quantum-remainder:next_raw;
        std::array<std::unique_ptr<Branch>,2> staged;
        std::size_t total=sizeof(*this);
        const auto taps=static_cast<std::size_t>(std::ceil(config.sample_rate*.04))|1U;
        auto reserved=sizeof(*this);
        for(const auto& view:views)if(view)reserved+=sizeof(Branch)+sizeof(HelperInputView)+
            helper::AnalyticFrequencyTranslator::workspace_required()+converter_reservation(physical_rate,config.sample_rate)+
            taps*(sizeof(double)+sizeof(float))+2ULL*config.sample_rate*sizeof(float)+4096;
        if(reserved>grant_)throw Error("conditional helper converter reservation exceeds optional frontend grant");
        for(std::size_t i=0;i<views.size();++i)if(views[i]) {
            staged[i]=std::make_unique<Branch>(*views[i],first);total+=staged[i]->bytes();
        }
        if(total>grant_)throw Error("conditional helper input views exceed their workspace grant");
        branches_=std::move(staged);
    }
    void push(std::span<const float> input,std::uint64_t first_raw,std::optional<double> corrected_residual_hz={}) {
        std::array<float,2048> translated{},converted{},filtered{};
        for(auto& owned:branches_)if(owned) {
            auto& branch=*owned;
            if(first_raw+input.size()<=branch.next_raw)continue;
            const auto skip=branch.next_raw>first_raw?branch.next_raw-first_raw:0;
            if(first_raw+skip!=branch.next_raw)throw Error("conditional helper input view has a physical capture gap");
            const auto cert_first=first_raw+skip,cert_end=first_raw+input.size();
            branch.raw_certificates.append({cert_first,cert_end,corrected_residual_hz.value_or(0),
                branch.current_correction_hz_,branch.current_correction_hz_,
                corrected_residual_hz && std::isfinite(*corrected_residual_hz) && *corrected_residual_hz>=0,
                branch.phase_support_first_<=cert_first && cert_end<=branch.phase_support_end_});
            for(std::size_t at=static_cast<std::size_t>(skip);at<input.size();) {
                const auto count=std::min(translated.size(),input.size()-at);
                branch.translator.push(input.subspan(at,count),std::span(translated).first(count),first_raw+at);
                branch.next_raw+=count;at+=count;
                // A helper outside the logical Nyquist band is removed
                // BEFORE decimation. The same single FIR owner is used; no
                // unfiltered helper metadata can alias into payload samples.
                const auto converter_count=branch.filter_before_converter()?
                    branch.filter.push(std::span(translated).first(count),filtered):count;
                const auto converter_input=branch.filter_before_converter()?
                    std::span<const float>(filtered).first(converter_count):std::span<const float>(translated).first(count);
                std::size_t consumed=0;
                for(;;) {
                    const auto progress=branch.converter.process(converter_input.subspan(consumed),converted);
                    consumed+=progress.consumed;
                    const auto produced=branch.filter_before_converter()?progress.produced:
                        branch.filter.push(std::span(converted).first(progress.produced),filtered);
                    const auto* output=branch.filter_before_converter()?converted.data():filtered.data();
                    if(produced) {
                        if(branch.pending.size()+produced>2ULL*branch.view->logical_rate)
                            throw Error("conditional helper input-view queue exceeded its bounded capture lead");
                        const auto output_first=branch.first+branch.pending.size();
                        branch.output_certificates.append(branch.produced_certificate(output_first,output_first+produced));
                        branch.pending.insert(branch.pending.end(),output,output+produced);
                        if(branch.first<branch.eligible) {
                            const auto warmup=static_cast<std::size_t>(std::min<std::uint64_t>(
                                branch.pending.size(),branch.eligible-branch.first));
                            branch.pending.erase(branch.pending.begin(),branch.pending.begin()+warmup);branch.first+=warmup;
                        }
                    }
                    if(!progress.consumed && !progress.produced)break;
                    if(consumed==converter_count && !progress.produced)break;
                }
                if(consumed!=converter_count)throw Error("conditional helper resampler did not consume its physical input");
            }
        }
        if(bytes()>grant_)throw Error("conditional helper input views exceed their workspace grant");
    }
    std::vector<HelperViewBlock> take(std::uint64_t logical_frontier,
            const std::optional<helper::Reference>& reference,std::uint64_t raw_captured_end=0,
            const std::optional<HelperFrequencyInterval>& frequency_interval={},
            const std::optional<HelperFrequencyPublication>& frequency_publication={}) {
        std::vector<HelperViewBlock> result;result.reserve(2);
        for(auto& owned:branches_)if(owned) {
            auto& branch=*owned;
            if(branch.first>=logical_frontier || branch.pending.empty())continue;
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(branch.pending.size(),logical_frontier-branch.first));
            HelperViewBlock block;block.view=branch.view;block.first_sample=branch.first;
            block.samples.assign(branch.pending.begin(),branch.pending.begin()+count);
            // Prepare both signed blocks before mutating either queue. A
            // later allocation failure must leave every ordinary input sample
            // owned by its original branch, including an accepted payload view.
            // Keep raw frequency provenance for conditional LF catalogue birth.
            // Phase consumers need a separately transformed/dated certificate.
            if(reference)block.reference=*reference;
            if(frequency_publication)try {
                block.frequency_publication=std::make_unique<const HelperFrequencyPublication>(*frequency_publication);
            }catch(const std::bad_alloc&) {
                // The ordinary immutable view PCM still commits. Optional
                // provenance cannot turn allocation failure into an input gap.
            }
            block.raw_captured_end=raw_captured_end;block.frequency_interval=frequency_interval;
            const auto correction=branch.output_certificates.summarize(block.first_sample,
                block.first_sample+block.samples.size(),false);
            if(correction.valid)block.corrected_rf_uncertainty_hz=correction.radius;
            block.applied_correction_hz={correction.q_low,correction.q_high};
            block.common_phase_corrected=correction.phase;
            result.push_back(std::move(block));
        }
        for(const auto& block:result) {
            auto& branch=*branches_[static_cast<std::size_t>(block.view->id-1)];
            const auto count=block.samples.size();
            branch.pending.erase(branch.pending.begin(),branch.pending.begin()+count);branch.first+=count;
        }
        return result;
    }
};
}
