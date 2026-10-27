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

inline std::optional<modem::SampledSimulationChannel::PreconversionCapture> shared_helper_capture(
        const modem::Config& config,double expected) {
    if(!config.oscillator_search)return {};
    const auto& policy=*config.oscillator_search;
    if(policy.reference!=modem::OscillatorReference::shared_radio || policy.rf_shift_hz<=0)return {};
    const auto sign=policy.sideband==modem::OscillatorSideband::upper?1.L:-1.L;
    const auto epsilon=static_cast<long double>(policy.rf.accuracy_ppm)*policy.margin*1e-6L;
    const auto kd=sign*policy.rf_shift_hz+config.carrier_hz,kh=sign*policy.rf_shift_hz+expected;
    const auto half=helper::occupied_bandwidth(config)/2;
    const auto low=std::min(static_cast<long double>(config.carrier_hz)-std::abs(kd)*epsilon-half*(1+epsilon),
        static_cast<long double>(expected)-std::abs(kh)*epsilon-150*(1+epsilon));
    const auto high=std::max(static_cast<long double>(config.carrier_hz)+std::abs(kd)*epsilon+half*(1+epsilon),
        static_cast<long double>(expected)+std::abs(kh)*epsilon+150*(1+epsilon));
    const auto rate=std::ceil(std::max(std::abs(low),std::abs(high))/.42L)+64;
    if(!std::isfinite(low)||!std::isfinite(high)||!std::isfinite(rate)||rate>120000000)return {};
    return modem::SampledSimulationChannel::PreconversionCapture{static_cast<double>(low),
        static_cast<double>(high),static_cast<std::uint32_t>(std::max(64.L,rate))};
}

// A real capture cannot distinguish the two signs of its analytic image.
// Each admissible branch has its own immutable NCO/input provenance. These
// facts describe a conditional common link, never full ordinary RF coverage.
struct HelperInputView {
    std::uint64_t id=0,budget_serial=0;
    std::uint32_t physical_rate=0,logical_rate=0;
    double helper_hz=0,clock_center=0,clock_lower=0,clock_upper=0;
    double correction_hz=0,translated_helper_hz=0;
    bool reference_passthrough=false;
    helper::CaptureBand input_band{},payload_output_band{};
    bool conjugate=false;
};
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
        std::shared_ptr<const HelperInputView> view;
        helper::AnalyticFrequencyTranslator translator;
        audio::Resampler converter;
        helper::PayloadFilter filter;
        std::uint64_t first_raw=0,next_raw=0,origin=0,first=0,eligible=0;
        std::vector<float> pending;
        Branch(HelperInputView v,std::uint64_t at,bool realtime=false):view(std::make_shared<const HelperInputView>(v)),
            translator({v.physical_rate,v.correction_hz,v.input_band,at,v.conjugate,realtime},
                       helper::AnalyticFrequencyTranslator::workspace_required()),
            converter(v.physical_rate,v.logical_rate),filter(v.logical_rate,v.translated_helper_hz),
            first_raw(at),next_raw(at),origin(at/v.physical_rate*v.logical_rate+
                (at%v.physical_rate)*v.logical_rate/v.physical_rate),first(origin+filter.delay()) {
            eligible=origin+static_cast<std::uint64_t>(std::ceil(
                static_cast<long double>(2*helper::AnalyticFrequencyTranslator::group_delay)*v.logical_rate/v.physical_rate))+
                filter.delay()+1;
            // Fund the maximum queued capture lead before any ordinary owner
            // can depend on this view. No later PCM push grows this capacity.
            pending.reserve(2ULL*v.logical_rate);
            if(filter.maximum_amplitude_error(v.payload_output_band.lower_hz,v.payload_output_band.upper_hz)>.001)
                throw Error("conditional helper filter does not preserve the payload passband");
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
    static std::size_t reservation_bound(std::uint32_t physical_rate,std::uint32_t logical_rate) {
        if(!physical_rate || !logical_rate || logical_rate>120000000 || physical_rate>120000000)
            throw Error("conditional helper reservation has an invalid sample clock");
        const auto taps=static_cast<std::size_t>((static_cast<std::uint64_t>(logical_rate)+24)/25+4)|1U;
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
    void reset()noexcept {branches_={};}
    // Manually enabled reference mode has one upstream converter and one
    // ordinary receiver. Positive real-PCM capture is the supported image;
    // bands crossing DC/Nyquist decline instead of adding a second receiver.
    void admit_realtime(const modem::Config& config,double expected,const helper::Reference& reference,
            std::uint32_t physical_rate,double passband,std::uint64_t next_raw) {
        if(active())return;
        if(!reference.frequency_valid || reference.sample_rate!=physical_rate ||
           !std::isfinite(reference.frequency_hz) || !std::isfinite(reference.frequency_uncertainty_hz) ||
           reference.frequency_uncertainty_hz<0 || reference.frequency_hz<=0)return;
        auto correction=expected-reference.frequency_hz;
        const auto half=helper::occupied_bandwidth(config)/2;
        const auto uncertainty=std::max(32.,4*reference.frequency_uncertainty_hz);
        const bool folded=config.carrier_hz-half-uncertainty<=10;
        if(folded)correction=0; // Ordinary demodulator applies the measured origin.
        const auto data_input=config.carrier_hz-correction;
        const auto low=folded?10.001:std::min(data_input-half,reference.frequency_hz-175)-uncertainty;
        const auto high=std::max(data_input+half,reference.frequency_hz+175)+uncertainty;
        if(low<=10 || high>=passband || low+correction<=10 ||
           high+correction>=physical_rate/2.-10)
            throw Error("helper reference or payload lies outside the captured positive passband");
        HelperInputView view;view.id=1;view.budget_serial=budget_serial_;
        view.physical_rate=physical_rate;view.logical_rate=config.sample_rate;
        view.helper_hz=expected;view.correction_hz=correction;
        view.reference_passthrough=folded;
        view.translated_helper_hz=folded?reference.frequency_hz:expected;
        view.input_band={low,high};
        view.payload_output_band={folded?0.:config.carrier_hz-half-uncertainty,config.carrier_hz+half+uncertainty};
        const auto quantum=physical_rate/std::gcd(physical_rate,config.sample_rate);
        const auto remainder=next_raw%quantum;
        if(remainder && next_raw>UINT64_MAX-(quantum-remainder))return;
        const auto first=remainder?next_raw+quantum-remainder:next_raw;
        auto branch=std::make_unique<Branch>(view,first,!folded);
        if(sizeof(*this)+branch->bytes()>grant_)throw Error("realtime helper converter exceeds its workspace grant");
        branches_[0]=std::move(branch);
    }
    bool track_frequency(const helper::Reference& reference,double expected)noexcept {
        if(!reference.frequency_valid || !std::isfinite(reference.frequency_hz))return false;
        auto& branch=branches_[0];
        if(branch && branch->view->reference_passthrough)
            return reference.sample_rate==branch->view->physical_rate &&
                std::abs(reference.frequency_hz-branch->view->translated_helper_hz)<25;
        return branch && reference.sample_rate==branch->view->physical_rate &&
            branch->translator.track_frequency(expected-reference.frequency_hz);
    }
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
    void push(std::span<const float> input,std::uint64_t first_raw) {
        std::array<float,2048> translated{},converted{},filtered{};
        for(auto& owned:branches_)if(owned) {
            auto& branch=*owned;
            if(first_raw+input.size()<=branch.next_raw)continue;
            const auto skip=branch.next_raw>first_raw?branch.next_raw-first_raw:0;
            if(first_raw+skip!=branch.next_raw)throw Error("conditional helper input view has a physical capture gap");
            for(std::size_t at=static_cast<std::size_t>(skip);at<input.size();) {
                const auto count=std::min(translated.size(),input.size()-at);
                branch.translator.push(input.subspan(at,count),std::span(translated).first(count),first_raw+at);
                branch.next_raw+=count;at+=count;
                std::size_t consumed=0;
                for(;;) {
                    const auto progress=branch.converter.process(std::span(translated).first(count).subspan(consumed),converted);
                    consumed+=progress.consumed;
                    const auto produced=branch.filter.push(std::span(converted).first(progress.produced),filtered);
                    if(produced) {
                        if(branch.pending.size()+produced>2ULL*branch.view->logical_rate)
                            throw Error("conditional helper input-view queue exceeded its bounded capture lead");
                        branch.pending.insert(branch.pending.end(),filtered.begin(),filtered.begin()+produced);
                        if(branch.first<branch.eligible) {
                            const auto warmup=static_cast<std::size_t>(std::min<std::uint64_t>(
                                branch.pending.size(),branch.eligible-branch.first));
                            branch.pending.erase(branch.pending.begin(),branch.pending.begin()+warmup);branch.first+=warmup;
                        }
                    }
                    if(!progress.consumed && !progress.produced)break;
                    if(consumed==count && !progress.produced)break;
                }
                if(consumed!=count)throw Error("conditional helper resampler did not consume its physical input");
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
