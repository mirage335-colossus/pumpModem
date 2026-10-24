#include "datapump/channel.hpp"
#include "live_simulation_clock.hpp"
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
struct SampledLegacy {
    static constexpr std::size_t taps=16,phases=256;
    Config config;
    ChannelConfig channel;
    long double rate=1,startup=0;
    std::uint64_t received=0,origin=0,total=0,buffer_start=0;
    std::size_t buffer_count=0;
    StreamingTransmitter* source=nullptr;
    bool complete=false,helper_only=false;
    Complex oscillator{},step{},burst_rotation{};
    double diffusion=0,sigma=0;
    std::mt19937_64 startup_random,phase_random,noise_random;
    std::normal_distribution<double> phase_normal{0,1},noise_normal{0,1};
    std::array<Complex,1024> buffer{};
    std::array<std::array<double,taps>,phases+1> coefficients{};
    std::optional<helper::Generator> helper_generator;
    helper::Options helper_options;
    std::optional<helper::Plan> helper_plan;
    long double helper_origin=0;
    SampledLegacy(Config value,ChannelConfig impairment,helper::Options helper_value,double helper_bandwidth):config(value),channel(impairment),
        startup_random(impairment.seed^0x8ebc6af09c88c6e3ULL),
        phase_random(impairment.seed^0xa0761d6478bd642fULL),noise_random(impairment.seed),helper_options(std::move(helper_value)) {
        static_assert(sizeof(SampledLegacy)+sizeof(SampledSimulationChannel)+
            SampledSimulationChannel::auxiliary_workspace_bound<=SampledSimulationChannel::workspace_bound);
        validate(config);validate_channel(config,channel);
        rate=1+static_cast<long double>(channel.clock_error_ppm)*1e-6L;
        diffusion=channel.phase_noise_degrees_per_sqrt_second*std::numbers::pi/180/std::sqrt(config.sample_rate);
        sigma=std::sqrt(nominal_signal_power*std::pow(10.,-channel.snr_db/10));
        if(helper_options.power_w>0) {
            const auto plan=helper::plan(helper_options,config.carrier_hz,
                helper_bandwidth>0?helper_bandwidth:waveform_bandwidth_hz(config)+waveform_gate_bandwidth_allowance_hz(config),
                config.sample_rate,0,outer_dsss_transmit_gain(config));
            helper_plan=plan;
            sigma*=plan.data_gain;
        }
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
namespace {
// The enabled channel is exclusive of SampledLegacy: no second source queue or
// double-precision interpolation table is retained beside this physical route.
struct SampledPhysical {
    static constexpr std::size_t taps=16,phases=256,fir_taps=257,max_stages=5;
    struct Decimator {
        std::array<float,fir_taps> history{};
        std::size_t next=0;
        bool odd=false;
        bool push(float value,const std::array<double,fir_taps>& coefficients,float& output) {
            history[next]=value;
            const auto newest=next;
            if(++next==fir_taps)next=0;
            const bool produce=!odd;odd=!odd;if(!produce)return false;
            const auto at=[&](std::size_t delay) {return history[(newest+fir_taps-delay)%fir_taps];};
            double sum=coefficients[fir_taps/2]*at(fir_taps/2);
            // Exact quarter-rate sinc zeros; symmetric halfband pairs halve
            // the remaining work without a table for every fractional phase.
            for(std::size_t tap=1;tap<fir_taps/2;tap+=2)
                sum+=coefficients[tap]*(static_cast<double>(at(tap))+at(fir_taps-1-tap));
            output=static_cast<float>(sum);return true;
        }
    };
    Config config;
    ChannelConfig channel;
    helper::Options options;
    helper::Plan plan;
    std::optional<Crypto> time_key;
    std::optional<helper::Generator> generator;
    SampledSimulationChannel::PhysicalObserver observer;
    std::uint32_t physical_rate=0,factor=1;
    std::size_t stage_count=0;
    long double rate=1,startup=0,helper_origin=0;
    std::uint64_t received=0,physical_received=0,origin=0,total=0,buffer_start=0;
    std::size_t buffer_count=0,observer_count=0;
    StreamingTransmitter* source=nullptr;
    bool complete=false,helper_only=false,primed=false;
    Complex oscillator{},step{},burst_rotation{};
    double diffusion=0,sigma=0;
    std::mt19937_64 startup_random,phase_random,noise_random;
    std::normal_distribution<double> phase_normal{0,1},noise_normal{0,1};
    std::array<Complex,1024> buffer{};
    std::array<std::array<float,taps>,phases+1> coefficients{};
    std::array<double,fir_taps> fir{};
    std::array<Decimator,max_stages> stages{};
    std::array<float,512> observer_buffer{};

    static double bessel_i0(double value) {
        double sum=1,term=1;
        for(unsigned n=1;n<80;++n) {
            term*=value*value/(4.*n*n);sum+=term;
            if(term<sum*1e-17)break;
        }
        return sum;
    }
    SampledPhysical(Config value,ChannelConfig impairment,helper::Options helper_value,double bandwidth)
        :config(value),channel(impairment),options(std::move(helper_value)),
         startup_random(impairment.seed^0x8ebc6af09c88c6e3ULL),
         phase_random(impairment.seed^0xa0761d6478bd642fULL),noise_random(impairment.seed) {
        validate(config);validate_channel(config,channel);helper::validate(options);
        const auto occupied=bandwidth>0?bandwidth:waveform_bandwidth_hz(config)+waveform_gate_bandwidth_allowance_hz(config);
        // At least 4x permits an unaliased helper above the original logical
        // Nyquist interval. Unusual small-rate/high-power routes may need more.
        for(stage_count=2;stage_count<=max_stages;++stage_count) {
            factor=1U<<stage_count;
            if(config.sample_rate>120000000U/factor)break;
            physical_rate=config.sample_rate*factor;
            try {
                plan=helper::plan(options,config.carrier_hz,occupied,physical_rate,
                                  static_cast<double>(config.sample_rate)+250,outer_dsss_transmit_gain(config));
                break;
            } catch(const Error&) {
                if(stage_count==max_stages)throw;
            }
        }
        if(stage_count>max_stages||config.sample_rate>120000000U/factor)
            throw Error("Helper Carrier simulation has no bounded physical sample rate for this geometry");
        // Shared 257-tap Kaiser halfband, beta=14. Exact zeros are retained.
        // Its128*(factor-1) physical delay is an integer logical count.
        const auto normalization=bessel_i0(14);
        double sum=0;
        for(std::size_t tap=0;tap<fir_taps;++tap) {
            const auto distance=static_cast<int>(tap)-static_cast<int>(fir_taps/2);
            double coefficient=distance==0?.5:distance%2==0?0:
                std::sin(std::numbers::pi*distance/2)/(std::numbers::pi*distance);
            const auto coordinate=static_cast<double>(distance)/(fir_taps/2);
            coefficient*=bessel_i0(14*std::sqrt(std::max(0.,1-coordinate*coordinate)))/normalization;
            fir[tap]=coefficient;sum+=coefficient;
        }
        for(auto& coefficient:fir)coefficient/=sum;
        // Admit only geometry whose actual FIR cascade preserves the payload
        // band and rejects the much brighter helper. No logical modem retune.
        const auto clock_rate=1+channel.clock_error_ppm*1e-6;
        const auto low=(config.carrier_hz-occupied/2)*clock_rate+channel.frequency_offset_hz;
        const auto high=(config.carrier_hz+occupied/2)*clock_rate+channel.frequency_offset_hz;
        for(unsigned i=0;i<=64;++i) {
            const auto gain=std::abs(response(low+(high-low)*i/64));
            if(gain<.999||gain>1.001)
                throw Error("Helper Carrier simulation cannot preserve this near-Nyquist data passband");
        }
        const auto power_ratio=plan.effective_power_w/options.data_power_w;
        for(unsigned i=0;options.frequency==helper::Frequency::high&&i<=120;++i) {
            const auto gain=std::abs(response((plan.frequency_hz-150+300.*i/120)*clock_rate+channel.frequency_offset_hz));
            if(gain*gain*power_ratio>1e-6)
                throw Error("Helper Carrier simulation cannot isolate this helper from the original data passband");
        }
        rate=1+static_cast<long double>(channel.clock_error_ppm)*1e-6L;
        diffusion=channel.phase_noise_degrees_per_sqrt_second*std::numbers::pi/180/std::sqrt(physical_rate);
        // Preserve the original flat in-data-band AWGN density. The FIR's
        // transition band removes a little total logical variance (about1.04%
        // at4x); normalizing that variance back up would worsen matched payload
        // SNR. Helper phase-noise sidebands remain real additional interference.
        sigma=std::sqrt(nominal_signal_power*std::pow(10.,-channel.snr_db/10))*plan.data_gain*std::sqrt(factor);
        oscillator=phase_rotation(tau*(uniform()-.5L));
        step=phase_rotation(tau*channel.frequency_offset_hz/physical_rate);
        for(std::size_t phase=0;phase<=phases;++phase) {
            double coefficient_sum=0;
            std::array<double,taps> row{};
            for(std::size_t tap=0;tap<taps;++tap) {
                const auto distance=static_cast<double>(phase)/phases-static_cast<double>(tap)+7;
                const auto window=.42+.5*std::cos(std::numbers::pi*distance/8)+.08*std::cos(std::numbers::pi*distance/4);
                row[tap]=sinc(std::numbers::pi*distance)*window;coefficient_sum+=row[tap];
            }
            for(std::size_t tap=0;tap<taps;++tap)coefficients[phase][tap]=static_cast<float>(row[tap]/coefficient_sum);
        }
        begin();
    }
    double response(double frequency)const {
        double gain=1;
        auto sample_rate=static_cast<double>(physical_rate);
        for(std::size_t stage=0;stage<stage_count;++stage) {
            const auto omega=2*std::numbers::pi*frequency/sample_rate;
            double amplitude=fir[fir_taps/2];
            for(std::size_t tap=1;tap<fir_taps/2;tap+=2)
                amplitude+=2*fir[tap]*std::cos(omega*(static_cast<int>(tap)-static_cast<int>(fir_taps/2)));
            gain*=amplitude;sample_rate/=2;
        }
        return gain;
    }
    bool decimate(float value,float& output) {
        for(std::size_t stage=0;stage<stage_count;++stage)
            if(!stages[stage].push(value,fir,value))return false;
        output=value;return true;
    }
    long double group_delay()const {return static_cast<long double>(fir_taps/2)*(factor-1)/factor;}
    double uniform() {return (static_cast<double>(startup_random()>>11)+.5)/9007199254740992.;}
    void begin() {
        origin=received;source=nullptr;total=0;buffer_start=0;buffer_count=0;complete=false;generator.reset();
        startup=channel.delay_samples+std::floor((.05L+.25L*uniform())*config.sample_rate)+.05L+.9L*uniform();
        // A previously observed lookahead cannot be rewritten when a fresh
        // burst is enqueued. Keep its onset beyond that physical frontier.
        startup=std::max(startup,group_delay()+1);
        burst_rotation=phase_rotation(tau*config.carrier_hz*rate*(static_cast<long double>(origin)+startup)/config.sample_rate);
    }
    void flush_observer() {
        if(observer&&observer_count)observer(std::span(observer_buffer).first(observer_count),physical_rate,physical_received-observer_count);
        observer_count=0;
    }
    void advance() {
        if(physical_received==std::numeric_limits<std::uint64_t>::max())throw Error("sampled simulation physical receiver counter overflow");
        ++physical_received;oscillator*=step;
        if(diffusion)oscillator*=phase_rotation(diffusion*phase_normal(phase_random));
        if((physical_received&4095U)==0)oscillator/=std::abs(oscillator);
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
            if((tap<7&&center<7-tap)||(tap>=7&&total-center<=tap-7))continue;
            const auto index=tap<7?center-(7-tap):center+(tap-7);
            const auto coefficient=coefficients[phase][tap]+blend*(coefficients[phase+1][tap]-coefficients[phase][tap]);
            value+=buffer[static_cast<std::size_t>(index-buffer_start)]*coefficient;
        }
        return value;
    }
    float next(StreamingTransmitter* transmitter,std::stop_token stop) {
        float output=0;
        for(;;) {
            const auto time=static_cast<long double>(physical_received)/factor;
            const auto position=(time-origin-startup)*rate;
            Complex value{};
            if(transmitter&&position>=0&&position<total) {
                const auto payload=interpolate(*transmitter,position,stop);
                value=helper_only?Complex{}:payload*burst_rotation;
                value=generator->mix_analytic(value,helper_origin+position/config.sample_rate,physical_rate,
                    helper_origin+static_cast<long double>(total)/config.sample_rate,helper_origin);
                value*=oscillator;
            }
            const auto pcm=static_cast<float>(value.real()+noise());
            if(observer)observer_buffer[observer_count++]=pcm;
            advance();
            if(observer_count==observer_buffer.size())flush_observer();
            if(decimate(pcm,output))return output;
        }
    }
    void prime(StreamingTransmitter* transmitter,std::stop_token stop) {
        if(primed)return;
        // Every discarded decimator output advances exactly one logical
        // sample. Afterwards outputn is the centered fit at physicaltime n.
        const auto delay=static_cast<std::size_t>(group_delay());
        for(std::size_t i=0;i<delay;++i) {
            if((i&63U)==0&&stop.stop_requested())throw Error("modem operation cancelled");
            next(transmitter,stop);
        }
        primed=true;
    }
    std::size_t read(StreamingTransmitter& transmitter,std::span<float> output,std::stop_token stop) {
        if(stop.stop_requested())throw Error("modem operation cancelled");
        if(output.empty()||complete)return 0;
        if(!source) {
            if(transmitter.samples_emitted())throw Error("sampled simulation requires a fresh transmitter for each burst");
            source=&transmitter;total=transmitter.total_samples();
            const auto physical_origin=transmitter.physical_output_origin();
            helper_origin=static_cast<long double>(physical_origin%config.sample_rate)/config.sample_rate;
            generator.emplace(options,plan,physical_origin/config.sample_rate,true,time_key);
            const auto duration=std::ceil(startup+static_cast<long double>(total)/rate);
            if(duration>=static_cast<long double>(std::numeric_limits<std::uint64_t>::max()/factor-origin))
                throw Error("sampled simulation duration exceeds the physical receiver counter");
        } else if(source!=&transmitter||transmitter.samples_emitted()!=buffer_start+buffer_count)
            throw Error("sampled simulation source changed without begin_burst");
        const auto endpoint=transmitter.total_samples();
        if(endpoint>total||endpoint<transmitter.samples_emitted())
            throw Error("sampled simulation source endpoint changed incompatibly");
        total=endpoint;
        if(!total){complete=true;return 0;}
        prime(&transmitter,stop);
        const auto end=startup+static_cast<long double>(total)/rate;
        std::size_t written=0;
        while(written<output.size()) {
            if((written&4095U)==0&&stop.stop_requested())throw Error("modem operation cancelled");
            if(static_cast<long double>(received-origin)>=end){complete=true;break;}
            output[written++]=next(&transmitter,stop);++received;
            if(written==1)live::detail::simulation_checkpoint(live::detail::SimulationPhase::read_progress,received);
        }
        flush_observer();return written;
    }
    void read_noise(std::span<float> output,std::stop_token stop) {
        if(stop.stop_requested())throw Error("modem operation cancelled");
        if(output.empty())return;
        prime(nullptr,stop);
        for(std::size_t i=0;i<output.size();++i) {
            if((i&4095U)==0&&stop.stop_requested())throw Error("modem operation cancelled");
            output[i]=next(nullptr,stop);++received;
        }
        flush_observer();
    }
};
}
struct SampledSimulationChannel::Impl {
    std::unique_ptr<SampledLegacy> legacy;
    std::unique_ptr<SampledPhysical> physical;
    PhysicalObserver observer;
    std::optional<Crypto> time_key;
    Impl(Config config,ChannelConfig channel,helper::Options options,double bandwidth) {
        static_assert(sizeof(Impl)+sizeof(SampledLegacy)+sizeof(SampledSimulationChannel)+
            SampledSimulationChannel::auxiliary_workspace_bound<=SampledSimulationChannel::workspace_bound);
        static_assert(sizeof(Impl)+sizeof(SampledPhysical)+sizeof(SampledSimulationChannel)+
            SampledSimulationChannel::auxiliary_workspace_bound<=SampledSimulationChannel::workspace_bound);
        if(options.power_w>0)physical=std::make_unique<SampledPhysical>(config,channel,std::move(options),bandwidth);
        else legacy=std::make_unique<SampledLegacy>(config,channel,std::move(options),bandwidth);
    }
};

SampledSimulationChannel::SampledSimulationChannel(Config config,ChannelConfig channel,helper::Options helper,double helper_bandwidth):impl_(std::make_unique<Impl>(config,channel,std::move(helper),helper_bandwidth)){}
SampledSimulationChannel::~SampledSimulationChannel()=default;
SampledSimulationChannel::SampledSimulationChannel(SampledSimulationChannel&&)noexcept=default;
SampledSimulationChannel& SampledSimulationChannel::operator=(SampledSimulationChannel&&)noexcept=default;
void SampledSimulationChannel::begin_burst(bool helper_only){
    if(helper_only&&!impl_->physical)throw Error("Helper-only sampled output requires an enabled helper");
    if(impl_->physical) {impl_->physical->helper_only=helper_only;impl_->physical->begin();return;}
    impl_->legacy->helper_only=helper_only;impl_->legacy->begin();
}
void SampledSimulationChannel::set_helper_callsign(std::string callsign) {
    auto& current=impl_->physical?impl_->physical->options:impl_->legacy->helper_options;
    auto options=current;options.callsign=std::move(callsign);helper::validate(options);current=std::move(options);
}
void SampledSimulationChannel::set_helper_time_key(std::optional<Crypto> key) {
    if(impl_->physical)impl_->physical->time_key=std::move(key);else impl_->time_key=std::move(key);
}
void SampledSimulationChannel::set_physical_observer(PhysicalObserver observer) {
    if(impl_->physical)impl_->physical->observer=std::move(observer);else impl_->observer=std::move(observer);
}
std::uint32_t SampledSimulationChannel::physical_sample_rate()const {return impl_->physical?impl_->physical->physical_rate:impl_->legacy->config.sample_rate;}
std::uint64_t SampledSimulationChannel::physical_received_samples()const {return impl_->physical?impl_->physical->physical_received:impl_->legacy->received;}
std::uint64_t SampledSimulationChannel::received_samples()const{return impl_->physical?impl_->physical->received:impl_->legacy->received;}
std::uint64_t SampledSimulationChannel::transmitted_samples()const {
    if(impl_->physical) {
        const auto& s=*impl_->physical;
        const auto position=(static_cast<long double>(s.received-s.origin)-s.startup)*s.rate;
        return position<=0?0:position>=s.total?s.total:static_cast<std::uint64_t>(position);
    }
    const auto& s=*impl_->legacy;
    const auto position=(static_cast<long double>(s.received-s.origin)-s.startup)*s.rate;
    return position<=0?0:position>=s.total?s.total:static_cast<std::uint64_t>(position);
}
std::size_t SampledSimulationChannel::working_bytes()const{return sizeof(Impl)+sizeof(*this)+(impl_->physical?sizeof(SampledPhysical):sizeof(SampledLegacy));}
double SampledSimulationChannel::startup_offset_samples()const{return impl_->physical?static_cast<double>(impl_->physical->startup):static_cast<double>(impl_->legacy->startup);}
double SampledSimulationChannel::carrier_phase_radians()const {
    if(impl_->physical) {
        const auto& s=*impl_->physical;
        return std::arg(s.oscillator*phase_rotation(tau*s.config.carrier_hz*s.rate*s.physical_received/s.physical_rate));
    }
    const auto& s=*impl_->legacy;
    return std::arg(s.oscillator*phase_rotation(tau*s.config.carrier_hz*s.rate*s.received/s.config.sample_rate));
}
std::size_t SampledSimulationChannel::read(StreamingTransmitter& source,std::span<float> output,std::stop_token stop) {
    if(impl_->physical)return impl_->physical->read(source,output,stop);
    auto& s=*impl_->legacy;
    if(stop.stop_requested())throw Error("modem operation cancelled");
    if(output.empty() || s.complete)return 0;
    if(!s.source) {
        if(source.samples_emitted())throw Error("sampled simulation requires a fresh transmitter for each burst");
        s.source=&source;s.total=source.total_samples();
        if(s.helper_plan) {
            const auto origin=source.physical_output_origin();
            s.helper_origin=static_cast<long double>(origin%s.config.sample_rate)/s.config.sample_rate;
            s.helper_generator.emplace(s.helper_options,*s.helper_plan,origin/s.config.sample_rate,false);
        }
        const auto duration=std::ceil(s.startup+static_cast<long double>(s.total)/s.rate);
        if(duration>=static_cast<long double>(std::numeric_limits<std::uint64_t>::max()-s.origin))
            throw Error("sampled simulation duration exceeds the receiver counter");
    } else if(s.source!=&source || source.samples_emitted()!=s.buffer_start+s.buffer_count)
        throw Error("sampled simulation source changed without begin_burst");
    // A graceful stop may shorten the endpoint after interpolation has prefetched
    // source samples. Those samples remain valid; only the future endpoint moves.
    const auto endpoint=source.total_samples();
    if(endpoint>s.total || endpoint<source.samples_emitted())
        throw Error("sampled simulation source endpoint changed incompatibly");
    s.total=endpoint;
    if(!s.total){s.complete=true;return 0;}
    std::size_t written=0;
    while(written<output.size()) {
        if((written&4095U)==0 && stop.stop_requested())throw Error("modem operation cancelled");
        const auto position=(static_cast<long double>(s.received-s.origin)-s.startup)*s.rate;
        if(position>=s.total){s.complete=true;break;}
        auto value=position<0?Complex{}:s.interpolate(source,position,stop);
        if(position>=0 && s.helper_generator) {
            value=s.helper_generator->mix_analytic(s.helper_only?Complex{}:value,s.helper_origin+position/s.config.sample_rate,s.config.sample_rate,
                s.helper_origin+static_cast<long double>(s.total)/s.config.sample_rate,s.helper_origin);
        }
        if(position>=0)value=value*s.oscillator*s.burst_rotation;
        output[written++]=static_cast<float>(value.real()+s.noise());s.advance();
        if(written==1)live::detail::simulation_checkpoint(live::detail::SimulationPhase::read_progress,s.received);
    }
    if(impl_->observer&&written)impl_->observer(output.first(written),s.config.sample_rate,s.received-written);
    return written;
}
void SampledSimulationChannel::read_noise(std::span<float> output,std::stop_token stop) {
    if(impl_->physical){impl_->physical->read_noise(output,stop);return;}
    auto& s=*impl_->legacy;
    if(stop.stop_requested())throw Error("modem operation cancelled");
    for(std::size_t i=0;i<output.size();++i) {
        if((i&4095U)==0 && stop.stop_requested())throw Error("modem operation cancelled");
        output[i]=static_cast<float>(s.noise());s.advance();
    }
    if(impl_->observer&&!output.empty())impl_->observer(output,s.config.sample_rate,s.received-output.size());
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
