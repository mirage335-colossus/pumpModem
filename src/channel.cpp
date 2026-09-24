#include "datapump/channel.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace datapump::modem {
namespace {
using Complex=std::complex<double>;
constexpr long double tau=2*std::numbers::pi_v<long double>;
double sinc(double value) {return std::abs(value)<1e-8?1-value*value/6:std::sin(value)/value;}
Complex phase_rotation(long double phase) {
    return std::polar(1.,static_cast<double>(std::remainder(phase,tau)));
}
std::uint64_t mix(std::uint64_t value) {
    value+=0x9e3779b97f4a7c15ULL;value=(value^(value>>30))*0xbf58476d1ce4e5b9ULL;
    value=(value^(value>>27))*0x94d049bb133111ebULL;return value^(value>>31);
}
double preview_normal(std::uint64_t seed,std::uint64_t sample) {
    const auto uniform=[](std::uint64_t bits){return (static_cast<double>(bits>>11)+.5)/9007199254740992.;};
    return std::sqrt(-2*std::log(uniform(mix(seed^sample))))*
           std::cos(static_cast<double>(tau)*uniform(mix(seed^sample^0xd1b54a32d192ed03ULL)));
}
}
struct SampledSimulationChannel::Impl {
    static constexpr std::size_t taps=16,phases=256;
    Config config;
    ChannelConfig channel;
    long double rate=1,startup=0;
    std::uint64_t received=0,origin=0,total=0,buffer_start=0;
    std::size_t buffer_count=0;
    StreamingTransmitter* source=nullptr;
    bool complete=false;
    Complex oscillator{},step{},burst_rotation{};
    double diffusion=0,sigma=0;
    std::mt19937_64 startup_random,phase_random,noise_random;
    std::normal_distribution<double> phase_normal{0,1},noise_normal{0,1};
    std::array<Complex,1024> buffer{};
    std::array<std::array<double,taps>,phases+1> coefficients{};
    Impl(Config value,ChannelConfig impairment):config(value),channel(impairment),
        startup_random(impairment.seed^0x8ebc6af09c88c6e3ULL),
        phase_random(impairment.seed^0xa0761d6478bd642fULL),noise_random(impairment.seed) {
        static_assert(sizeof(Impl)+sizeof(SampledSimulationChannel)<=SampledSimulationChannel::workspace_bound);
        validate(config);validate_channel(config,channel);
        rate=1+static_cast<long double>(channel.clock_error_ppm)*1e-6L;
        diffusion=channel.phase_noise_degrees_per_sqrt_second*std::numbers::pi/180/std::sqrt(config.sample_rate);
        sigma=std::sqrt(nominal_signal_power*std::pow(10.,-channel.snr_db/10));
        oscillator=phase_rotation(tau*(uniform()-.5L));
        step=phase_rotation(tau*channel.frequency_offset_hz/config.sample_rate);
        // Interpolated polyphase Blackman-windowed sinc coefficients avoid
        // per-tap trigonometry while retaining fractional sample boundaries.
        for(std::size_t phase=0;phase<=phases;++phase) {
            double sum=0;
            for(std::size_t tap=0;tap<taps;++tap) {
                const auto distance=static_cast<double>(phase)/phases-static_cast<double>(tap)+7;
                const auto window=.42+.5*std::cos(std::numbers::pi*distance/8)+.08*std::cos(std::numbers::pi*distance/4);
                const auto coefficient=sinc(std::numbers::pi*distance)*window;
                coefficients[phase][tap]=coefficient;sum+=coefficient;
            }
            for(auto& coefficient:coefficients[phase])coefficient/=sum;
        }
        begin();
    }
    double uniform() {return (static_cast<double>(startup_random()>>11)+.5)/9007199254740992.;}
    void begin() {
        origin=received;source=nullptr;total=0;buffer_start=0;buffer_count=0;complete=false;
        startup=channel.delay_samples+std::floor((.05L+.25L*uniform())*config.sample_rate)+.05L+.9L*uniform();
        // Analytic source PCM begins at source sample zero for every burst.
        // Restore the carrier phase accrued before that waveform began.
        burst_rotation=phase_rotation(tau*config.carrier_hz*rate*(static_cast<long double>(origin)+startup)/config.sample_rate);
    }
    void advance() {
        if(received==std::numeric_limits<std::uint64_t>::max())throw Error("sampled simulation receiver counter overflow");
        ++received;oscillator*=step;
        if(diffusion)oscillator*=phase_rotation(diffusion*phase_normal(phase_random));
        if((received&4095U)==0)oscillator/=std::abs(oscillator);
    }
    double noise() {return sigma?noise_normal(noise_random)*sigma:0.;}
    Complex interpolate(StreamingTransmitter& transmitter,long double position,std::stop_token stop) {
        const auto center=static_cast<std::uint64_t>(std::floor(position));
        const auto low=center>7?center-7:0;
        const auto high=center+std::min<std::uint64_t>(total-1-center,8);
        if(high>=buffer_start+buffer_count) {
            const auto discard=static_cast<std::size_t>(std::min<std::uint64_t>(buffer_count,low-buffer_start));
            std::move(buffer.begin()+static_cast<std::ptrdiff_t>(discard),buffer.begin()+static_cast<std::ptrdiff_t>(buffer_count),buffer.begin());
            buffer_start+=discard;buffer_count-=discard;
            buffer_count+=transmitter.read_analytic(std::span(buffer).subspan(buffer_count),stop);
            if(high>=buffer_start+buffer_count)throw Error("sampled simulation source ended before its advertised duration");
        }
        const auto fractional=static_cast<double>(position-center)*phases;
        const auto phase=std::min(phases-1,static_cast<std::size_t>(fractional));
        const auto blend=fractional-static_cast<double>(phase);
        Complex value{};
        for(std::size_t tap=0;tap<taps;++tap) {
            if((tap<7 && center<7-tap) || (tap>=7 && total-center<=tap-7))continue;
            const auto index=tap<7?center-(7-tap):center+(tap-7);
            const auto coefficient=coefficients[phase][tap]+blend*(coefficients[phase+1][tap]-coefficients[phase][tap]);
            value+=buffer[static_cast<std::size_t>(index-buffer_start)]*coefficient;
        }
        return value;
    }
};
SampledSimulationChannel::SampledSimulationChannel(Config config,ChannelConfig channel):impl_(std::make_unique<Impl>(config,channel)){}
SampledSimulationChannel::~SampledSimulationChannel()=default;
SampledSimulationChannel::SampledSimulationChannel(SampledSimulationChannel&&)noexcept=default;
SampledSimulationChannel& SampledSimulationChannel::operator=(SampledSimulationChannel&&)noexcept=default;
void SampledSimulationChannel::begin_burst(){impl_->begin();}
std::uint64_t SampledSimulationChannel::received_samples()const{return impl_->received;}
std::uint64_t SampledSimulationChannel::transmitted_samples()const {
    const auto& s=*impl_;
    const auto position=(static_cast<long double>(s.received-s.origin)-s.startup)*s.rate;
    return position<=0?0:position>=s.total?s.total:static_cast<std::uint64_t>(position);
}
std::size_t SampledSimulationChannel::working_bytes()const{return sizeof(Impl)+sizeof(*this);}
double SampledSimulationChannel::startup_offset_samples()const{return static_cast<double>(impl_->startup);}
double SampledSimulationChannel::carrier_phase_radians()const {
    const auto& s=*impl_;
    return std::arg(s.oscillator*phase_rotation(tau*s.config.carrier_hz*s.rate*s.received/s.config.sample_rate));
}
std::size_t SampledSimulationChannel::read(StreamingTransmitter& source,std::span<float> output,std::stop_token stop) {
    auto& s=*impl_;
    if(stop.stop_requested())throw Error("modem operation cancelled");
    if(output.empty() || s.complete)return 0;
    if(!s.source) {
        if(source.samples_emitted())throw Error("sampled simulation requires a fresh transmitter for each burst");
        s.source=&source;s.total=source.total_samples();
        const auto duration=std::ceil(s.startup+static_cast<long double>(s.total)/s.rate);
        if(duration>=static_cast<long double>(std::numeric_limits<std::uint64_t>::max()-s.origin))
            throw Error("sampled simulation duration exceeds the receiver counter");
    } else if(s.source!=&source || source.samples_emitted()!=s.buffer_start+s.buffer_count)
        throw Error("sampled simulation source changed without begin_burst");
    std::size_t written=0;
    while(written<output.size()) {
        if((written&4095U)==0 && stop.stop_requested())throw Error("modem operation cancelled");
        const auto position=(static_cast<long double>(s.received-s.origin)-s.startup)*s.rate;
        if(position>=s.total){s.complete=true;break;}
        const auto value=position<0?Complex{}:s.interpolate(source,position,stop)*s.oscillator*s.burst_rotation;
        output[written++]=static_cast<float>(value.real()+s.noise());s.advance();
    }
    return written;
}
void SampledSimulationChannel::read_noise(std::span<float> output,std::stop_token stop) {
    auto& s=*impl_;
    if(stop.stop_requested())throw Error("modem operation cancelled");
    for(std::size_t i=0;i<output.size();++i) {
        if((i&4095U)==0 && stop.stop_requested())throw Error("modem operation cancelled");
        output[i]=static_cast<float>(s.noise());s.advance();
    }
}
struct SimulationChannel::Impl {
    Config config;
    ChannelConfig channel;
    long double rate=1,frequency=0;
    std::uint64_t transmitted=0,received=0;
    double phase=0,diffusion=0;
    std::mt19937_64 phase_random,noise_random;
    struct Knot {std::uint64_t sample=0;double phase=0;};
    // Each knot spans at least one sample.4096 covers a2048-sample preview
    // plus interpolation margins throughout the supported +/-1% clock range.
    std::array<Knot,4096> knots{};
    std::size_t begin=0,count=1;
    Impl(Config value,ChannelConfig impairment):config(value),channel(impairment),
        phase_random(impairment.seed^0xa0761d6478bd642fULL),noise_random(impairment.seed) {
        validate(config);validate_channel(config,channel);
        rate=1+static_cast<long double>(channel.clock_error_ppm)*1e-6L;
        frequency=channel.frequency_offset_hz+static_cast<long double>(config.carrier_hz)*(rate-1);
        diffusion=channel.phase_noise_degrees_per_sqrt_second*std::numbers::pi/180;
    }
    void retain(std::uint64_t sample,double value) {
        if(count==knots.size()){begin=(begin+1)%knots.size();--count;}
        knots[(begin+count++)%knots.size()]={sample,value};
    }
    double phase_at(long double sample)const {
        const auto at=[&](std::size_t index)->const Knot&{return knots[(begin+index)%knots.size()];};
        if(sample<=at(0).sample)return at(0).phase;
        if(sample>=at(count-1).sample)return at(count-1).phase;
        std::size_t low=0,high=count-1;
        while(high-low>1){const auto mid=low+(high-low)/2;if(at(mid).sample<=sample)low=mid;else high=mid;}
        const auto& left=at(low);const auto& right=at(high);
        return left.phase+(right.phase-left.phase)*static_cast<double>((sample-left.sample)/(right.sample-left.sample));
    }
};
SimulationChannel::SimulationChannel(Config config,ChannelConfig channel):impl_(std::make_unique<Impl>(config,channel)){}
SimulationChannel::~SimulationChannel()=default;
SimulationChannel::SimulationChannel(SimulationChannel&&)noexcept=default;
SimulationChannel& SimulationChannel::operator=(SimulationChannel&&)noexcept=default;
std::size_t SimulationChannel::working_bytes()const{return sizeof(Impl);}
std::uint64_t SimulationChannel::received_samples()const{return impl_->received;}
std::uint64_t SimulationChannel::preview_end_samples()const{
    const auto lookahead=static_cast<std::uint64_t>(std::ceil(8/impl_->rate));
    return impl_->received>lookahead?impl_->received-lookahead:0;
}
double SimulationChannel::phase_noise_radians()const{return impl_->phase;}
SymbolObservation SimulationChannel::noise(std::uint64_t count) {
    return add_awgn({{},count},impl_->channel.snr_db,impl_->noise_random);
}
std::optional<SymbolObservation> SimulationChannel::process(SymbolObservation observation) {
    auto& s=*impl_;
    if(!observation.sample_count || !std::isfinite(observation.value.real()) || !std::isfinite(observation.value.imag()))
        throw Error("invalid simulation observation");
    if(observation.sample_count>std::numeric_limits<std::uint64_t>::max()-s.transmitted)
        throw Error("simulation transmitter counter overflow");
    const auto transmitted=s.transmitted+observation.sample_count;
    const auto endpoint=std::ceil(static_cast<long double>(transmitted)/s.rate);
    if(endpoint>=static_cast<long double>(std::numeric_limits<std::uint64_t>::max()))
        throw Error("simulation receiver counter overflow");
    const auto end=static_cast<std::uint64_t>(endpoint),count=end-s.received;
    s.transmitted=transmitted;
    if(!count)return {};
    const auto pieces=std::min<std::uint64_t>(16,count);
    Complex integral{};auto cursor=s.received;
    std::normal_distribution<double> normal(0,1);
    for(std::uint64_t piece=0;piece<pieces;++piece) {
        const auto next=s.received+(count/pieces)*(piece+1)+((count%pieces)*(piece+1))/pieces;
        const auto duration=static_cast<double>(next-cursor)/s.config.sample_rate;
        const auto change=s.diffusion?normal(s.phase_random)*s.diffusion*std::sqrt(duration):0.;
        const auto angle=tau*s.frequency*duration+change;
        const auto middle=tau*s.frequency*(static_cast<long double>(cursor)/s.config.sample_rate)+s.phase+angle/2;
        // Analytic ramp integral prevents carrier aliasing even when one
        // observation represents billions of cycles. Unresolved Brownian
        // bridge variance contributes its mean coherence attenuation.
        const auto bridge=std::exp(-s.diffusion*s.diffusion*duration/12);
        integral+=phase_rotation(middle)*(sinc(static_cast<double>(angle/2))*bridge*static_cast<double>(next-cursor));
        s.phase+=change;s.retain(next,s.phase);cursor=next;
    }
    s.received=end;
    observation.value*=integral/static_cast<double>(count);observation.sample_count=count;
    return add_awgn(observation,s.channel.snr_db,s.noise_random);
}
void SimulationChannel::preview_last(const StreamingTransmitter& source,std::span<float> output)const {
    const auto& s=*impl_;
    if(output.size()>2048)throw Error("simulation preview is limited to2048 samples");
    if(source.samples_emitted()!=s.transmitted)throw Error("simulation preview source does not match channel position");
    std::array<Complex,StreamingTransmitter::analytic_preview_limit> analytic{};source.preview_last_analytic(analytic);
    const auto available=std::min<std::uint64_t>(analytic.size(),s.transmitted);
    const auto tx_start=s.transmitted-available;
    const auto leading=analytic.size()-static_cast<std::size_t>(available);
    const auto end=preview_end_samples();
    const auto count=std::min<std::uint64_t>(output.size(),end);
    const auto padding=output.size()-static_cast<std::size_t>(count);
    std::fill(output.begin(),output.end(),0);
    const auto sigma=std::sqrt(nominal_signal_power*std::pow(10.,-s.channel.snr_db/10));
    for(std::uint64_t index=0;index<count;++index) {
        const auto rx=end-count+index;
        const auto position=static_cast<long double>(rx)*s.rate;
        const auto local=position-tx_start+leading;
        const auto center=static_cast<std::int64_t>(std::floor(local));
        Complex value{};double weight=0;
        if(local>=0 && local<analytic.size()) {
            if(std::abs(local-center)<1e-10)value=analytic[static_cast<std::size_t>(center)];
            else for(auto tap=center-7;tap<=center+8;++tap) {
                const auto distance=static_cast<double>(local-tap);
                const auto window=.42+.5*std::cos(std::numbers::pi*distance/8)+.08*std::cos(std::numbers::pi*distance/4);
                const auto coefficient=sinc(std::numbers::pi*distance)*window;
                weight+=coefficient;
                if(tap>=0 && tap<static_cast<std::int64_t>(analytic.size()))value+=analytic[static_cast<std::size_t>(tap)]*coefficient;
            }
            if(weight)value/=weight;
        }
        const auto phase=tau*s.channel.frequency_offset_hz*(static_cast<long double>(rx)/s.config.sample_rate)+s.phase_at(rx);
        output[padding+static_cast<std::size_t>(index)]=static_cast<float>((value*phase_rotation(phase)).real()+
            sigma*preview_normal(s.channel.seed^0xe7037ed1a0b428dbULL,rx));
    }
}
}
