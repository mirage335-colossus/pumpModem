#include "datapump/channel.hpp"
#include "channel_perturbation.hpp"
#include "channel_spread.hpp"
#include "live_simulation_clock.hpp"
#include "audio_trace.hpp"
#include "helper_power_reference.hpp"
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
namespace {
using Perturbation=detail::FrequencySelectivePerturbation;
using SpreadRay=detail::SpreadRay;
std::unique_ptr<SpreadRay> make_spread(const Config& config,const ChannelConfig& channel,double sample_rate) {
    if(channel.spread_doppler_hz==0 && channel.spread_delay_ms==0)return {};
    const auto clock=1+channel.clock_error_ppm*1e-6;
    const auto half_band=helper::placement_band(config).waveform_bandwidth_hz/2;
    const auto modulation=channel.perturbation_fast_depth>0?channel.spread_doppler_hz:
        channel.perturbation_slow_depth>0?.1*channel.spread_doppler_hz:0;
    const auto upper=(config.carrier_hz+half_band)*clock+
        channel.frequency_offset_hz+channel.spread_doppler_hz+modulation;
    const auto lower=(config.carrier_hz-half_band)*clock+channel.frequency_offset_hz-modulation;
    if(channel.spread_doppler_hz>=sample_rate/2 || upper>config.sample_rate/2. || lower<0)
        throw Error("Doppler-spread payload leaves the sampled channel passband");
    auto result=std::make_unique<SpreadRay>(sample_rate,channel.spread_doppler_hz,channel.seed);
    audio::trace::record("simulation_two_ray_spread",[&](auto& f) {
        f.add("seed",channel.seed);f.add("delay_ms",channel.spread_delay_ms);
        f.add("doppler_hz",result->doppler_hz());f.add("phase_rad",result->phase_radians());
    });
    return result;
}
std::unique_ptr<Perturbation> make_perturbation(const Config& config,const ChannelConfig& channel,double sample_rate) {
    if(channel.spread_doppler_hz==0 || (channel.perturbation_slow_depth==0 && channel.perturbation_fast_depth==0))return {};
    const auto rate=1+channel.clock_error_ppm*1e-6;
    auto result=std::make_unique<Perturbation>(sample_rate,config.carrier_hz*rate,
        helper::placement_band(config).waveform_bandwidth_hz*rate,channel.spread_doppler_hz,
        channel.perturbation_slow_depth,channel.perturbation_fast_depth,channel.seed);
    audio::trace::record("simulation_frequency_selective_perturbation",[&](auto& f) {
        f.add("seed",channel.seed);f.add("doppler_reference_hz",channel.spread_doppler_hz);
        f.add("slow_depth",channel.perturbation_slow_depth);f.add("fast_depth",channel.perturbation_fast_depth);
        f.add("slow_rate_hz",result->rates_hz[0]);f.add("fast_rate_hz",result->rates_hz[1]);
        f.add("band_width_hz",result->band_width_hz);
        for(std::size_t i=0;i<3;++i) {
            f.add(("band"+std::to_string(i)+"_center_hz").c_str(),result->band_centers_hz[i]);
            f.add(("band"+std::to_string(i)+"_slow_phase_rad").c_str(),result->initial_phases[i][0]);
            f.add(("band"+std::to_string(i)+"_fast_phase_rad").c_str(),result->initial_phases[i][1]);
        }
    });
    return result;
}
}
struct SampledLegacy {
    static constexpr std::size_t taps=16,phases=256,stored_phases=249;
    Config config;
    ChannelConfig channel;
    std::unique_ptr<Perturbation> perturbation;
    std::unique_ptr<SpreadRay> spread;
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
    std::array<std::array<double,taps>,stored_phases> coefficients{};
    std::optional<helper::Generator> helper_generator;
    helper::Options helper_options;
    std::optional<helper::Plan> helper_plan;
    long double helper_origin=0;
    SampledLegacy(Config value,ChannelConfig impairment,helper::Options helper_value,double helper_bandwidth):config(value),channel(impairment),
        startup_random(impairment.seed^0x8ebc6af09c88c6e3ULL),
        phase_random(impairment.seed^0xa0761d6478bd642fULL),noise_random(impairment.seed),helper_options(std::move(helper_value)) {
        static_assert(sizeof(SampledLegacy)+sizeof(SampledSimulationChannel)+
            SampledSimulationChannel::auxiliary_workspace_bound+
            SampledSimulationChannel::helper_cohort_workspace_bound+sizeof(Perturbation)+sizeof(SpreadRay)<=SampledSimulationChannel::workspace_bound);
        validate(config);validate_channel(config,channel);
        perturbation=make_perturbation(config,channel,config.sample_rate);
        spread=make_spread(config,channel,config.sample_rate);
        rate=1+static_cast<long double>(channel.clock_error_ppm)*1e-6L;
        diffusion=channel.phase_noise_degrees_per_sqrt_second*std::numbers::pi/180/std::sqrt(config.sample_rate);
        sigma=std::sqrt(nominal_signal_power*std::pow(10.,-channel.snr_db/10));
        if(helper::effective_helper_power(helper_options)>0) {
            const auto plan=helper::plan(helper_options,helper::placement_band(config,helper_bandwidth),
                config.sample_rate,0,outer_dsss_transmit_gain(config));
            helper_plan=plan;
            sigma*=plan.data_gain;
        }
        oscillator=phase_rotation(tau*(uniform()-.5L));
        step=phase_rotation(tau*channel.frequency_offset_hz/config.sample_rate);
        // Interpolated polyphase Blackman-windowed sinc coefficients avoid
        // per-tap trigonometry while retaining fractional sample boundaries.
        for(std::size_t phase=0;phase<stored_phases;++phase)coefficients[phase]=make_coefficients(phase);
        begin();
    }
    static std::array<double,taps> make_coefficients(std::size_t phase) {
        std::array<double,taps> result{};double sum=0;
        for(std::size_t tap=0;tap<taps;++tap) {
            const auto distance=static_cast<double>(phase)/phases-static_cast<double>(tap)+7;
            const auto window=.42+.5*std::cos(std::numbers::pi*distance/8)+.08*std::cos(std::numbers::pi*distance/4);
            const auto coefficient=sinc(std::numbers::pi*distance)*window;
            result[tap]=coefficient;sum+=coefficient;
        }
        for(auto& coefficient:result)coefficient/=sum;
        return result;
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
        // Rare final phases are reconstructed identically to free bounded space
        // for the optional channel stages without changing the interpolation.
        std::array<double,taps> first{},second{};
        const auto* first_row=phase<stored_phases?&coefficients[phase]:(first=make_coefficients(phase),&first);
        const auto* second_row=phase+1<stored_phases?&coefficients[phase+1]:(second=make_coefficients(phase+1),&second);
        Complex value{};
        for(std::size_t tap=0;tap<taps;++tap) {
            if((tap<7 && center<7-tap) || (tap>=7 && total-center<=tap-7))continue;
            const auto index=tap<7?center-(7-tap):center+(tap-7);
            const auto coefficient=(*first_row)[tap]+blend*((*second_row)[tap]-(*first_row)[tap]);
            value+=buffer[static_cast<std::size_t>(index-buffer_start)]*coefficient;
        }
        return value;
    }
};
namespace {
// The enabled channel is exclusive of SampledLegacy: no second source queue or
// double-precision interpolation table is retained beside this physical route.
constexpr std::size_t sampled_physical_max_stages=5;
void validate_sampled_capture_domain(const std::optional<SampledSimulationChannel::PreconversionCapture>& capture) {
    if(!capture)return;
    const auto& domain=*capture;
    if(!std::isfinite(domain.minimum_hz)||!std::isfinite(domain.maximum_hz)||
       domain.minimum_hz>=domain.maximum_hz||
       (domain.minimum_sample_rate&&domain.minimum_sample_rate<64U)||domain.minimum_sample_rate>120000000U)
        throw Error("invalid Helper Carrier preconversion capture domain");
}
struct SampledPhysicalSelection {
    std::uint32_t rate=0,factor=1;
    std::size_t stages=0;
    double occupied=0;
    helper::Plan plan;
};
SampledPhysicalSelection select_sampled_physical_clock(const Config& config,const helper::Options& options,
        double bandwidth,const std::optional<SampledSimulationChannel::PreconversionCapture>& capture) {
    validate(config);helper::validate(options);validate_sampled_capture_domain(capture);
    if(helper::effective_helper_power(options)<=0)throw Error("Helper Carrier physical clock requires an enabled helper");
    const auto band=helper::placement_band(config,bandwidth);
    SampledPhysicalSelection selected;selected.occupied=band.waveform_bandwidth_hz+band.gate_bandwidth_hz;
    for(selected.stages=2;selected.stages<=sampled_physical_max_stages;++selected.stages) {
        selected.factor=1U<<selected.stages;
        if(config.sample_rate>120000000U/selected.factor)break;
        selected.rate=config.sample_rate*selected.factor;
        if(capture && (selected.rate<capture->minimum_sample_rate ||
           std::max(std::abs(capture->minimum_hz),std::abs(capture->maximum_hz))+10>=selected.rate/2.))continue;
        try {
            selected.plan=helper::plan(options,band,selected.rate,0,outer_dsss_transmit_gain(config));
            return selected;
        }catch(const Error&) {
            if(selected.stages==sampled_physical_max_stages)throw;
        }
    }
    throw Error("Helper Carrier simulation has no bounded physical sample rate for this geometry");
}
struct SampledPhysical {
    static constexpr std::size_t taps=16,phases=256,fir_taps=257,max_stages=sampled_physical_max_stages;
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
    std::unique_ptr<Perturbation> perturbation;
    std::unique_ptr<SpreadRay> spread;
    helper::Options options;
    helper::Plan plan;
    std::optional<Crypto> time_key;
    std::optional<helper::Generator> generator;
    SampledSimulationChannel::PhysicalObserver observer;
    SampledSimulationChannel::PhysicalRfObserver rf_observer;
    std::optional<SampledSimulationChannel::RfRetuneSimulation> retunes;
    std::optional<std::uint64_t> lab_helper_end_sample;
    SampledSimulationChannel::PhysicalRfSegment rf_segment;
    std::uint64_t retune_samples=0,settling_samples=0;
    Complex retune_rotation{1,0},retune_step{1,0},retune_step_slew{1,0};
    std::optional<SampledSimulationChannel::PreconversionCapture> capture_domain;
    std::uint32_t physical_rate=0,factor=1;
    std::size_t stage_count=0;
    long double rate=1,startup=0,helper_origin=0,lead_seconds=2,tail_seconds=2,helper_start=0,helper_end=0;
    std::uint64_t received=0,physical_received=0,origin=0,total=0,buffer_start=0,helper_epoch=0;
    std::size_t buffer_count=0,observer_count=0;
    StreamingTransmitter* source=nullptr;
    bool complete=false,helper_only=false,primed=false,logical_payload_preserved=true,power_reference_prepared=false;
    Complex oscillator{},step{},burst_rotation{};
    double diffusion=0,sigma=0,occupied_bandwidth_hz=0;
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
    SampledPhysical(Config value,ChannelConfig impairment,helper::Options helper_value,double bandwidth,
                    SampledSimulationChannel::HelperEnvelope envelope,
                    std::optional<SampledSimulationChannel::PreconversionCapture> capture)
        :config(value),channel(impairment),options(std::move(helper_value)),
         capture_domain(capture),
         startup_random(impairment.seed^0x8ebc6af09c88c6e3ULL),
         phase_random(impairment.seed^0xa0761d6478bd642fULL),noise_random(impairment.seed) {
        validate(config);helper::validate(options);
        if(capture_domain) {
            validate_sampled_capture_domain(capture_domain);
            // All ordinary impairment checks still apply. Only the offset's
            // logical Nyquist check moves to the explicit physical domain.
            auto narrow_checks=channel;narrow_checks.frequency_offset_hz=0;
            validate_channel(config,narrow_checks);
            if(!std::isfinite(channel.frequency_offset_hz))
                throw Error("invalid Helper Carrier physical frequency offset");
        } else validate_channel(config,channel);
        // The physical clock carries the closest power-qualified helper,
        // independently of the logical modem clock. Small-rate/high-power
        // routes can place it beyond logical Nyquist and may need more stages.
        const auto selected=select_sampled_physical_clock(config,options,bandwidth,capture_domain);
        stage_count=selected.stages;factor=selected.factor;physical_rate=selected.rate;plan=selected.plan;
        perturbation=make_perturbation(config,channel,physical_rate);
        spread=make_spread(config,channel,physical_rate);
        const auto occupied=selected.occupied;
        occupied_bandwidth_hz=occupied;
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
        // band. In-band helpers remain a real linear mixture, as in physical
        // audio; do not move or subtract them to hide their interference.
        const auto clock_rate=1+channel.clock_error_ppm*1e-6;
        const auto low=(config.carrier_hz-occupied/2)*clock_rate+channel.frequency_offset_hz;
        const auto high=(config.carrier_hz+occupied/2)*clock_rate+channel.frequency_offset_hz;
        // Both actual modulation bands belong to this physical mixture. The
        // auxiliary is 400 Hz below the pilot; checking only the pilot misses
        // auxiliary images which can fold into DATA at the logical clock.
        // The frontend's additional 75 Hz notch transitions are filter support,
        // not additional transmitted modulation bandwidth.
        const std::array helper_frequencies{plan.low_frequency_hz,plan.auxiliary_frequency_hz,plan.frequency_hz};
        constexpr double helper_half_width=150;
        if(capture_domain) {
            const auto helper_low=(helper_frequencies.front()-helper_half_width)*clock_rate+channel.frequency_offset_hz;
            const auto helper_high=(helper_frequencies.back()+helper_half_width)*clock_rate+channel.frequency_offset_hz;
            const auto physical_low=std::min(low,helper_low),physical_high=std::max(high,helper_high);
            if(!std::isfinite(physical_low)||!std::isfinite(physical_high)||
               physical_low<capture_domain->minimum_hz||physical_high>capture_domain->maximum_hz)
                throw Error("Helper Carrier actual signal lies outside its preconversion capture domain");
        }
        for(unsigned i=0;i<=64;++i) {
            const auto gain=std::abs(response(low+(high-low)*i/64));
            if(gain<.999||gain>1.001)logical_payload_preserved=false;
        }
        if(!logical_payload_preserved&&!capture_domain)
            throw Error("Helper Carrier simulation cannot preserve this near-Nyquist data passband");
        const auto power_ratio=plan.effective_power_w/options.data_power_w;
        for(const auto center:helper_frequencies)
            for(unsigned i=0;options.frequency==helper::Frequency::high&&i<=120;++i) {
                const auto frequency=(center-helper_half_width+2*helper_half_width*i/120)*clock_rate+channel.frequency_offset_hz;
                const auto alias=std::abs(std::remainder(frequency,static_cast<double>(config.sample_rate)));
                if(std::abs(frequency)<=config.sample_rate/2. || alias<low || alias>high)continue;
                const auto gain=std::abs(response(frequency));
                // Retain the original conservative total-Helper ratio bound
                // for each band; this does not change either tone's power.
                if(gain*gain*power_ratio>1e-6&&!capture_domain)
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
        begin(envelope);
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
    static long double rounded_duration(double seconds) {
        if(!std::isfinite(seconds)||seconds<0)throw Error("invalid Helper Carrier envelope duration");
        // One input-double ulp cannot turn an intended integral dwell count
        // (e.g. 3.2s) into an extra dwell after promotion to long double.
        const auto count=static_cast<long double>(std::max(2.,seconds))*2.5L;
        const auto nearest=std::round(count);
        const auto snapped=std::abs(count-nearest)<=4*std::numeric_limits<double>::epsilon()*count?nearest:count;
        const auto result=std::ceil(snapped)*.4L;
        if(!std::isfinite(result))throw Error("Helper Carrier envelope duration overflow");
        return result;
    }
    static long double dwell_boundary(long double seconds,bool upward) {
        const auto count=seconds*2.5L,nearest=std::round(count);
        const auto snapped=std::abs(count-nearest)<=8*std::numeric_limits<long double>::epsilon()*std::max(1.L,std::abs(count))?nearest:count;
        return (upward?std::ceil(snapped):std::floor(snapped))*.4L;
    }
    void validate_duration()const {
        const auto end=startup+(helper_end-helper_origin)*config.sample_rate/rate;
        const auto limit=std::numeric_limits<std::uint64_t>::max()/factor;
        if(origin>=limit||!std::isfinite(end)||end<0||
           std::ceil(end)+group_delay()+1>=static_cast<long double>(limit-origin))
            throw Error("sampled simulation duration exceeds the physical receiver counter");
    }
    void update_helper_end() {
        const auto phase=static_cast<long double>(helper_epoch%2);
        helper_end=dwell_boundary(phase+helper_origin+static_cast<long double>(total)/config.sample_rate+tail_seconds,true)-phase;
        helper_end=helper::protected_endpoint(helper_epoch,helper_start,helper_end);
        validate_duration();
    }
    void extend_tail(double seconds) {
        const auto duration=rounded_duration(seconds);
        const auto end=startup+(helper_end-helper_origin)*config.sample_rate/rate;
        if(complete||(source&&static_cast<long double>(received-origin)>=end))
            throw Error("cannot extend a completed Helper Carrier envelope");
        if(duration<=tail_seconds)return;
        const auto rendered=helper_origin+
            (static_cast<long double>(physical_received)/factor-origin-startup)*rate/config.sample_rate;
        if(source&&rendered>=helper_end-.004L)
            throw Error("cannot extend a Helper Carrier envelope after its physical final ramp");
        const auto available=static_cast<long double>(std::numeric_limits<std::uint64_t>::max()/factor)-received;
        if((lead_seconds+duration)*config.sample_rate/rate+group_delay()+1>=available)
            throw Error("Helper Carrier envelope duration overflow");
        const auto previous=tail_seconds;
        tail_seconds=std::max(tail_seconds,duration);
        try {if(source)update_helper_end();}
        catch(...) {tail_seconds=previous;if(source)update_helper_end();throw;}
    }
    void begin(SampledSimulationChannel::HelperEnvelope envelope) {
        const auto lead=rounded_duration(envelope.lead_seconds),tail=rounded_duration(envelope.tail_seconds);
        const auto available=static_cast<long double>(std::numeric_limits<std::uint64_t>::max()/factor)-received;
        if(channel.delay_samples+(lead+tail+.8L)*config.sample_rate/rate+group_delay()+1>=available)
            throw Error("Helper Carrier envelope duration overflow");
        origin=received;source=nullptr;total=0;buffer_start=0;buffer_count=0;complete=false;generator.reset();
        power_reference_prepared=false;
        lead_seconds=lead;tail_seconds=tail;helper_start=helper_end=0;helper_epoch=0;
        startup=channel.delay_samples+std::floor((.05L+.25L*uniform())*config.sample_rate)+.05L+.9L*uniform();
        // A previously observed lookahead cannot be rewritten when a fresh
        // burst is enqueued. Keep its onset beyond that physical frontier.
        startup=std::max(startup,group_delay()+1);
        startup+=lead_seconds*config.sample_rate/rate;
    }
    void attach(StreamingTransmitter& transmitter,std::uint64_t additional_idle=0,std::stop_token stop={}) {
        if(source)throw Error("Helper Carrier burst is already bound to a source");
        if(transmitter.samples_emitted())throw Error("sampled simulation requires a fresh transmitter for each burst");
        if(origin!=received)throw Error("Helper Carrier lead must be prepared before reading the burst");
        const auto physical_origin=transmitter.physical_output_origin();
        const auto next_origin=static_cast<long double>(physical_origin%config.sample_rate)/config.sample_rate;
        const auto next_epoch=physical_origin/config.sample_rate;
        const auto phase=static_cast<long double>(next_epoch%2);
        const auto normal_start=dwell_boundary(phase+next_origin-lead_seconds,false)-phase;
        const auto next_startup=startup+(next_origin-normal_start-lead_seconds)*config.sample_rate/rate+additional_idle;
        // Expand the same absolute calendar backward. The extra idle changes
        // neither the source's keyed epoch nor the original startup fraction.
        auto next_start=normal_start;
        if(additional_idle) {
            const auto expanded=dwell_boundary(phase+normal_start-
                static_cast<long double>(additional_idle)*rate/config.sample_rate,false)-phase;
            // The observer may already have seen centered-FIR lookahead from
            // an earlier idle. Never replace that recorded noise by a helper,
            // nor begin midway through its initial ramp. Start at the next
            // available whole dwell instead.
            const auto earliest=next_origin+
                (static_cast<long double>(physical_received)/factor-origin-next_startup)*rate/config.sample_rate;
            next_start=std::max(expanded,dwell_boundary(phase+earliest,true)-phase);
        }
        const auto limit=std::numeric_limits<std::uint64_t>::max()/factor;
        const auto next_total=transmitter.total_samples();
        auto next_end=dwell_boundary(phase+next_origin+
            static_cast<long double>(next_total)/config.sample_rate+tail_seconds,true)-phase;
        next_end=helper::protected_endpoint(next_epoch,next_start,next_end);
        const auto duration=std::max(next_startup+(next_end-next_origin)*config.sample_rate/rate,
            next_startup+static_cast<long double>(next_total)/rate+
                (spread?static_cast<long double>(channel.spread_delay_ms)*config.sample_rate/1000:0));
        if(!std::isfinite(next_startup)||next_startup<0||origin>=limit||
           !std::isfinite(duration)||duration<0||
           std::ceil(duration)+group_delay()+1>=static_cast<long double>(limit-origin))
            throw Error("Helper Carrier lead exceeds the physical receiver counter");
        // Construct optional metadata before publishing a source binding. An
        // unavailable cipher cannot leave a half-prepared active burst.
        auto next_plan=plan;
        if(!power_reference_prepared && !helper_only) {
            auto reference=helper::transmit_power_reference(config,transmitter,next_epoch,config.stream_epoch,time_key,stop);
            reference.helper_start_seconds=next_start;
            reference.helper_end_seconds=next_end;
            next_plan=helper::plan(options,helper::placement_band(config,occupied_bandwidth_hz),
                physical_rate,0,outer_dsss_transmit_gain(config),&reference);
        }
        helper::Generator next_generator(options,next_plan,next_epoch,true,time_key);
        plan=next_plan;
        sigma=std::sqrt(nominal_signal_power*std::pow(10.,-channel.snr_db/10))*plan.data_gain*std::sqrt(factor);
        source=&transmitter;total=next_total;
        helper_origin=next_origin;helper_epoch=next_epoch;helper_start=next_start;helper_end=next_end;
        startup=next_startup;
        burst_rotation=phase_rotation(tau*config.carrier_hz*rate*(static_cast<long double>(origin)+startup)/config.sample_rate);
        generator=std::move(next_generator);
        // Observation only: source-owned valid support excludes independent
        // suppression cover. It is never passed to the receiver as END or
        // length authority. Preserve the fractional channel map for audits.
        audio::trace::record("simulation_source_binding",[&](auto& f) {
            f.add("logical_rate",config.sample_rate);f.add("physical_rate",physical_rate);
            f.add("physical_factor",factor);f.add("logical_origin",origin);
            f.add("logical_startup",startup);f.add("source_rate_ratio",rate);
            f.add("source_physical_output_origin",physical_origin);
            f.add("source_total_samples",total);
            f.add("source_physical_valid_payload_end",transmitter.physical_valid_payload_end());
            f.add("logical_filter_group_delay",group_delay());
        });
    }
    void flush_observer() {
        if(observer&&observer_count)observer(std::span(observer_buffer).first(observer_count),physical_rate,physical_received-observer_count);
        observer_count=0;
    }
    void validate_retune_drift_capture(double rf) {
        // This optional laboratory model can run indefinitely. Check both
        // frequency endpoints of each affine dwell before exposing any PCM or
        // mapping for it; a finite initial passband is not an unlimited lease.
        const auto clock_rate=1+channel.clock_error_ppm*1e-6;
        const auto half=occupied_bandwidth_hz/2;
        const auto derivative=retunes->sideband_sign*rf*retunes->rf_fractional_slew_per_second;
        for(const auto sample:std::array{physical_received,physical_received+retune_samples}) {
            const auto offset=channel.frequency_offset_hz*(rf/retunes->rf_shift_hz[0])+
                derivative*(static_cast<long double>(sample)/physical_rate);
            const auto low=(config.carrier_hz-half)*clock_rate+offset;
            const auto high=(config.carrier_hz+half)*clock_rate+offset;
            const auto helper_low=(plan.low_frequency_hz-150)*clock_rate+offset;
            const auto helper_high=(plan.frequency_hz+150)*clock_rate+offset;
            const auto full_low=std::min(low,helper_low),full_high=std::max(high,helper_high);
            if(!std::isfinite(offset)||!std::isfinite(full_low)||!std::isfinite(full_high)||
               full_low<=-static_cast<double>(physical_rate)/2+10 || full_high>=physical_rate/2.-10 ||
               (capture_domain && (full_low<capture_domain->minimum_hz || full_high>capture_domain->maximum_hz)) ||
               (!capture_domain && (std::abs(offset)>=config.sample_rate/2. || low<0 || high>config.sample_rate/2.)))
                throw Error("sampled RF drift leaves its actual capture passband");
            for(unsigned i=0;i<=64;++i) {
                const auto frequency=low+(high-low)*i/64;
                const auto gain=std::abs(response(static_cast<double>(frequency)));
                if(gain<.999 || gain>1.001) {
                    logical_payload_preserved=false;
                    if(!capture_domain)throw Error("sampled RF drift leaves the ordinary payload filter passband");
                }
            }
            if(!capture_domain)for(const auto center:std::array{plan.low_frequency_hz,plan.auxiliary_frequency_hz,plan.frequency_hz})
                for(unsigned i=0;i<=120;++i) {
                    const auto frequency=(center-150+300.*i/120)*clock_rate+offset;
                    const auto alias=std::abs(std::remainder(frequency,static_cast<long double>(config.sample_rate)));
                    if(std::abs(frequency)<=config.sample_rate/2. || alias<low || alias>high)continue;
                    const auto gain=std::abs(response(static_cast<double>(frequency)));
                    if(gain*gain*plan.effective_power_w/options.data_power_w>1e-6)
                        throw Error("sampled RF drift aliases Helper modulation into the ordinary payload passband");
                }
        }
    }
    void begin_rf_segment() {
        if(!retunes || physical_received%retune_samples)return;
        // Deliver the preceding ADC interval before changing its metadata.
        flush_observer();
        const auto hop=physical_received/retune_samples;
        const auto rf=retunes->rf_shift_hz[hop%retunes->rf_shift_hz.size()];
        if(retune_samples>UINT64_MAX-physical_received)throw Error("RF retune sample counter overflow");
        if(retunes->rf_fractional_slew_per_second!=0)validate_retune_drift_capture(rf);
        rf_segment={physical_received,physical_received+settling_samples,
            physical_received+retune_samples,hop+1,rf,retunes->sideband_sign};
        const auto fraction=(static_cast<double>(mix(retunes->phase_seed^hop)>>11)+.5)/9007199254740992.;
        retune_rotation=phase_rotation(tau*(fraction-.5));
        // The ordinary oscillator already contributes the base configured CFO.
        // Apply only its RF-dependent increment to the complete analytic mix.
        retune_step=phase_rotation(tau*channel.frequency_offset_hz*(rf/retunes->rf_shift_hz[0]-1)/physical_rate);
        if(retunes->rf_fractional_slew_per_second!=0) {
            const auto derivative=retunes->sideband_sign*rf*retunes->rf_fractional_slew_per_second;
            // Integrate the affine instantaneous frequency over the NEXT ADC
            // interval. Each hop's starting phase remains independently random.
            const auto middle_time=(static_cast<long double>(physical_received)+.5L)/physical_rate;
            const auto extra=channel.frequency_offset_hz*(rf/retunes->rf_shift_hz[0]-1)+derivative*middle_time;
            retune_step=phase_rotation(tau*extra/physical_rate);
            retune_step_slew=phase_rotation(tau*derivative/(static_cast<long double>(physical_rate)*physical_rate));
        }
        if(rf_observer)rf_observer(rf_segment,physical_rate);
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
    Complex delayed(long double position,std::stop_token stop) {
        const auto delayed_position=position-channel.spread_delay_ms*config.sample_rate*rate/1000;
        if(!source || helper_only || delayed_position<0 || delayed_position>=total)return {};
        return detail::preview_spread_ray(*source,delayed_position,total,[&](std::size_t phase) {
            std::array<double,taps> row{};
            std::copy(coefficients[phase].begin(),coefficients[phase].end(),row.begin());return row;
        },stop);
    }
    float next(StreamingTransmitter* transmitter,std::stop_token stop) {
        float output=0;
        for(;;) {
            begin_rf_segment();
            const auto time=static_cast<long double>(physical_received)/factor;
            const auto position=(time-origin-startup)*rate;
            Complex value{};
            if(transmitter && position>=0&&position<total) {
                const auto payload=interpolate(*transmitter,position,stop);
                if(!helper_only)value=payload;
            }
            if(spread) {
                const auto ray=channel.spread_delay_ms==0?value:(transmitter?delayed(position,stop):Complex{});
                value=spread->process(value,ray);
            }
            value*=burst_rotation;
            if(perturbation)value=perturbation->process(value);
            if(transmitter) {
                auto rendered_helper_end=helper_end;
                if(lab_helper_end_sample) {
                    const auto cutoff=helper_origin+
                        (static_cast<long double>(*lab_helper_end_sample)/factor-origin-startup)*rate/config.sample_rate;
                    rendered_helper_end=std::min(rendered_helper_end,cutoff);
                }
                value=generator->mix_analytic(value,helper_origin+position/config.sample_rate,physical_rate,
                    rendered_helper_end,helper_start);
                value*=oscillator;
                if(retunes) {
                    value*=retune_rotation;
                    // Model an unavailable receiver during retuning. Source
                    // and ADC dates continue; no sample is removed or redated.
                    if(physical_received<rf_segment.settled_first_sample)value={};
                }
            }
            else if(perturbation || spread)value*=oscillator;
            const auto pcm=static_cast<float>(value.real()+noise());
            if(observer)observer_buffer[observer_count++]=pcm;
            advance();
            if(retunes) {
                retune_rotation*=retune_step;
                if(retunes->rf_fractional_slew_per_second!=0)retune_step*=retune_step_slew;
                if((physical_received&4095U)==0)retune_rotation/=std::max(std::abs(retune_rotation),1e-30);
            }
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
        if(capture_domain&&!observer)
            throw Error("Helper Carrier preconversion capture requires a physical PCM consumer");
        if(!source) {
            attach(transmitter,0,stop);
        } else if(source!=&transmitter||transmitter.samples_emitted()!=buffer_start+buffer_count)
            throw Error("sampled simulation source changed without begin_burst");
        const auto endpoint=transmitter.total_samples();
        if(endpoint>total||endpoint<transmitter.samples_emitted())
            throw Error("sampled simulation source endpoint changed incompatibly");
        total=endpoint;
        update_helper_end();
        prime(&transmitter,stop);
        const auto payload_end=startup+static_cast<long double>(total)/rate;
        const auto helper_end_sample=startup+(helper_end-helper_origin)*config.sample_rate/rate;
        const auto end=std::max(helper_end_sample,payload_end+
            (spread?channel.spread_delay_ms*config.sample_rate/1000:0));
        const bool reach_payload_end=static_cast<long double>(received-origin)<payload_end;
        std::size_t written=0;
        while(written<output.size()) {
            if((written&4095U)==0&&stop.stop_requested())throw Error("modem operation cancelled");
            if(static_cast<long double>(received-origin)>=end){complete=true;break;}
            output[written++]=next(&transmitter,stop);++received;
            if(written==1)live::detail::simulation_checkpoint(live::detail::SimulationPhase::read_progress,received);
            // Give callers a committed boundary to extend the helper post,
            // even when their output span could hold the entire envelope.
            if(reach_payload_end&&static_cast<long double>(received-origin)>=payload_end)break;
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
    Impl(Config config,ChannelConfig channel,helper::Options options,double bandwidth,HelperEnvelope envelope,
         std::optional<PreconversionCapture> capture) {
        static_assert(sizeof(Impl)+sizeof(SampledLegacy)+sizeof(SampledSimulationChannel)+
            SampledSimulationChannel::auxiliary_workspace_bound+
            SampledSimulationChannel::helper_cohort_workspace_bound+sizeof(Perturbation)+sizeof(SpreadRay)<=SampledSimulationChannel::workspace_bound);
        static_assert(sizeof(Impl)+sizeof(SampledPhysical)+sizeof(SampledSimulationChannel)+
            SampledSimulationChannel::auxiliary_workspace_bound+
            SampledSimulationChannel::physical_helper_cohort_workspace_bound+sizeof(Perturbation)+sizeof(SpreadRay)<=SampledSimulationChannel::workspace_bound);
        if(helper::effective_helper_power(options)>0)physical=std::make_unique<SampledPhysical>(config,channel,std::move(options),bandwidth,envelope,capture);
        else {
            if(capture)throw Error("preconversion capture requires an enabled Helper Carrier");
            legacy=std::make_unique<SampledLegacy>(config,channel,std::move(options),bandwidth);
        }
    }
};

SampledSimulationChannel::SampledSimulationChannel(Config config,ChannelConfig channel,helper::Options helper,double helper_bandwidth,HelperEnvelope envelope,
    std::optional<PreconversionCapture> capture):impl_(std::make_unique<Impl>(config,channel,std::move(helper),helper_bandwidth,envelope,capture)){}
std::uint32_t SampledSimulationChannel::planned_physical_sample_rate(const Config& config,const helper::Options& helper,
        double bandwidth,std::optional<PreconversionCapture> capture) {
    if(!(helper::effective_helper_power(helper)>0)) {
        if(capture)throw Error("preconversion capture requires an enabled Helper Carrier");
        validate(config);return config.sample_rate;
    }
    return select_sampled_physical_clock(config,helper,bandwidth,capture).rate;
}
SampledSimulationChannel::~SampledSimulationChannel()=default;
SampledSimulationChannel::SampledSimulationChannel(SampledSimulationChannel&&)noexcept=default;
SampledSimulationChannel& SampledSimulationChannel::operator=(SampledSimulationChannel&&)noexcept=default;
void SampledSimulationChannel::begin_burst(bool helper_only,HelperEnvelope envelope){
    if(helper_only&&!impl_->physical)throw Error("Helper-only sampled output requires an enabled helper");
    if(impl_->physical) {impl_->physical->begin(envelope);impl_->physical->helper_only=helper_only;return;}
    impl_->legacy->helper_only=helper_only;impl_->legacy->begin();
}
void SampledSimulationChannel::prepare_helper_lead(StreamingTransmitter& source,std::uint64_t additional_samples) {
    if(!impl_->physical)throw Error("Helper Carrier lead requires an enabled helper");
    impl_->physical->attach(source,additional_samples);
}
void SampledSimulationChannel::extend_helper_tail(double seconds) {
    if(impl_->physical)impl_->physical->extend_tail(seconds);
}
void SampledSimulationChannel::set_helper_callsign(std::string callsign) {
    auto& current=impl_->physical?impl_->physical->options:impl_->legacy->helper_options;
    auto options=current;options.callsign=std::move(callsign);helper::validate(options);current=std::move(options);
}
void SampledSimulationChannel::set_helper_time_key(std::optional<Crypto> key) {
    if(impl_->physical)impl_->physical->time_key=std::move(key);else impl_->time_key=std::move(key);
}
void SampledSimulationChannel::set_helper_power_reference(helper::PowerReference reference) {
    if(!impl_->physical)throw Error("Helper Carrier power reference requires an enabled physical helper");
    auto& s=*impl_->physical;
    if(s.source)throw Error("Helper Carrier power reference must precede source binding");
    s.plan=helper::plan(s.options,helper::placement_band(s.config,s.occupied_bandwidth_hz),
        s.physical_rate,0,outer_dsss_transmit_gain(s.config),&reference);
    s.sigma=std::sqrt(nominal_signal_power*std::pow(10.,-s.channel.snr_db/10))*s.plan.data_gain*std::sqrt(s.factor);
    s.power_reference_prepared=true;
}
void SampledSimulationChannel::set_physical_observer(PhysicalObserver observer) {
    if(impl_->physical)impl_->physical->observer=std::move(observer);else impl_->observer=std::move(observer);
}
void SampledSimulationChannel::set_physical_rf_observer(PhysicalRfObserver observer) {
    if(!impl_->physical)throw Error("actual RF observation requires physical Helper capture");
    if(impl_->physical->physical_received)throw Error("actual RF observer must precede capture");
    impl_->physical->rf_observer=std::move(observer);
}
void SampledSimulationChannel::set_rf_retune_simulation(const RfRetuneSimulation& value) {
    if(!impl_->physical || impl_->physical->physical_received)
        throw Error("RF retune simulation requires fresh physical Helper capture");
    if(!std::isfinite(value.dwell_seconds)||!std::isfinite(value.settling_seconds)||
       !std::isfinite(value.rf_fractional_slew_per_second)||
       value.dwell_seconds<=0||value.dwell_seconds>10||value.settling_seconds<0||
       value.settling_seconds>=value.dwell_seconds || (value.sideband_sign!=1&&value.sideband_sign!=-1))
        throw Error("invalid sampled RF retune timing or sideband");
    for(const auto rf:value.rf_shift_hz)if(!std::isfinite(rf)||rf<=0)
        throw Error("invalid sampled RF retune frequency");
    auto& physical=*impl_->physical;
    const auto dwell=value.dwell_seconds*physical.physical_rate;
    const auto settle=value.settling_seconds*physical.physical_rate;
    if(dwell<1||std::abs(dwell-std::round(dwell))>1e-7)
        throw Error("sampled RF dwell is not an integral physical aperture");
    physical.retunes=value;physical.retune_samples=static_cast<std::uint64_t>(std::llround(dwell));
    physical.settling_samples=static_cast<std::uint64_t>(std::ceil(settle));
}
void SampledSimulationChannel::set_lab_helper_end_sample(std::optional<std::uint64_t> sample) {
    if(!impl_->physical || impl_->physical->physical_received)
        throw Error("laboratory Helper loss requires fresh physical Helper capture");
    impl_->physical->lab_helper_end_sample=sample;
}
std::uint32_t SampledSimulationChannel::physical_sample_rate()const {return impl_->physical?impl_->physical->physical_rate:impl_->legacy->config.sample_rate;}
audio::CaptureTransportRecipe SampledSimulationChannel::capture_transport()const noexcept {
    if(!impl_->physical)return {};
    // prime() discards the exact128*(F-1)/F centered-FIR outputs once.
    // Afterwards logical n has raw support [Fn-128(F-1),Fn+128(F-1)].
    // begin_burst never resets those filters or either stream counter.
    return {audio::CaptureTransportKind::sampled_physical_halfband,
        impl_->physical->physical_rate,impl_->physical->config.sample_rate,0,0};
}
std::size_t SampledSimulationChannel::helper_cohort_workspace_bytes()const {
    return impl_->physical?physical_helper_cohort_workspace_bound:helper_cohort_workspace_bound;
}
bool SampledSimulationChannel::logical_payload_preserved()const {return !impl_->physical||impl_->physical->logical_payload_preserved;}
std::uint64_t SampledSimulationChannel::physical_received_samples()const {return impl_->physical?impl_->physical->physical_received:impl_->legacy->received;}
std::uint64_t SampledSimulationChannel::received_samples()const{return impl_->physical?impl_->physical->received:impl_->legacy->received;}
std::optional<std::uint64_t> SampledSimulationChannel::payload_begin_sample()const {
    if(impl_->physical) {
        const auto& s=*impl_->physical;
        if(s.source)return s.origin+static_cast<std::uint64_t>(std::ceil(s.startup));
    } else {
        const auto& s=*impl_->legacy;
        if(s.source)return s.origin+static_cast<std::uint64_t>(std::ceil(s.startup));
    }
    return {};
}
std::optional<std::uint64_t> SampledSimulationChannel::payload_end_sample()const {
    if(impl_->physical) {
        const auto& s=*impl_->physical;
        if(s.source)return s.origin+static_cast<std::uint64_t>(std::ceil(s.startup+static_cast<long double>(s.total)/s.rate));
    } else {
        const auto& s=*impl_->legacy;
        if(s.source)return s.origin+static_cast<std::uint64_t>(std::ceil(s.startup+static_cast<long double>(s.total)/s.rate));
    }
    return {};
}
bool SampledSimulationChannel::payload_finished()const {
    const auto end=payload_end_sample();return end&&received_samples()>=*end;
}
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
std::size_t SampledSimulationChannel::working_bytes()const{return sizeof(Impl)+sizeof(*this)+(impl_->physical?
    sizeof(SampledPhysical)+(impl_->physical->perturbation?sizeof(Perturbation):0)+(impl_->physical->spread?sizeof(SpreadRay):0):
    sizeof(SampledLegacy)+(impl_->legacy->perturbation?sizeof(Perturbation):0)+(impl_->legacy->spread?sizeof(SpreadRay):0));}
SampledSimulationChannel::PerturbationMetadata SampledSimulationChannel::perturbation_metadata()const {
    const auto& channel=impl_->physical?impl_->physical->channel:impl_->legacy->channel;
    const auto* p=impl_->physical?impl_->physical->perturbation.get():impl_->legacy->perturbation.get();
    PerturbationMetadata result;result.seed=channel.seed;result.doppler_reference_hz=channel.spread_doppler_hz;
    result.slow_depth=channel.perturbation_slow_depth;result.fast_depth=channel.perturbation_fast_depth;
    if(p) {result.enabled=true;result.rates_hz=p->rates_hz;result.band_centers_hz=p->band_centers_hz;
        result.band_width_hz=p->band_width_hz;result.initial_phases_radians=p->initial_phases;}
    return result;
}
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
            auto reference=helper::transmit_power_reference(s.config,source,origin/s.config.sample_rate,
                s.config.stream_epoch,impl_->time_key);
            reference.punctuate=false;reference.helper_start_seconds=s.helper_origin;
            reference.helper_end_seconds=s.helper_origin+static_cast<long double>(s.total)/s.config.sample_rate;
            s.helper_plan=helper::plan(s.helper_options,helper::placement_band(s.config),s.config.sample_rate,0,
                outer_dsss_transmit_gain(s.config),&reference);
            s.sigma=std::sqrt(nominal_signal_power*std::pow(10.,-s.channel.snr_db/10))*s.helper_plan->data_gain;
            s.helper_generator.emplace(s.helper_options,*s.helper_plan,origin/s.config.sample_rate,false,impl_->time_key);
        }
        const auto duration=std::ceil(s.startup+static_cast<long double>(s.total)/s.rate+
            (s.spread?static_cast<long double>(s.channel.spread_delay_ms)*s.config.sample_rate/1000:0));
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
        const auto delay=s.spread?s.channel.spread_delay_ms*s.config.sample_rate*s.rate/1000:0;
        if(position>=s.total+delay){s.complete=true;break;}
        auto value=position<0 || position>=s.total?Complex{}:s.interpolate(source,position,stop);
        if(s.spread) {
            const auto delayed=delay==0?value:detail::preview_spread_ray(source,position-delay,s.total,[&](std::size_t phase) {
                return phase<s.stored_phases?s.coefficients[phase]:s.make_coefficients(phase);
            },stop);
            value=s.spread->process(value,delayed);
        }
        if(s.perturbation)value=s.perturbation->process(value*s.burst_rotation);
        if(position>=0 && s.helper_generator) {
            value=s.helper_generator->mix_analytic(s.helper_only?Complex{}:value,s.helper_origin+position/s.config.sample_rate,s.config.sample_rate,
                s.helper_origin+static_cast<long double>(s.total)/s.config.sample_rate,s.helper_origin);
        }
        if(position>=0 || s.perturbation || s.spread)
            value=s.perturbation?value*s.oscillator:value*s.oscillator*s.burst_rotation;
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
        Complex tail{};
        // Source lifetime belongs to the caller; read_noise never dereferences
        // a transmitter retained from an earlier read. read() drains the ray.
        if(s.spread)tail=s.spread->process({},{});
        if(s.perturbation)tail=s.perturbation->process(tail*s.burst_rotation);
        if(s.perturbation)tail*=s.oscillator;
        else if(s.spread)tail*=s.oscillator*s.burst_rotation;
        output[i]=static_cast<float>(tail.real()+s.noise());s.advance();
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
        if(channel.spread_delay_ms!=0 || channel.spread_doppler_hz!=0)
            throw Error("accelerated simulation does not support propagation spread; use the sampled simulation channel");
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
