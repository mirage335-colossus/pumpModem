#include "datapump/helper_carrier.hpp"
#include "datapump/fhss_schedule.hpp"
#include "datapump/modem.hpp"
#include "datapump/pattern_receiver.hpp"
#include "datapump/types.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace datapump::helper {
namespace {
constexpr double tau=2*std::numbers::pi;
constexpr long double dwell=.4L,ramp=.004L,metadata_end=.388L,symbol_time=.008L;
constexpr long double punctuation_begin=.236L,punctuation_return=.276L,punctuation_transition=.008L;
// Literal rational-calendar endpoints avoid accumulated rounding at an exact source sample.
constexpr long double punctuation_latest_start=.212L,punctuation_protected_end=.308L;
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
void validate_callsign(std::string_view callsign) {
    if(callsign.size()>19)throw Error("Helper Carrier callsign must contain at most 19 seven-bit ASCII characters");
    for(const char c:callsign)
        if(static_cast<unsigned char>(c)>127)throw Error("Helper Carrier callsign must be seven-bit ASCII");
}
double effective_power(const Options& options) {
    return std::min(options.power_w,
        options.data_power_w*std::pow(10.,options.ratio_limit_db/10));
}
std::pair<double,double> gains_from_ratio(double power_ratio) {
    const auto relative_peak=std::sqrt(2*modem::nominal_signal_power)*
        std::sqrt(power_ratio);
    return {relative_peak,.95/(1+relative_peak)};
}
// Bound the periodic complex envelope's energy in either data sideband. Each
// transition is a quintic smootherstep, whose derivative is Beta(3,3). Its
// transform has magnitude <=15*(3+z^2+3z)/z^5, z=pi*f*T, and <=1.
// The envelope has two unit 4ms ramps, two magnitude-two 8ms reversals, and
// <=ten magnitude-two metadata transitions. Integration by parts supplies the
// additional 1/(2*pi*f*dwell). Coefficients therefore fall at least as f^-4;
// sum their squared bound over the 2.5Hz dwell harmonics. Factor four includes
// the real carrier's other image and possible constructive addition. This is
// a digital periodic-waveform bound, not an RF/DAC distortion certificate.
double leakage_bound(double distance,double bandwidth,bool metadata) {
    const auto derivative_transform=[&](double duration) {
        const auto z=std::numbers::pi*distance*duration;
        return std::min(1.,15*(3+z*z+3*z)/std::pow(z,5));
    };
    const auto coefficient=(2*derivative_transform(.004)+
        (metadata?24:4)*derivative_transform(.008))/(tau*distance*.4);
    const auto harmonics=1+std::min(std::ceil(bandwidth/2.5),distance/(7*2.5));
    return 4*coefficient*coefficient*harmonics;
}
long double aligned_duration(long double seconds,long double rate) {
    auto count=seconds*2.5L;
    const auto nearest=std::round(count);
    // Input durations are binary64. Do not add a whole dwell because promoting
    // an intended integral count (e.g. 6.4s) exposes its input rounding residue.
    const auto tolerance=std::min(4*std::numeric_limits<double>::epsilon()*std::max(1.L,std::abs(count)),
        .5L/rate*2.5L);
    if(std::abs(count-nearest)<=tolerance)
        count=nearest;
    return std::ceil(count)*dwell;
}
}
void validate(const Options& options) {
    if(!std::isfinite(options.power_w)||options.power_w<0||
       !std::isfinite(options.data_power_w)||options.data_power_w<=0||
       !std::isfinite(options.ratio_limit_db)||options.ratio_limit_db<0||options.ratio_limit_db>90)
        throw Error("invalid Helper Carrier power or ratio limit");
    switch(options.frequency) {
        case Frequency::high:case Frequency::low:case Frequency::cochannel:break;
        default:throw Error("invalid Helper Carrier frequency placement");
    }
    validate_callsign(options.callsign);
}
double calibrated_data_gain(const Options& options) {
    validate(options);
    if(options.power_w==0)return 1;
    return gains_from_ratio(effective_power(options)/options.data_power_w).second;
}
ReferenceLevel reference_level(const Options& options,double nominal,double scale,double payload_fraction,double usable_fraction) {
    validate(options);
    if(options.power_w<=0||!std::isfinite(nominal)||
       !std::isfinite(scale)||scale<=0||scale>1||
       !std::isfinite(payload_fraction)||payload_fraction<=0||payload_fraction>1||
       !std::isfinite(usable_fraction)||usable_fraction<=0||usable_fraction>1)
        throw Error("invalid Helper Carrier reference-level inputs");
    const auto ratio=std::min(static_cast<long double>(options.power_w)/options.data_power_w,
        std::pow(10.L,static_cast<long double>(options.ratio_limit_db)/10));
    const auto payload=static_cast<long double>(nominal)+20*std::log10(static_cast<long double>(scale));
    const auto straight=payload+10*std::log10(ratio);
    const auto usable=straight+10*std::log10(static_cast<long double>(usable_fraction));
    const auto average=payload+10*std::log10(static_cast<long double>(payload_fraction));
    ReferenceLevel result{static_cast<double>(ratio),static_cast<double>(straight),
        static_cast<double>(usable),static_cast<double>(average)};
    if(!std::isfinite(result.effective_power_ratio)||result.effective_power_ratio<=0||
       !std::isfinite(result.straight_cn0_db_hz)||!std::isfinite(result.usable_cn0_db_hz)||
       !std::isfinite(result.payload_average_cn0_db_hz))
        throw Error("Helper Carrier reference level exceeds the numerical range");
    return result;
}
EnvelopePlan plan_envelope(const modem::Config& config,const EnvelopeRequirements& requirements) {
    modem::validate(config);
    for(const auto duration:{requirements.frequency_acquisition_seconds,requirements.frontend_lookahead_seconds,
                             requirements.receiver_buffer_deficit_seconds})
        if(!std::isfinite(duration)||duration<0)throw Error("invalid Helper Carrier envelope duration");
    if(!std::isfinite(requirements.frequency_precision_extension_seconds)||
       requirements.frequency_precision_extension_seconds<0||requirements.frequency_precision_extension_seconds>36)
        throw Error("invalid Helper Carrier precision lead extension");
    if(!std::isfinite(requirements.maximum_source_capture_ratio)||requirements.maximum_source_capture_ratio<=0)
        throw Error("invalid Helper Carrier envelope clock ratio");
    const auto ratio=std::max(1.L,static_cast<long double>(requirements.maximum_source_capture_ratio));
    const auto rate=static_cast<long double>(config.sample_rate);
    const auto support=2.L*modem::pattern_pulse_padding_samples(config)/rate;
    const auto ordinary_tail=static_cast<long double>(modem::pattern_absence_capture_samples(config))/rate+1+support;
    const auto lookahead=static_cast<long double>(requirements.frontend_lookahead_seconds);
    EnvelopePlan result;
    result.lead_seconds=aligned_duration(ratio*(std::max(2.L,
        static_cast<long double>(requirements.frequency_acquisition_seconds))+lookahead),rate);
    auto precision_extension=aligned_duration(
        static_cast<long double>(requirements.frequency_precision_extension_seconds),rate);
    // 36 seconds is exactly 90 dwells. Clamp only input-rounding residue;
    // validated additional airtime may never exceed that independent cap.
    precision_extension=std::min(36.L,precision_extension);
    result.lead_seconds+=precision_extension;
    result.tail_seconds=aligned_duration(ratio*(std::max({2.L,ordinary_tail,
        static_cast<long double>(requirements.receiver_buffer_deficit_seconds)})+lookahead),rate);
    result.preparation_seconds=result.lead_seconds+dwell+1/rate;
    result.maximum_stop_extension_seconds=result.lead_seconds+result.tail_seconds+2*dwell+2/rate;
    // The source wrapper still checks its full endpoint. Reject a plan whose
    // independent extension alone cannot fit the existing 64-bit source clock.
    if(!std::isfinite(result.maximum_stop_extension_seconds)||
       std::ceil(result.maximum_stop_extension_seconds*rate)>=std::ldexp(1.L,64))
        throw Error("Helper Carrier envelope exceeds the source sample counter");
    return result;
}
PlacementBand placement_band(const modem::Config& config,double minimum_bandwidth) {
    modem::validate(config);
    if(!std::isfinite(minimum_bandwidth)||minimum_bandwidth<0)
        throw Error("invalid Helper Carrier minimum occupied data bandwidth");
    if(config.fake_hopping) {
        // Helper geometry must not turn an unavailable mandatory gate into
        // ungated payload. This is the same public shape used by the source.
        (void)modem::FhssPowerShape(*config.fake_hopping,config.sample_rate,
            modem::pattern_pulse_padding_samples(config),modem::symbol_sample_count(config));
    }
    const auto waveform=modem::pattern_pulse_enabled(config)?
        (1+modem::pattern_pulse_rolloff)*config.sample_rate/modem::pattern_chip_samples(config):
        modem::waveform_bandwidth_hz(config);
    const auto full=std::max(minimum_bandwidth,waveform+modem::waveform_gate_bandwidth_allowance_hz(config));
    PlacementBand band{config.carrier_hz,waveform,full-waveform};
    (void)mono_passband(band);
    return band;
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
          double minimum_high_frequency_hz,double data_amplitude_scale) {
    return plan(options,PlacementBand{carrier,bandwidth,0},rate,
        minimum_high_frequency_hz,data_amplitude_scale);
}
Plan plan(const Options& options,const PlacementBand& band,std::uint32_t rate,
          double minimum_high_frequency_hz,double data_amplitude_scale) {
    validate(options);
    if(options.power_w==0)return {};
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
    if(folded&&options.frequency==Frequency::low)
        throw Error("Helper Carrier Low has no isolated passband below gate-modulated data reaching DC");
    const auto leakage_width=folded?passband.upper_hz-passband.lower_hz:bandwidth;
    const double direction=options.frequency==Frequency::high?1:-1;
    const auto effective=effective_power(options);
    const auto power_ratio=effective/options.data_power_w;
    // Begin with a 300Hz nominal metadata band and a 300Hz guard. Increase
    // separation until the conservative digital leakage bound is at least
    // 30dB below data power. The data waveform and passband are never changed.
    double distance=450;
    if(minimum_high_frequency_hz>0&&options.frequency==Frequency::high)
        distance=std::max(distance,50*std::ceil((minimum_high_frequency_hz-carrier-bandwidth/2)/50));
    const auto maximum_distance=folded?
        (direction>0?rate/2.-250-passband.upper_hz:passband.lower_hz-250):
        (direction>0?rate/2.-250-carrier-bandwidth/2:carrier-bandwidth/2-250);
    while(options.frequency!=Frequency::cochannel&&distance<=maximum_distance&&
          leakage_bound(distance,leakage_width,true)*power_ratio>1e-3)
        distance+=50;
    if(options.frequency!=Frequency::cochannel&&distance>maximum_distance)
        throw Error("Helper Carrier cannot fit its power-dependent leakage guard at this output sample rate");
    // Cochannel is an intentional overlapping diagnostic configuration. Keep
    // the actual linear mixture; it provides no isolation from payload data.
    const double frequency=options.frequency==Frequency::cochannel?carrier:
        folded?passband.upper_hz+distance:carrier+direction*(bandwidth/2+distance);
    const auto [relative_peak,data_gain]=gains_from_ratio(power_ratio);
    // Ordinary PCM has the unchanged nominal mean power and peak bound one.
    // Leave 5% composite headroom, with no nonlinear limiter or data power boost.
    // Match the unchanged payload's digital backoff. Keep its conservative
    // gain identical; only the helper yields amplitude/headroom here.
    return {frequency,relative_peak*data_gain*data_amplitude_scale,data_gain,effective};
}
std::uint32_t minimum_output_rate(const Options& options,double carrier,double bandwidth,
                                  std::uint32_t logical_rate) {
    return minimum_output_rate(options,PlacementBand{carrier,bandwidth,0},logical_rate);
}
std::uint32_t minimum_output_rate(const Options& options,const PlacementBand& band,
                                  std::uint32_t logical_rate) {
    if(options.power_w==0)return 0;
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
    if(symbol>=(1U<<21))throw Error("invalid APSK-2097152 symbol");
    const auto radius=.9+.1*static_cast<double>(symbol>>11)/1023;
    return std::polar(radius,tau*static_cast<double>(symbol&2047U)/2048);
}
Generator::Generator(Options options,Plan plan,std::uint64_t epoch,bool punctuate,std::optional<Crypto> time_key)
    :options_(std::move(options)),plan_(plan),epoch_(epoch),punctuate_(punctuate),time_key_(std::move(time_key)) {
    // Encryption never exposes a stored public callsign, including through
    // validation or a failed-cipher fallback. Keep the caller's settings intact.
    if(time_key_)options_.callsign.clear();
    validate(options_);
    if(!std::isfinite(plan_.frequency_hz)||!std::isfinite(plan_.helper_gain)||
       !std::isfinite(plan_.data_gain)||plan_.helper_gain<0||plan_.data_gain<0||
       plan_.helper_gain+plan_.data_gain>1.0000000001)
        throw Error("invalid Helper Carrier mixer plan");
    if(!options_.callsign.empty()) {
        const auto packet=callsign_packet(options_.callsign);
        for(std::size_t i=0;i<packet.count;++i)symbols_[i]=apsk_point(packet.symbols[i]);
        symbol_count_=packet.count;
    } else if(options_.timecode && plan_.helper_gain>0) {
        // Validate cipher availability before the first PCM. Real
        // dwell timestamps are cached below, never encrypted once per sample.
        (void)timecode_packet(0,time_key_?&*time_key_:nullptr);
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
    auto envelope=smooth(std::min(local,dwell-local)/ramp);
    if(end_seconds) {
        if(!std::isfinite(*end_seconds))throw Error("invalid Helper Carrier final output coordinate");
        envelope*=smooth((*end_seconds-seconds)/ramp);
    }
    if(start_seconds) {
        if(!std::isfinite(*start_seconds))throw Error("invalid Helper Carrier initial output coordinate");
        envelope*=smooth((seconds-*start_seconds)/ramp);
    }
    // Paired8ms C2 reversals cross zero at240/280ms. The32ms inverted
    // plateau also preserves the existing marker fit's32ms post-flip support.
    // The original running carrier returns unchanged before metadata; neither
    // reversal resets its phase. Both constant flanks remain at least20ms.
    const bool punctuation_fits=punctuate_ &&
        (!start_seconds || (*start_seconds-seconds)+local<=punctuation_latest_start+roundoff) &&
        (!end_seconds || (*end_seconds-seconds)+local+roundoff>=punctuation_protected_end);
    const auto reversal=punctuation_fits?
        1-2*smooth((local-punctuation_begin)/punctuation_transition)+
          2*smooth((local-punctuation_return)/punctuation_transition):1.;
    const std::complex<double> punctuation{reversal,0};
    std::complex<double> modulation=punctuation;
    // A time frame has at most three symbols. Cache it before its earliest
    // possible start, then use its actual compact length. No crypto per sample.
    if(options_.callsign.empty()&&options_.timecode&&local>=metadata_end-3*symbol_time) {
        const auto absolute_dwell=static_cast<long double>(epoch_)+seconds-local;
        if(absolute_dwell>=0 && absolute_dwell<4294967296.L) {
            const auto ticks=static_cast<std::uint64_t>(std::round(absolute_dwell*65536));
            if(ticks>=(std::uint64_t{1}<<48)) {symbol_count_=0;cached_time_.reset();}
            else if(cached_time_!=ticks) {
                const auto packet=timecode_packet(ticks,time_key_?&*time_key_:nullptr);
                symbol_count_=packet.count;
                for(std::size_t i=0;i<symbol_count_;++i)symbols_[i]=apsk_point(packet.symbols[i]);
                cached_time_=ticks;
            }
        } else {symbol_count_=0;cached_time_.reset();}
    }
    const auto metadata_begin=metadata_end-symbol_count_*symbol_time;
    if(symbol_count_ && local>=metadata_begin) {
        const auto position=(local-metadata_begin)/symbol_time;
        const auto index=static_cast<std::size_t>(std::floor(position));
        if(index<symbol_count_) {
            const auto previous=index?symbols_[index-1]:std::complex<double>{1,0};
            const auto blend=smooth(position-index);
            modulation=punctuation*(previous+(symbols_[index]-previous)*blend);
        } else if(symbol_count_ && index==symbol_count_) {
            const auto blend=smooth(position-index);
            const auto last=symbols_[symbol_count_-1];
            modulation=punctuation*(last+(std::complex<double>{1,0}-last)*blend);
        }
    }
    const auto cycles=std::fmod(static_cast<long double>(plan_.frequency_hz)*seconds,1.L);
    const auto carrier=std::polar(1.,tau*static_cast<double>(cycles));
    return payload*plan_.data_gain+plan_.helper_gain*envelope*modulation*carrier;
}
float Generator::mix(float payload,long double seconds,std::uint32_t rate,
                     std::optional<long double> end_seconds,std::optional<long double> start_seconds) const {
    if(plan_.helper_gain==0)return payload;
    return static_cast<float>(mix_analytic({payload,0},seconds,rate,end_seconds,start_seconds).real());
}

Tracker::Tracker(double expected,double width):expected_(expected),half_width_(width) {
    if(!std::isfinite(expected)||expected<=0||!std::isfinite(width)||width<0||width>100)
        throw Error("invalid Helper Carrier tracking band");
}
void Tracker::reset() {
    rate_=0;window_size_=window_count_=group_windows_=0;groups_=0;
    admitted_=false;present_seconds_=absent_seconds_=energy_=0;bins_.clear();update_.reset();
}
void Tracker::configure(std::uint32_t rate) {
    reset();
    if(rate<1000||expected_-half_width_<10||expected_+half_width_>=rate/2.-10)
        throw Error("Helper Carrier tracking band is outside capture passband");
    rate_=rate;window_size_=rate/10;
    const auto cells=static_cast<unsigned>(std::ceil(half_width_/5));
    const auto spacing=cells?half_width_/cells:0;
    bins_.resize(2*cells+1);
    if(sizeof(*this)+bins_.capacity()*sizeof(Bin)>workspace_bound) {
        std::vector<Bin>{}.swap(bins_);rate_=0;
        throw Error("Helper Carrier tracker exceeds its bounded workspace");
    }
    for(std::size_t i=0;i<bins_.size();++i) {
        auto& bin=bins_[i];
        bin.frequency=expected_+(static_cast<double>(i)-cells)*spacing;
        const auto omega=tau*bin.frequency/rate;
        bin.step=std::polar(1.,omega);
        // Exact finite-window Gram matrix, including noninteger tone periods.
        const auto count=static_cast<double>(window_size_);
        const auto geometric=(std::sin(count*omega)/std::sin(omega))*
            std::polar(1.,(count-1)*omega);
        bin.cc=(count+geometric.real())/2;
        bin.ss=(count-geometric.real())/2;
        bin.cs=geometric.imag()/2;
    }
}
void Tracker::push(std::span<const float> samples,std::uint32_t rate) {
    if(rate_!=rate)configure(rate);
    for(const auto sample:samples) {
        if(!std::isfinite(sample)) {reset();return;}
        energy_+=static_cast<double>(sample)*sample;
        for(auto& bin:bins_) {
            bin.sum+=static_cast<double>(sample)*bin.oscillator;
            bin.oscillator*=bin.step;
        }
        if(++window_count_==window_size_)finish_window();
    }
}
void Tracker::finish_window() {
    const auto degrees=static_cast<double>(window_size_-2);
    for(auto& bin:bins_) {
        const auto c=bin.sum.real(),s=bin.sum.imag();
        const auto determinant=bin.cc*bin.ss-bin.cs*bin.cs;
        const auto explained=std::clamp((bin.ss*c*c+bin.cc*s*s-2*bin.cs*c*s)/determinant,0.,energy_);
        const auto residual=std::max(energy_-explained,std::numeric_limits<double>::min());
        // Under finite independent normal noise this has F(2,N-2) survival.
        const auto f=energy_>0?std::min(1e100,explained*.5/(residual/degrees)):0;
        const auto logp=-degrees*.5*std::log1p(2*f/degrees);
        bin.log_evidence-=logp;
        bin.snr_sum+=std::max(0.,f-1);
        bin.power+=explained;
        bin.sum={};bin.oscillator={1,0};
    }
    window_count_=0;energy_=0;
    const auto target_windows=std::min<std::size_t>(96,4+static_cast<std::size_t>(
        std::ceil(2*std::log1p(static_cast<double>(groups_)))));
    if(++group_windows_<target_windows)return;
    const bool admission_budget_remaining=groups_!=std::numeric_limits<std::uint64_t>::max();
    if(admission_budget_remaining)++groups_;
    // Fisher combination of K independent finite-window p values has
    // integer-gamma survival. K is deterministic before observing this group.
    // Union bound covers all bins; alpha spending
    // 1/(g(g+1)) covers every look, including a beacon-free infinite stream.
    const auto log_alpha=std::log(.01)-std::log(static_cast<double>(bins_.size()))-
        std::log(static_cast<double>(groups_))-std::log(static_cast<double>(groups_)+1);
    std::size_t best=0;
    for(std::size_t i=1;i<bins_.size();++i)
        if(bins_[i].log_evidence>bins_[best].log_evidence)best=i;
    const auto evidence=bins_[best].log_evidence;
    double log_sum=0,log_term=0;
    if(evidence>0)for(std::size_t j=1;j<target_windows;++j) {
        log_term+=std::log(evidence)-std::log(static_cast<double>(j));
        const auto high=std::max(log_sum,log_term),low=std::min(log_sum,log_term);
        log_sum=high+std::log1p(std::exp(low-high));
    }
    const auto log_probability=evidence>0?-evidence+log_sum:0;
    const auto snr=10*std::log10(std::max(1e-100,
        bins_[best].snr_sum/static_cast<double>(target_windows)));
    if(!admitted_&&snr>=6&&admission_budget_remaining&&log_probability<=log_alpha)
        admitted_=true;
    const bool present=admitted_&&snr>=6;
    const auto elapsed=static_cast<double>(target_windows*window_size_)/rate_;
    if(present) {present_seconds_+=elapsed;absent_seconds_=0;}
    else {present_seconds_=0;absent_seconds_+=elapsed;}
    if(absent_seconds_+1e-9>=6)admitted_=false;
    auto frequency=bins_[best].frequency;
    if(best>0&&best+1<bins_.size()) {
        const auto left=bins_[best-1].power,center=bins_[best].power,right=bins_[best+1].power;
        const auto curvature=left-2*center+right;
        if(curvature<0)frequency+=std::clamp(.5*(left-right)/curvature,-.5,.5)*
            (bins_[best+1].frequency-frequency);
    }
    update_=Observation{present,present&&present_seconds_+1e-9>=6,frequency,snr,present_seconds_,absent_seconds_};
    for(auto& bin:bins_) {bin.log_evidence=bin.snr_sum=bin.power=0;}
    group_windows_=0;
}
std::optional<Observation> Tracker::take_update() {
    return std::exchange(update_,std::nullopt);
}
}
