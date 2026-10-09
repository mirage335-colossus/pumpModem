#include "datapump/clock_sync.hpp"
#include "datapump/utc_timing.hpp"
#include "datapump/resampler.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include <cmath>
#include <numbers>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F function) {try {function();}catch(const datapump::Error&){return;}throw std::runtime_error("unsafe timing accepted");}
void shaped_timing_error(datapump::modem::Config config) {
    using namespace datapump::modem;
    config.scramble=true;config.stream_epoch=1800000100;
    for(std::size_t i=0;i<config.spreading_seed.size();++i)config.spreading_seed[i]=static_cast<std::uint8_t>(i*31+7);
    for(std::size_t i=0;i<config.dsss_seed.size();++i)config.dsss_seed[i]=static_cast<std::uint8_t>(i*17+3);
    const datapump::Bytes bits{0,1,0};
    PatternTransmitter transmitter(bits,config,config.stream_epoch,0,false);
    std::vector<float> raw(static_cast<std::size_t>(transmitter.total_samples()));
    require(transmitter.read(raw)==raw.size(),"finite waveform fixture incomplete");
    PatternCode code(config,config.stream_epoch);
    const auto padding=pattern_pulse_padding_samples(config);
    const double amplitude=std::sqrt(2*nominal_signal_power);
    const auto ideal=[&](double sample) {
        std::complex<double> envelope{};
        for(std::size_t bit=0;bit<bits.size();++bit)
            envelope+=code.shaped_value(bit*code.chips_per_symbol(),bits[bit],
                sample-static_cast<double>(padding)-static_cast<double>(bit*code.symbol_samples()));
        return (pattern_limit_pcm(amplitude*envelope)*std::polar(1.,
            2*std::numbers::pi*config.carrier_hz*sample/config.sample_rate)).real();
    };
    datapump::audio::Resampler resampler(config.sample_rate,config.sample_rate,true);
    resampler.initialize_timing(.375,0);
    const auto slew=datapump::audio::conservative_timing_slew(config.sample_rate,
        static_cast<double>(code.symbol_samples())/config.sample_rate);
    resampler.set_rate_correction(.0001,slew);
    std::array<float,257> output{};
    std::size_t offset=0,produced=0;double energy=0,observed=0,dot=0,error=0;
    while(!resampler.finished()) {
        const auto count=std::min<std::size_t>(1031,raw.size()-offset);
        const auto progress=resampler.process(std::span(raw).subspan(offset,count),output,offset+count==raw.size());
        require(progress.consumed || progress.produced || resampler.finished(),"finite waveform ASRC stalled");
        offset+=progress.consumed;
        for(std::size_t i=0;i<progress.produced;++i,++produced) {
            const double t=static_cast<double>(produced)/config.sample_rate;
            const auto ramp=std::min(t,.0001/slew);
            const double source=.375+config.sample_rate*(t+.5*slew*ramp*ramp+.0001*(t-ramp));
            const auto reference=ideal(source),sample=static_cast<double>(output[i]);
            energy+=reference*reference;observed+=sample*sample;dot+=reference*sample;
            error+=(reference-sample)*(reference-sample);
        }
    }
    const auto mismatch_db=-10*std::log10(dot*dot/(energy*observed));
    const auto relative_error=error/energy;
    std::cout<<"ASRC finite-pulse case Fs="<<config.sample_rate<<" carrier="<<config.carrier_hz
        <<" DSSS="<<config.dsss_factor<<" symbol_seconds="<<static_cast<double>(code.symbol_samples())/config.sample_rate
        <<" samples="<<produced<<" waveform_mismatch_db="<<mismatch_db<<" error_energy_ratio="<<relative_error<<'\n';
    // Conditional waveform-norm regression, including every boundary/tail and
    // the actual nonlinear amplitude limiter. Not an acquisition-bank PD/PFA
    // or receiver noise-covariance qualification.
    require(mismatch_db<.01 && relative_error<.001,"UTC interpolation distorts finite shaped/limited waveform");
}
}
int main() {try {
    using namespace datapump::audio;
    constexpr long double epoch=1800000000.L;
    for(const auto fs:{64U,6000U,48000U})for(const auto ppm:{-200.,0.,200.})for(const auto age:{-7.,0.,8.}) {
        const datapump::clock_sync::Policy policy{.001,.4,2.564};
        const auto tau=(1+ppm*1e-6)/fs;
        const auto anchor=epoch+age;
        const auto map=datapump::clock_sync::arrival_map(policy,epoch,anchor,-173,fs,tau,.002,.1,fs-1,.5);
        require(map.has_value(),"valid modeled timing map unexpectedly unavailable");
        for(const auto phase:{0U,fs/3,fs-1})for(const auto rate:{-.002L,.002L})
            for(const auto arrival_error:{-.802L,.802L}) {
                const auto actual=-173+(epoch+policy.offset_seconds-anchor+
                    static_cast<long double>(phase)/fs+arrival_error)/(tau+rate/fs);
                const auto predicted=map->origin_samples+map->phase_scale*phase;
                require(std::abs(actual-predicted)<=map->half_width_samples,
                    "modeled UTC interval lost a delay/slope/phase corner");
            }
        require(map->half_width_samples<fs,"small region unexpectedly restored the full legacy window");
    }
    require(!datapump::clock_sync::arrival_map({},epoch,epoch,0,64,1./64,1,.1,63),
        "singular inverse-slope model authorized timing pruning");
    auto invalid_policy=datapump::clock_sync::Policy{};
    invalid_policy.accuracy_seconds=std::numeric_limits<double>::quiet_NaN();
    require(!datapump::clock_sync::arrival_map(invalid_policy,epoch,epoch,0,64,1./64,0,.1,63),
        "nonfinite GPS accuracy produced a zero-width prior");
    // Retain the original broad-budget cases, and test the new adjustable
    // default separately rather than shrinking their long-horizon assertions.
    EstimatedDeviceTiming assumed;assumed.maximum_utc_error_seconds=.5;
    require(assumed.initial_correction({},1,.1,.0003)==0.,"short stream needlessly waits for a rate estimate");
    require(!assumed.initial_correction({},14400,.1,.0003),"long stream discarded accumulated clock error");
    require(!assumed.initial_correction(LinkRateInterval{.00005,.00015,40},14400,.1,.0003),
        "imprecise rate estimate authorized long-message UTC timing");
    const auto prepared=assumed.initial_correction(LinkRateInterval{.00009,.00011,400},14400,.1,.0003);
    require(prepared && std::abs(*prepared-.0001)<1e-15,"finite timing preparation lost measured rate");
    require(!assumed.initial_correction(LinkRateInterval{.00029,.00031,40},14400,.1,.0003),
        "timing preparation exceeded the receiver correction domain");
    rejects([&]{assumed.initial_correction(LinkRateInterval{.00029,.00031,400},14400,.1,.0003);});
    rejects([&]{assumed.initial_correction(LinkRateInterval{.0004,.0005,40},14400,.1,.0003);});
    EstimatedDeviceTiming ordinary;
    require(ordinary.maximum_utc_error_seconds==.03 &&
        ordinary.initial_correction({},1,.013,.0003)==0.,"30 ms audio model needlessly delays a short waveform");
    rejects([&]{ordinary.initial_correction({},1,.1,.0003);});
    const auto narrow=datapump::clock_sync::arrival_map({},epoch,epoch,0,48000,1./48000,0,.03,0,.03);
    const auto wide=datapump::clock_sync::arrival_map({},epoch,epoch,0,48000,1./48000,0,.03,0,.08);
    require(narrow && wide && std::abs((wide->half_width_samples-narrow->half_width_samples)/48000-.05)<1e-12,
        "editable audio error did not propagate exactly into arrival coverage");
    TimePrediction boundary{1.015L,.014,1./48000,TimingQuality::bounded,0};
    require(presentation_correction(boundary,1,48000,.03).uncertainty_seconds==.014,
        "valid 30 ms presentation allowance rejected");
    boundary.utc_seconds=1.017L;
    rejects([&]{presentation_correction(boundary,1,48000,.03);});
    require(presentation_correction(boundary,1,48000,.04).uncertainty_seconds==.014,
        "larger selected audio error was ignored");
    for(const auto error:{0.,-1.,61.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
        rejects([&]{datapump::clock_sync::validate_audio_error(error);});
    for(const auto horizon:{1.,3600.,86400.})for(const auto initial:{-.0001,.0001}) {
        constexpr long double origin=.375L/48000;
        constexpr double limit=.01/48000;
        AffineTimingGuard guard(origin,initial,horizon,limit);
        long double source=origin,elapsed=0;
        double current=initial;
        for(unsigned i=0;i<1000;++i) {
            const auto target=guard.constrain(i%2?.001:-.001,source,elapsed,current);
            // A trapezoidal rate transition models one finite resampler ramp.
            const auto next=current+.5*(target-current);
            const auto dt=static_cast<long double>(horizon)/1000;
            source+=(1.L+(current+next)/2)*dt;elapsed+=dt;current=next;
            // Use the exact final coordinate to avoid fixture summation error
            // being mistaken for a caller that exceeds its declared duration.
            guard.verify(source,i==999?horizon:elapsed);
        }
        rejects([&]{guard.verify(source+.001,horizon);});
        rejects([&]{guard.verify(source,horizon+1);});
    }
    // A sustained correction spends the whole warp budget. Intermediate
    // coordinate rounding must not spend its reserved allowance a second time
    // or shrink the cap below the rate already safely installed.
    for(const auto horizon:{1.,3600.,86400.})for(const auto sign:{-1.,1.}) {
        constexpr long double origin=.375L/48000;
        constexpr double initial=.0001,limit=.01/48000,roundoff=limit/10;
        const auto radius=(limit-roundoff)/horizon;
        AffineTimingGuard guard(origin,initial,horizon,limit,roundoff);
        const auto current=guard.constrain(sign*.001,origin,0,initial);
        require(std::abs(current-initial-sign*radius)<2e-20,"finite timing cap changed its complete-horizon bound");
        for(const auto fraction:{.5L,.99L,1.L}) {
            const auto elapsed=fraction*horizon;
            const auto source=origin+(1.L+current)*elapsed+sign*.9L*roundoff;
            guard.verify(source,elapsed);
            require(guard.constrain(sign*.001,source,elapsed,current)==current,
                "reserved coordinate roundoff falsely exhausted finite timing continuation");
        }
        rejects([&]{guard.verify(origin+(1.L+initial)*horizon+sign*1.01L*limit,horizon);});
    }
    // A huge arbitrary codec offset cannot enter the rate calculation. The
    // interval contains every deterministic endpoint error, including jitter
    // with the same sign at every intermediate observation.
    for(const auto ppm:{-500.,0.,500.})for(const auto offset:{0.L,12345.L})
        for(const auto endpoint:{-1.,1.}) {
            LinkRateTimeline rate;
            for(unsigned i=0;i<=40;++i) {
                const long double elapsed=i*.1L;
                const auto jitter=(i?endpoint:-endpoint)*2e-6L;
                rate.observe({offset+elapsed*(1+ppm*1e-6L),epoch+elapsed+jitter,elapsed,
                    42e-9,2e-6,17});
            }
            const auto interval=rate.interval();
            const auto truth=1/(1+ppm*1e-6)-1;
            require(interval && interval->lower<=truth && truth<=interval->upper,
                "link clock interval lost true rate at timestamp error boundary");
            require(interval->uncertainty()>1e-6,"link clock fit averaged away bounded jitter");
            rejects([&]{rate.observe({offset+5,epoch+6,5,42e-9,2e-6,17});});
        }
    LinkRateTimeline unresolved;
    unresolved.observe({0,epoch,0,.1,.1,0});
    unresolved.observe({.05L,epoch+.05L,.05L,.1,.1,0});
    require(!unresolved.interval(),"short imprecise timestamps manufactured rate confidence");
    for(const auto ppm:{-500.,-100.,0.,100.,500.}) {
        UtcTimeline timeline(48000);
        for(unsigned i=0;i<80;++i) {
            const long double t=i*.05L;
            timeline.observe({t*48000*(1+ppm*1e-6L),epoch+t,t,2e-7,2e-8,TimingQuality::bounded,4});
        }
        const auto prediction=timeline.predict(4.1L*48000*(1+ppm*1e-6L));
        require(std::abs(prediction.utc_seconds-epoch-4.1L)<2e-8,"UTC fit lost fractional timestamp accuracy");
        require(prediction.quality==TimingQuality::bounded,"bounded provider was downgraded");
        const auto correction=presentation_correction(prediction,epoch+4.1L,48000);
        require(std::abs(correction.rate_fraction-(1/(1+ppm*1e-6)-1))<1e-8,"DAC correction has wrong sign or scale");
    }
    UtcTimeline timeline(48000);
    timeline.observe({0,epoch,0,.1,1e-6,TimingQuality::estimated,9});
    timeline.observe({2400,epoch+.05L,.05L,.1,1e-6,TimingQuality::estimated,9});
    require(timeline.predict(4800).quality==TimingQuality::estimated,"estimated device timing promoted to a bound");
    require(timeline.predict(4800).uncertainty_seconds>=.1,"fit discarded provider uncertainty");
    rejects([&] {timeline.observe({4800,epoch+1.1L,.1L,.1,1e-6,TimingQuality::estimated,9});});
    rejects([&] {timeline.observe({4800,epoch-.1L,.1L,.1,1e-6,TimingQuality::estimated,9});});
    rejects([&] {timeline.observe({4800,epoch+.1L,.1L,.1,1e-6,TimingQuality::estimated,10});});
    rejects([&] {presentation_correction(timeline.predict(4800),epoch,48000,.01);});
    require(conservative_timing_slew(48000,3600)<conservative_timing_slew(48000,.1)*1e-6,
        "timing slew uses audio block length instead of full coherent symbol");
    const auto policy=datapump::clock_sync::parse("GPS_1ms-400ms_region-2564ms_offset");
    require(policy && std::abs(policy->half_window_seconds()-.202)<1e-12,"clock window changed total-region semantics");
    datapump::modem::Config waveform;waveform.sample_rate=4800;waveform.carrier_hz=1200;waveform.bandwidth_hz=1200;
    shaped_timing_error(waveform);
    waveform.bandwidth_hz=120;waveform.dsss_factor=10;shaped_timing_error(waveform);
    waveform.sample_rate=64;waveform.carrier_hz=.5;waveform.bandwidth_hz=.5;waveform.dsss_factor=1;
    shaped_timing_error(waveform);
    std::cout<<"UTC timestamp, uncertainty, rate direction and discontinuity tests passed\n";
    return 0;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}}
