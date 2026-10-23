#include "datapump/helper_carrier.hpp"
#include "datapump/modem.hpp"
#include "datapump/types.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace datapump::helper {
namespace {
constexpr double tau=2*std::numbers::pi;
constexpr long double dwell=.4L,ramp=.004L,metadata_begin=.324L,symbol_time=.008L;
double smooth(long double x) {
    const auto t=static_cast<double>(std::clamp(x,0.L,1.L));
    return t*t*t*(10+t*(-15+6*t));
}
void validate_callsign(std::string_view callsign) {
    if(callsign.size()>19)throw Error("Helper Carrier callsign must contain at most 19 seven-bit ASCII characters");
    for(const char c:callsign)
        if(static_cast<unsigned char>(c)>127)throw Error("Helper Carrier callsign must be seven-bit ASCII");
}
// Bound the periodic complex envelope's energy in either data sideband. Each
// transition is a quintic smootherstep, whose derivative is Beta(3,3). Its
// transform has magnitude <=15*(3+z^2+3z)/z^5, z=pi*f*T, and <=1.
// The envelope has two unit 4ms ramps, one magnitude-two 8ms reversal, and
// <=nine magnitude-two metadata transitions. Integration by parts supplies the
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
        (metadata?20:2)*derivative_transform(.008))/(tau*distance*.4);
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
        case Frequency::high:case Frequency::low:case Frequency::cochannel:break;
        default:throw Error("invalid Helper Carrier frequency placement");
    }
    validate_callsign(options.callsign);
}
Plan plan(const Options& options,double carrier,double bandwidth,std::uint32_t rate,
          double minimum_high_frequency_hz) {
    validate(options);
    if(options.power_w==0)return {};
    if(!std::isfinite(minimum_high_frequency_hz)||minimum_high_frequency_hz<0)
        throw Error("invalid Helper Carrier minimum High frequency");
    if(minimum_high_frequency_hz>0&&options.frequency!=Frequency::high)
        throw Error("Helper Carrier alongside data requires High placement outside its capture passband");
    if(!rate||!std::isfinite(carrier)||!std::isfinite(bandwidth)||bandwidth<=0||
       carrier-bandwidth/2<0||carrier+bandwidth/2>=rate/2.)
        throw Error("Helper Carrier requires a valid data passband at the output sample rate");
    if(options.frequency==Frequency::cochannel)
        throw Error("Helper Carrier 0 Hz offset is unavailable: co-channel interference is not qualified");
    const double direction=options.frequency==Frequency::high?1:-1;
    const auto effective=std::min(options.power_w,
        options.data_power_w*std::pow(10.,options.ratio_limit_db/10));
    const auto power_ratio=effective/options.data_power_w;
    // Begin with a 300Hz nominal metadata band and a 300Hz guard. Increase
    // separation until the conservative digital leakage bound is at least
    // 30dB below data power. The data waveform and passband are never changed.
    double distance=450;
    if(minimum_high_frequency_hz>0)
        distance=std::max(distance,50*std::ceil((minimum_high_frequency_hz-carrier-bandwidth/2)/50));
    const auto maximum_distance=direction>0?rate/2.-250-carrier-bandwidth/2:
                                           carrier-bandwidth/2-250;
    while(distance<=maximum_distance&&
          leakage_bound(distance,bandwidth,true)*power_ratio>1e-3)
        distance+=50;
    if(distance>maximum_distance)
        throw Error("Helper Carrier cannot fit its power-dependent leakage guard at this output sample rate");
    const double frequency=carrier+direction*(bandwidth/2+distance);
    const auto relative_peak=std::sqrt(2*modem::nominal_signal_power)*
        std::sqrt(power_ratio);
    // Ordinary PCM has the unchanged nominal mean power and peak bound one.
    // Leave 5% composite headroom, with no nonlinear limiter or data power boost.
    const auto data_gain=.95/(1+relative_peak);
    return {frequency,relative_peak*data_gain,data_gain,effective};
}
std::array<std::uint32_t,8> metadata_symbols(std::string_view callsign) {
    validate_callsign(callsign);
    std::array<std::uint8_t,20> bytes{};
    bytes[0]=static_cast<std::uint8_t>(callsign.size());
    for(std::size_t i=0;i<callsign.size();++i)bytes[i+1]=static_cast<std::uint8_t>(callsign[i]);
    std::uint16_t crc=0xffff;
    for(const auto byte:bytes) {
        crc^=static_cast<std::uint16_t>(byte)<<8;
        for(unsigned i=0;i<8;++i)
            crc=static_cast<std::uint16_t>((crc&0x8000)?(crc<<1)^0x1021:crc<<1);
    }
    std::array<std::uint32_t,8> symbols{};
    std::size_t offset=0;
    const auto append=[&](std::uint32_t value,unsigned bits) {
        while(bits) {
            --bits;
            symbols[offset/21]|=((value>>bits)&1U)<<(20-offset%21);
            ++offset;
        }
    };
    append(bytes[0],5);
    for(std::size_t i=1;i<bytes.size();++i)append(bytes[i],7);
    append(crc,16);
    return symbols;
}
std::complex<double> apsk_point(std::uint32_t symbol) {
    if(symbol>=(1U<<21))throw Error("invalid APSK-2097152 symbol");
    const auto radius=.9+.1*static_cast<double>(symbol>>11)/1023;
    return std::polar(radius,tau*static_cast<double>(symbol&2047U)/2048);
}
Generator::Generator(Options options,Plan plan,std::uint64_t epoch,bool punctuate)
    :options_(std::move(options)),plan_(plan),epoch_(epoch),punctuate_(punctuate) {
    validate(options_);
    if(!std::isfinite(plan_.frequency_hz)||!std::isfinite(plan_.helper_gain)||
       !std::isfinite(plan_.data_gain)||plan_.helper_gain<0||plan_.data_gain<0||
       plan_.helper_gain+plan_.data_gain>1.0000000001)
        throw Error("invalid Helper Carrier mixer plan");
    if(!options_.callsign.empty()) {
        const auto words=metadata_symbols(options_.callsign);
        for(std::size_t i=0;i<words.size();++i)symbols_[i]=apsk_point(words[i]);
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
    // An 8ms C2 amplitude reversal has precisely one 180-degree phase flip at
    // its zero crossing and exactly constant amplitude/phase on either side.
    // This keeps a bounded polynomial spectrum and avoids a phase-step spur.
    const auto dwell_start=seconds-local;
    const bool punctuation_fits=punctuate_ && (!start_seconds || *start_seconds<=dwell_start+.212L) &&
        (!end_seconds || *end_seconds>=dwell_start+.268L);
    const std::complex<double> punctuation{punctuation_fits?1-2*smooth((local-.236L)/.008L):1.,0};
    std::complex<double> modulation=punctuation;
    if(!options_.callsign.empty()&&local>=metadata_begin) {
        const auto position=(local-metadata_begin)/symbol_time;
        const auto index=static_cast<std::size_t>(std::floor(position));
        if(index<symbols_.size()) {
            const auto previous=index?symbols_[index-1]:std::complex<double>{1,0};
            const auto blend=smooth(position-index);
            modulation=punctuation*(previous+(symbols_[index]-previous)*blend);
        } else if(index==symbols_.size()) {
            const auto blend=smooth(position-index);
            modulation=punctuation*(symbols_.back()+(std::complex<double>{1,0}-symbols_.back())*blend);
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
