#include "datapump/helper_carrier.hpp"
#include "datapump/types.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace datapump::helper {
namespace {
constexpr double tau=2*std::numbers::pi;
constexpr double nominal_data_mean_power=.153125;
constexpr long double dwell=.4L,ramp=.004L,metadata_end=.392L,symbol_time=.020L;
constexpr long double punctuation_begin=.152L,punctuation_mid=.160L,punctuation_end=.168L;
// Literal rational-calendar endpoints avoid accumulated rounding at an exact source sample.
constexpr long double punctuation_latest_start=.128L,punctuation_protected_end=.192L;
// The calendar is reduced to [0,2) before dwell subtraction. Allow32
// long-double epsilons per unit for fmod, division and subtract rounding;
// even a binary64 long double changes a guard by less than1.5e-14 seconds.
long double calendar_roundoff(long double normalized) {
    return 32*std::numeric_limits<long double>::epsilon()*std::max(1.L,std::abs(normalized));
}
double smooth(long double x) {
    const auto t=static_cast<double>(std::clamp(x,0.L,1.L));
    return t*t*t*(10+t*(-15+6*t));
}
double auxiliary_smooth(long double x) {
    const auto t=static_cast<double>(std::clamp(x,0.L,1.L));
    return t*t*t*t*(35+t*(-84+t*(70-20*t)));
}
void validate_callsign(std::string_view callsign) {
    if(callsign.size()>16)throw Error("Helper Carrier callsign must contain at most 16 seven-bit ASCII characters");
    for(const char c:callsign)
        if(static_cast<unsigned char>(c)==0 || static_cast<unsigned char>(c)>127)throw Error("Helper Carrier callsign must be non-NUL seven-bit ASCII");
}
std::pair<double,double> gains_from_ratio(double power_ratio,double helper_mean,
        double data_mean=nominal_data_mean_power) {
    const auto relative_peak=std::sqrt(2*data_mean*power_ratio/helper_mean);
    const auto composite_peak=relative_peak*(pilot_amplitude_fraction+low_pilot_amplitude_fraction+auxiliary_amplitude_fraction);
    return {relative_peak,.95/(1+composite_peak)};
}
std::complex<double> auxiliary_modulation(long double local,
        std::span<const std::complex<double>> symbols,bool punctuate) {
    // The two taper halves meet at zero with zero derivatives. This makes the
    // phase change at 160ms while keeping the 152–160 and 160–168ms envelope
    // transitions independently smooth.
    double reversal=1;
    if(punctuate) {
        if(local<punctuation_mid)
            reversal=1-auxiliary_smooth((local-punctuation_begin)/(punctuation_mid-punctuation_begin));
        else
            reversal=-auxiliary_smooth((local-punctuation_mid)/(punctuation_end-punctuation_mid));
    }
    std::complex<double> result{reversal,0};
    const auto begin=metadata_end-symbols.size()*symbol_time;
    if(!symbols.empty() && local>=begin) {
        const auto position=(local-begin)/symbol_time;
        const auto index=static_cast<std::size_t>(std::floor(position));
        if(index<symbols.size()) {
            const auto previous=index?symbols[index-1]:std::complex<double>{1,0};
            result*=previous+(symbols[index]-previous)*auxiliary_smooth(position-index);
        } else {
            // Hold the last APSK value throughout the boundary taper. There is
            // no return to the inverted straight reference before zero.
            result*=symbols.back();
        }
    }
    return result;
}
// Bound the periodic complex envelope's energy in either data sideband. The
// pilot4ms ramps retain quintic smootherstep (C2), whose Beta(3,3) derivative
// transform has magnitude <=15*(3+3z+z^2)/z^5, z=pi*f*T, and <=1.
// The auxiliary8ms reversal halves and 20ms symbol transitions use septic
// smootherstep (C3), with bound 105*(15+15z+6z^2+z^3)/z^7. For z>=10 this
// is below the old quintic bound; the minimum450Hz planning distance gives
// z>=11.31 for8ms transitions. Bound the longer 20ms symbol transitions by
// the same conservative 8ms envelope so the planned frequencies do not move.
// The bound retains two unit4ms ramps, two unit8ms reversal halves and
// <=nine magnitude-two metadata transitions. Integration by parts supplies the
// additional 1/(2*pi*f*dwell). Coefficients therefore fall at least as f^-4;
// sum their squared bound over the 2.5Hz dwell harmonics. Factor four includes
// the real carrier's other image and possible constructive addition. This is
// a digital periodic-waveform bound, not an RF/DAC distortion certificate.
double leakage_bound(double distance,double bandwidth,double transition_magnitude) {
    const auto derivative_transform=[&](double duration) {
        const auto z=std::numbers::pi*distance*duration;
        return std::min(1.,15*(3+z*z+3*z)/std::pow(z,5));
    };
    const auto coefficient=(2*derivative_transform(.004)+
        transition_magnitude*derivative_transform(.008))/(tau*distance*.4);
    const auto harmonics=1+std::min(std::ceil(bandwidth/2.5),distance/(7*2.5));
    return 4*coefficient*coefficient*harmonics;
}
}
void validate(const Options& options) {
    if(!std::isfinite(options.power_w)||options.power_w<0||
       !std::isfinite(options.data_power_w)||options.data_power_w<=0||
       !std::isfinite(options.ratio_limit_db)||options.ratio_limit_db<0||options.ratio_limit_db>90)
        throw Error("invalid Helper Carrier power or ratio limit");
    switch(options.frequency) {
        case Frequency::high:break;
        default:throw Error("Helper Carrier requires High frequency placement");
    }
    validate_callsign(options.callsign);
}
double effective_helper_power(const Options& options) {
    validate(options);
    return std::min(std::max(0.,options.power_w-options.data_power_w),
        options.data_power_w*std::pow(10.,options.ratio_limit_db/10));
}
MeanPower mean_power(const Options& options,bool pilot_gating,const PowerReference* supplied) {
    validate(options);
    struct PreviewCache {Options options;bool pilot_gating=false;MeanPower power;};
    thread_local std::optional<PreviewCache> preview;
    if(!supplied&&preview&&preview->options==options&&preview->pilot_gating==pilot_gating)return preview->power;
    const PowerReference default_reference;
    const auto& reference=supplied?*supplied:default_reference;
    if(!std::isfinite(reference.begin_seconds)||!std::isfinite(reference.end_seconds)||
       reference.end_seconds<=reference.begin_seconds||reference.end_seconds-reference.begin_seconds>2.000001L)
        throw Error("invalid Helper Carrier active power reference interval");
    if(reference.data_mean_power&&(!std::isfinite(*reference.data_mean_power)||*reference.data_mean_power<=0))
        throw Error("invalid Helper Carrier actual Robust mean power");
    for(const auto endpoint:{reference.helper_start_seconds,reference.helper_end_seconds})
        if(endpoint&&!std::isfinite(*endpoint))throw Error("invalid Helper Carrier power reference envelope");
    std::array<std::complex<double>,9> symbols{};
    std::size_t count=0;
    std::optional<std::uint64_t> cached_time;
    // The bounded actual-waveform reference resolves a 4ms ramp with at least
    // 65 nodes even at the maximum two-second window. No calibration is
    // performed while samples are emitted.
    constexpr auto nodes=power_reference_nodes;
    double pilot=0,auxiliary=0,accepted=0;
    for(unsigned i=0;i<nodes;++i) {
        const auto seconds=power_reference_node(reference,i);
        if(reference.active&&!reference.active(seconds))continue;
        auto local=std::fmod(static_cast<long double>(reference.epoch%2)+seconds,2.L);
        if(local<0)local+=2;
        local-=std::floor(local/dwell)*dwell;
        if((options.timecode||!options.callsign.empty())&&local>=metadata_end-9*symbol_time) {
            const auto absolute_dwell=static_cast<long double>(reference.epoch)+seconds-local;
            if(absolute_dwell>=0&&absolute_dwell<4294967296.L) {
                const auto ticks=static_cast<std::uint64_t>(std::round(absolute_dwell*65536));
                if(cached_time!=ticks) {
                    const auto packet=metadata_packet(ticks,options.callsign,reference.time_key?&*reference.time_key:nullptr);
                    count=packet.count;
                    for(std::size_t k=0;k<count;++k)symbols[k]=apsk_point(packet.symbols[k]);
                    cached_time=ticks;
                }
            } else count=0;
        }
        auto pg=pilot_gating?smooth(std::min(local,dwell-local)/ramp):1.;
        auto ag=smooth(std::min(local,dwell-local)/.008L);
        if(reference.helper_start_seconds) {
            const auto start=smooth((seconds-*reference.helper_start_seconds)/ramp);pg*=start;ag*=start;
        }
        if(reference.helper_end_seconds) {
            const auto end=smooth((*reference.helper_end_seconds-seconds)/ramp);pg*=end;ag*=end;
        }
        const auto roundoff=calendar_roundoff(local);
        const bool punctuation_fits=reference.punctuate &&
            (!reference.helper_start_seconds||(*reference.helper_start_seconds-seconds)+local<=punctuation_latest_start+roundoff) &&
            (!reference.helper_end_seconds||(*reference.helper_end_seconds-seconds)+local+roundoff>=punctuation_protected_end);
        pilot+=pg*pg;
        auxiliary+=ag*ag*std::norm(auxiliary_modulation(local,std::span(symbols).first(count),punctuation_fits));
        ++accepted;
    }
    if(!accepted)throw Error("Helper Carrier active power reference contains no active Robust samples");
    MeanPower result{pilot_power_fraction*pilot/accepted,low_pilot_power_fraction*pilot/accepted,
        auxiliary_power_fraction*auxiliary/accepted};
    result.total=result.high_pilot+result.low_pilot+result.auxiliary;
    if(!std::isfinite(result.total)||result.total<=0)
        throw Error("Helper Carrier active power reference has no helper energy");
    if(!supplied)preview=PreviewCache{options,pilot_gating,result};
    return result;
}
double calibrated_data_gain(const Options& options,bool pilot_gating,const PowerReference* reference) {
    validate(options);
    if(options.power_w==0||effective_helper_power(options)==0)return 1;
    return gains_from_ratio(effective_helper_power(options)/options.data_power_w,
        mean_power(options,pilot_gating,reference).total,
        reference&&reference->data_mean_power?*reference->data_mean_power:nominal_data_mean_power).second;
}
ReferenceLevel reference_level(const Options& options,double nominal,double scale,double payload_fraction,double usable_fraction) {
    validate(options);
    if(options.power_w<=0||!std::isfinite(nominal)||
       !std::isfinite(scale)||scale<=0||scale>1||
       !std::isfinite(payload_fraction)||payload_fraction<=0||payload_fraction>1||
       !std::isfinite(usable_fraction)||usable_fraction<=0||usable_fraction>1)
        throw Error("invalid Helper Carrier reference-level inputs");
    const auto ratio=static_cast<long double>(effective_helper_power(options))/options.data_power_w;
    const auto payload=static_cast<long double>(nominal)+20*std::log10(static_cast<long double>(scale));
    if(ratio==0) {
        const auto absent=-std::numeric_limits<double>::infinity();
        return {0,absent,absent,static_cast<double>(payload+10*std::log10(static_cast<long double>(payload_fraction))),
            0,absent,absent,absent};
    }
    const auto straight=payload+10*std::log10(ratio);
    const auto usable=straight+10*std::log10(static_cast<long double>(usable_fraction));
    const auto average=payload+10*std::log10(static_cast<long double>(payload_fraction));
    ReferenceLevel result{static_cast<double>(ratio),static_cast<double>(straight),
        static_cast<double>(usable),static_cast<double>(average)};
    const auto mean=mean_power(options).total;
    result.pilot_effective_power_ratio=static_cast<double>(ratio*pilot_power_fraction/mean);
    result.pilot_straight_cn0_db_hz=static_cast<double>(straight+10*std::log10(pilot_power_fraction/mean));
    result.pilot_usable_cn0_db_hz=static_cast<double>(usable+10*std::log10(pilot_power_fraction/mean));
    result.auxiliary_straight_cn0_db_hz=static_cast<double>(straight+10*std::log10(auxiliary_power_fraction/mean));
    if(!std::isfinite(result.effective_power_ratio)||result.effective_power_ratio<=0||
       !std::isfinite(result.straight_cn0_db_hz)||!std::isfinite(result.usable_cn0_db_hz)||
       !std::isfinite(result.payload_average_cn0_db_hz))
        throw Error("Helper Carrier reference level exceeds the numerical range");
    return result;
}
MonoPassband mono_passband(const PlacementBand& band) {
    if(!std::isfinite(band.carrier_hz)||band.carrier_hz<=0||
       !std::isfinite(band.waveform_bandwidth_hz)||band.waveform_bandwidth_hz<=0||
       !std::isfinite(band.gate_bandwidth_hz)||band.gate_bandwidth_hz<0||
       band.carrier_hz-band.waveform_bandwidth_hz/2<0)
        throw Error("Helper Carrier requires a valid data passband at the output sample rate");
    const auto bandwidth=band.waveform_bandwidth_hz+band.gate_bandwidth_hz;
    const auto lower=band.carrier_hz-bandwidth/2,upper=band.carrier_hz+bandwidth/2;
    if(!std::isfinite(bandwidth)||!std::isfinite(lower)||!std::isfinite(upper)||upper<=0)
        throw Error("invalid Helper Carrier gate-modulated data passband");
    // For a positive nominal carrier the positive and negative real images
    // overlap when the gate skirt crosses DC. Their positive union is one
    // interval [0,max(abs(edges))], never a negative capture frequency.
    return {std::max(0.,lower),std::max(std::abs(lower),std::abs(upper))};
}
Plan plan(const Options& options,double carrier,double bandwidth,std::uint32_t rate,
          double minimum_high_frequency_hz,double data_amplitude_scale,const PowerReference* reference) {
    return plan(options,PlacementBand{carrier,bandwidth,0},rate,
        minimum_high_frequency_hz,data_amplitude_scale,reference);
}
Plan plan(const Options& options,const PlacementBand& band,std::uint32_t rate,
          double minimum_high_frequency_hz,double data_amplitude_scale,const PowerReference* reference) {
    validate(options);
    if(options.power_w==0||effective_helper_power(options)==0)return {};
    if(!std::isfinite(minimum_high_frequency_hz)||minimum_high_frequency_hz<0)
        throw Error("invalid Helper Carrier minimum High frequency");
    if(!std::isfinite(data_amplitude_scale)||data_amplitude_scale<=0||data_amplitude_scale>1)
        throw Error("invalid Helper Carrier data amplitude reference");
    const auto passband=mono_passband(band);
    const auto carrier=band.carrier_hz;
    const auto bandwidth=band.waveform_bandwidth_hz+band.gate_bandwidth_hz;
    if(!rate||passband.upper_hz>=rate/2.)
        throw Error("Helper Carrier requires a valid data passband at the output sample rate");
    const bool folded=carrier-bandwidth/2<0;
    const auto leakage_width=folded?passband.upper_hz-passband.lower_hz:bandwidth;
    const auto effective=effective_helper_power(options);
    const auto power_ratio=effective/options.data_power_w;
    const auto helper_mean=mean_power(options,band.pilot_dwell_gating,reference);
    const auto mean=helper_mean.total;
    // Omitting auxiliary average energy is conservative and keeps placement
    // valid across clear/encrypted metadata changes. Gain uses the full mean.
    const auto guard_mean=helper_mean.high_pilot+helper_mean.low_pilot;
    // The periodic leakage certificate averages transitions over 400ms. A
    // finite helper aperture shorter than that contains proportionately more
    // start/end transition energy. Its actual duration is independent of the
    // active Robust reference (which excludes helper-only lead/tail).
    double finite_transition_factor=1;
    if(reference&&reference->helper_start_seconds&&reference->helper_end_seconds) {
        const auto duration=*reference->helper_end_seconds-*reference->helper_start_seconds;
        if(!std::isfinite(duration)||duration<=0)throw Error("invalid Helper Carrier finite power envelope");
        finite_transition_factor=static_cast<double>(std::max(1.L,dwell/duration));
    }
    const auto low_distance=passband.lower_hz>auxiliary_frequency_hz?
        passband.lower_hz-auxiliary_frequency_hz:low_pilot_frequency_hz-passband.upper_hz;
    if(low_distance<450)
        throw Error("Helper Carrier fixed 550/950 Hz tones overlap the data separation guard; raise the Robust carrier");
    const auto low_leakage=[&] {
        const auto distance_to=[&](double f) {return f<passband.lower_hz?passband.lower_hz-f:f-passband.upper_hz;};
        const auto low=std::sqrt(low_pilot_power_fraction*leakage_bound(distance_to(low_pilot_frequency_hz),leakage_width,0));
        const auto aux=std::sqrt(auxiliary_power_fraction*leakage_bound(distance_to(auxiliary_frequency_hz),leakage_width,22));
        return low+aux;
    }();
    if(low_leakage*low_leakage*power_ratio/guard_mean*finite_transition_factor>1e-3)
        throw Error("Helper Carrier fixed low tones require more data separation at this power ratio; raise the Robust carrier");
    auto frequency=std::max({2400.,minimum_high_frequency_hz,passband.upper_hz+450});
    const auto maximum_frequency=rate/2.-250;
    const auto leakage=[&] {
        const auto high=std::sqrt(pilot_power_fraction*leakage_bound(frequency-passband.upper_hz,leakage_width,0));
        return (low_leakage+high)*(low_leakage+high)*power_ratio/guard_mean*finite_transition_factor;
    };
    while(frequency<=maximum_frequency&&leakage()>1e-3)frequency+=50;
    if(frequency>maximum_frequency)
        throw Error("Helper Carrier cannot fit its power-dependent leakage guard at this output sample rate");
    const bool actual_data=reference&&reference->data_mean_power;
    const auto [relative_peak,data_gain]=gains_from_ratio(power_ratio,mean,
        actual_data?*reference->data_mean_power:nominal_data_mean_power);
    // A measured source mean already contains its DSSS/backoff factor.
    const auto source_scale=actual_data?1.:data_amplitude_scale;
    return {frequency,relative_peak*data_gain*source_scale,data_gain,effective,
        auxiliary_frequency_hz,low_pilot_frequency_hz,band.pilot_dwell_gating};
}
std::uint32_t minimum_output_rate(const Options& options,double carrier,double bandwidth,
                                  std::uint32_t logical_rate) {
    return minimum_output_rate(options,PlacementBand{carrier,bandwidth,0},logical_rate);
}
std::uint32_t minimum_output_rate(const Options& options,const PlacementBand& band,
                                  std::uint32_t logical_rate) {
    if(options.power_w==0||effective_helper_power(options)==0)return 0;
    if(logical_rate<64||logical_rate>120000000)
        throw Error("invalid Helper Carrier logical sample rate");
    // Hardware and modem clocks are independent. A larger logical clock is
    // not a reason to move an otherwise adjacent helper out of the audio band.
    const auto placement=plan(options,band,120000000);
    const auto required=std::ceil(2*(placement.frequency_hz+250));
    if(required>120000000)throw Error("Helper Carrier exceeds the physical audio sample-rate limit");
    return static_cast<std::uint32_t>(std::max({48000.,required,static_cast<double>(logical_rate)}));
}
long double protected_endpoint(std::uint64_t epoch,long double start,long double end) {
    if(!std::isfinite(start)||!std::isfinite(end)||end<start)
        throw Error("invalid Helper Carrier protected endpoint");
    auto local=std::fmod(static_cast<long double>(epoch%2)+end,2.L);
    if(local<0)local+=2;
    const auto roundoff=calendar_roundoff(local);
    local-=std::floor(local/dwell)*dwell;
    // Bound the normalized comparisons rather than subtracting an absolute
    // dwell origin from a possibly large source clock. At the exact final
    // guard boundary no extension is needed and the endpoint stays bit exact.
    if(local+roundoff>=punctuation_begin && local+roundoff<punctuation_protected_end &&
       (start-end)+local<=punctuation_latest_start+roundoff)
        return std::max(end,end+(punctuation_protected_end-local));
    return end;
}
std::complex<double> apsk_point(std::uint32_t symbol) {
    if(symbol>=(1U<<14))throw Error("invalid APSK-16384 symbol");
    const auto radius=.2+.8*static_cast<double>(symbol>>7)/127;
    return std::polar(radius,tau*static_cast<double>(symbol&127U)/128);
}
Generator::Generator(Options options,Plan plan,std::uint64_t epoch,bool punctuate,std::optional<Crypto> time_key)
    :options_(std::move(options)),plan_(plan),epoch_(epoch),punctuate_(punctuate),time_key_(std::move(time_key)) {
    validate(options_);
    if(!std::isfinite(plan_.frequency_hz)||!std::isfinite(plan_.auxiliary_frequency_hz)||
       !std::isfinite(plan_.low_frequency_hz)||plan_.low_frequency_hz<0||
       (plan_.low_frequency_hz>0&&plan_.low_frequency_hz>=plan_.frequency_hz)||
       plan_.auxiliary_frequency_hz<0||
       (plan_.auxiliary_frequency_hz>0&&plan_.auxiliary_frequency_hz>=plan_.frequency_hz)||
       !std::isfinite(plan_.helper_gain)||
       !std::isfinite(plan_.data_gain)||plan_.helper_gain<0||plan_.data_gain<0||
       plan_.helper_gain*(pilot_amplitude_fraction+
            (plan_.low_frequency_hz>0?low_pilot_amplitude_fraction:0)+
            (plan_.auxiliary_frequency_hz>0?auxiliary_amplitude_fraction:0))+plan_.data_gain>1.0000000001)
        throw Error("invalid Helper Carrier mixer plan");
    if(time_key_ && (options_.timecode || !options_.callsign.empty()) && plan_.helper_gain>0) {
        // Fail before emitting samples when the keyed format is unavailable.
        (void)metadata_packet(0,options_.callsign,&*time_key_);
    }
}
std::complex<double> Generator::mix_analytic(std::complex<double> payload,long double seconds,
                                          std::uint32_t rate,std::optional<long double> end_seconds,
                                          std::optional<long double> start_seconds) const {
    if(plan_.helper_gain==0)return payload;
    if(!rate||!std::isfinite(seconds)||plan_.frequency_hz<=0||plan_.frequency_hz>=rate/2.)
        throw Error("invalid Helper Carrier physical output coordinate");
    // epoch modulo two preserves the rational 2/5-second dwell grid without
    // adding a large Unix epoch to each fractional sample coordinate.
    auto local=std::fmod(static_cast<long double>(epoch_%2)+seconds,2.L);
    if(local<0)local+=2;
    const auto roundoff=calendar_roundoff(local);
    local-=std::floor(local/dwell)*dwell;
    auto envelope=plan_.pilot_dwell_gating?smooth(std::min(local,dwell-local)/ramp):1.;
    // The auxiliary's periodic boundary gate has twice the pilot's support.
    // Explicit source start/end ramps still occupy4ms on both components.
    auto auxiliary_envelope=smooth(std::min(local,dwell-local)/.008L);
    if(end_seconds) {
        if(!std::isfinite(*end_seconds))throw Error("invalid Helper Carrier final output coordinate");
        envelope*=smooth((*end_seconds-seconds)/ramp);
        auxiliary_envelope*=smooth((*end_seconds-seconds)/ramp);
    }
    if(start_seconds) {
        if(!std::isfinite(*start_seconds))throw Error("invalid Helper Carrier initial output coordinate");
        envelope*=smooth((seconds-*start_seconds)/ramp);
        auxiliary_envelope*=smooth((seconds-*start_seconds)/ramp);
    }
    // Only the auxiliary crosses zero at160ms. Its two independent8ms C3
    // tapers reverse phase smoothly. Metadata starts at212ms in that inverted
    // reference and its last value is held through the dwell boundary taper.
    // The zero-amplitude boundary permits a phase reset; pilot phase is untouched.
    const bool punctuation_fits=punctuate_ &&
        (!start_seconds || (*start_seconds-seconds)+local<=punctuation_latest_start+roundoff) &&
        (!end_seconds || (*end_seconds-seconds)+local+roundoff>=punctuation_protected_end);
    // Cache the complete record at its earliest transition, once per dwell.
    if((options_.timecode||!options_.callsign.empty())&&local>=metadata_end-9*symbol_time) {
        const auto absolute_dwell=static_cast<long double>(epoch_)+seconds-local;
        if(absolute_dwell>=0 && absolute_dwell<4294967296.L) {
            const auto ticks=static_cast<std::uint64_t>(std::round(absolute_dwell*65536));
            if(ticks>=(std::uint64_t{1}<<48)) {symbol_count_=0;cached_time_.reset();}
            else if(cached_time_!=ticks) {
                const auto packet=metadata_packet(ticks,options_.callsign,time_key_?&*time_key_:nullptr);
                symbol_count_=packet.count;
                for(std::size_t i=0;i<symbol_count_;++i)symbols_[i]=apsk_point(packet.symbols[i]);
                cached_time_=ticks;
            }
        } else {symbol_count_=0;cached_time_.reset();}
    }
    const auto modulation=auxiliary_modulation(local,std::span(symbols_).first(symbol_count_),punctuation_fits);
    const auto cycles=std::fmod(static_cast<long double>(plan_.frequency_hz)*seconds,1.L);
    const auto carrier=std::polar(1.,tau*static_cast<double>(cycles));
    auto helper=plan_.helper_gain*envelope*(pilot_amplitude_fraction*carrier);
    if(plan_.low_frequency_hz>0) {
        const auto low_cycles=std::fmod(static_cast<long double>(plan_.low_frequency_hz)*seconds,1.L);
        helper+=plan_.helper_gain*envelope*low_pilot_amplitude_fraction*
            std::polar(1.,tau*static_cast<double>(low_cycles));
    }
    if(plan_.auxiliary_frequency_hz>0) {
        const auto auxiliary_cycles=std::fmod(static_cast<long double>(plan_.auxiliary_frequency_hz)*seconds,1.L);
        helper+=plan_.helper_gain*auxiliary_envelope*
            (auxiliary_amplitude_fraction*modulation*std::polar(1.,tau*static_cast<double>(auxiliary_cycles)));
    }
    return payload*plan_.data_gain+helper;
}
float Generator::mix(float payload,long double seconds,std::uint32_t rate,
                     std::optional<long double> end_seconds,std::optional<long double> start_seconds) const {
    if(plan_.helper_gain==0)return payload;
    return static_cast<float>(mix_analytic({payload,0},seconds,rate,end_seconds,start_seconds).real());
}

} // namespace datapump::helper
