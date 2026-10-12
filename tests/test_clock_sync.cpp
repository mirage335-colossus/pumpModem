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
void native_timestamp_intervals() {
    using namespace datapump::audio;
    constexpr auto mode=UtcTimeline::Prediction::timestamp_intersection;
    constexpr long double epoch=1800000000.L;
    constexpr double radius=.0130208333333333;
    // Exact reproduction: valid +10,+10,-10 ms pointer errors on a steady
    // clock inflate the old fitted radius beyond the 25 ms half-width of the
    // operator's causal 0..50 ms lag interval. Keep the old failure as evidence.
    UtcTimeline old(48000),native(48000,.001,mode);
    for(const auto [frame,time]:{std::pair{4320.L,.1L},std::pair{6720.L,.15L},std::pair{10080.L,.2L}}) {
        const FrameTimestamp stamp{frame,epoch+time,time,radius,1e-6,TimingQuality::estimated,7};
        old.observe(stamp);native.observe(stamp);
    }
    const auto desired=epoch+10080.L/48000;
    rejects([&]{(void)presentation_correction(old.predict(10080),desired,48000,.025);});
    const auto prediction=native.predict(10080);
    const auto corrected=presentation_correction(prediction,desired,48000,.025);
    require(std::abs(corrected.error_seconds)+prediction.uncertainty_seconds<.016,
        "valid native pointer intervals still inflate the UTC error");
    require(prediction.uncertainty_seconds>=radius && prediction.quality==TimingQuality::estimated,
        "native timestamp model erased provider uncertainty or upgraded quality");
    std::cout<<"Native UTC counterexample: old radius_ms="<<old.predict(10080).uncertainty_seconds*1000
        <<" new radius_ms="<<prediction.uncertainty_seconds*1000
        <<" center_error_ms="<<corrected.error_seconds*1000<<'\n';
    // Includes common codec offset (never averages down), fractional frames,
    // long epoch origins, clock envelope edges, arbitrary block cadence,
    // extrapolation, and multiple wraps of the bounded observation history.
    for(const auto fs:{8000U,48000U,192000U})for(const double ppm:{-2000.,-1000.,0.,1000.,2000.})
        for(const long double offset:{-.002L,0.L,.002L}) {
            UtcTimeline timeline(fs,.001,mode);long double elapsed=0;
            for(unsigned i=0;i<256;++i) {
                elapsed+=.05L+.003L*(i%5);
                const auto jitter=i%3==2?-.010L:.010L;
                const auto frame=elapsed*fs*(1-ppm*1e-6L)+jitter*fs+.375L;
                timeline.observe({frame,epoch+elapsed+offset,elapsed+offset,radius,1e-6,TimingQuality::estimated,12});
                for(const long double horizon:{-.025L,0.L,.05L,.25L}) {
                    const auto target=frame+horizon*fs;
                    const auto actual_scale=1/(1-ppm*1e-6L);
                    const auto actual=epoch+target*actual_scale/fs;
                    const auto p=timeline.predict(target);
                    require(std::abs(p.utc_seconds-actual)<=p.uncertainty_seconds,
                        "native prediction omitted an admitted timing corner");
                    require(p.uncertainty_seconds>=radius+.002*std::abs(horizon),
                        "native prediction lost provider floor or extrapolation allowance");
                    require(std::abs(p.seconds_per_frame*fs-actual_scale)<=p.rate_uncertainty_fraction+1e-15,
                        "native rate metadata lost an admitted clock envelope edge");
                }
            }
        }
    UtcTimeline contradiction(48000,.001,mode);
    // Each adjacent pair passes the existing continuity gate. Collectively
    // they cannot describe a clock in the admitted envelope.
    for(unsigned i=0;i<4;++i)contradiction.observe({4800.L+i*4896.L,epoch+.1L+i*.1L,
        .1L+i*.1L,.001,1e-6,TimingQuality::estimated,8});
    rejects([&]{(void)contradiction.predict(19488);});
    rejects([&]{native.observe({12480,epoch+.3L,.25L,radius,1e-6,TimingQuality::estimated,7});});
    try {
        (void)presentation_correction({epoch+.04L,.014,1./48000,TimingQuality::estimated},epoch,48000,.025);
        throw std::runtime_error("unsafe native playback accepted");
    } catch(const datapump::Error& e) {
        const std::string message=e.what();
        require(message.find("center error=")!=std::string::npos && message.find("timestamp radius=")!=std::string::npos &&
            message.find("allowed radius=25.000000 ms")!=std::string::npos,"UTC failure omitted numeric timing evidence");
    }
}
void native_query_uncertainty() {
    using namespace datapump::audio;
    constexpr auto mode=UtcTimeline::Prediction::timestamp_intersection;
    constexpr long double epoch=1800000000.L;
    constexpr double shared=.014375; // 8 kHz, 10 ms period, codec/query floor, one frame
    constexpr double query=.003440737362; // isolated slow query consistent with the user report
    constexpr long double center_error=.008709924L;
    UtcTimeline previous(8000,.001,mode),native(8000,.001,mode,shared);
    require(native.observation_uncertainty().maximum==0,"empty timeline invented query uncertainty");
    for(unsigned i=1;i<=16;++i) {
        const auto t=i*.05L;
        const auto bracket=i==3?query:0.;
        const FrameTimestamp observation{t*8000,epoch+t+center_error,t,shared+bracket/2,
            bracket+1e-6,TimingQuality::estimated,9};
        previous.observe(observation);native.observe(observation);
    }
    const auto frame=.8L*8000+817; // user's 102.125 ms prediction horizon
    const auto desired=epoch+frame/8000;
    const auto before=previous.predict(frame),after=native.predict(frame);
    rejects([&]{(void)presentation_correction(before,desired,8000,.025);});
    const auto correction=presentation_correction(after,desired,8000,.025);
    require(std::abs(correction.error_seconds)+after.uncertainty_seconds<.0233 &&
        after.uncertainty_seconds>=shared && after.quality==TimingQuality::estimated,
        "isolated slow query persisted as a shared native clock error");
    require(std::abs(after.utc_seconds-desired)<=after.uncertainty_seconds,
        "native interval omitted the compatible physical clock");
    const auto raw=native.observation_uncertainty();
    require(raw.latest==shared && raw.maximum==shared+query/2,
        "query diagnostic lost complete observation uncertainties");
    std::cout<<"Native query counterexample: previous radius_ms="<<before.uncertainty_seconds*1000
        <<" new radius_ms="<<after.uncertainty_seconds*1000
        <<" center_error_ms="<<correction.error_seconds*1000
        <<" previous_total_ms="<<(std::abs(before.utc_seconds-desired)+before.uncertainty_seconds)*1000
        <<" new_total_ms="<<(std::abs(after.utc_seconds-desired)+after.uncertainty_seconds)*1000<<'\n';
    // No better evidence: the full interval, not just its shared floor, must
    // determine the bound. The decomposition must not force unsafe acceptance.
    UtcTimeline broad(8000,.001,mode,shared);
    for(unsigned i=1;i<=16;++i)broad.observe({i*400.L,epoch+i*.05L,i*.05L,.030,.031251,
        TimingQuality::estimated,9});
    require(broad.predict(6400).uncertainty_seconds>=.030,"wide queries were discarded from the intersection");
    rejects([&]{(void)presentation_correction(broad.predict(6400),epoch+.8L,8000,.025);});
    // Shared offsets do not average down. Include rate edges, fractional
    // frames, both prediction directions and several observation-ring wraps.
    for(const auto fs:{8000U,48000U,192000U})for(const double ppm:{-2000.,0.,2000.})
        for(const long double offset:{-static_cast<long double>(shared),static_cast<long double>(shared)}) {
            UtcTimeline timeline(fs,.001,mode,shared);
            for(unsigned i=1;i<=150;++i) {
                const auto t=i*.05L,position=t*fs*(1-ppm*1e-6L)+.375L;
                const auto bracket=i%13==0?query:0.;
                timeline.observe({position,epoch+t+offset,t,shared+bracket/2,
                    bracket+1e-6,TimingQuality::estimated,9});
                for(const long double horizon:{-.025L,0.L,.102125L,.25L}) {
                    const auto target=position+horizon*fs;
                    const auto truth=epoch+(target-.375L)/fs/(1-ppm*1e-6L);
                    const auto p=timeline.predict(target);
                    require(std::abs(p.utc_seconds-truth)<=p.uncertainty_seconds,
                        "split native uncertainty omitted common offset or rate corner");
                    require(p.uncertainty_seconds>=shared+.002*std::abs(horizon),
                        "split native uncertainty lost its shared provider floor");
                }
            }
        }
    rejects([&]{UtcTimeline invalid(8000,.001,mode,-1.);});
    rejects([&]{UtcTimeline invalid(8000,.001,mode,std::numeric_limits<double>::quiet_NaN());});
    rejects([&]{UtcTimeline invalid(8000,.001,mode,std::numeric_limits<double>::infinity());});
    rejects([&]{UtcTimeline invalid(8000,.001,UtcTimeline::Prediction::fitted,shared);});
    UtcTimeline missing(8000,.001,mode,shared);
    rejects([&]{missing.observe({400,epoch+.05L,.05L,shared-1e-6,1e-6,TimingQuality::estimated,9});});
    require(!missing.ready(),"invalid provider observation mutated empty timing state");
    rejects([&]{native.observe({6800,epoch+.85L+center_error,.85L,shared-1e-6,1e-6,TimingQuality::estimated,9});});
    const auto unchanged=native.predict(frame);
    require(unchanged.utc_seconds==after.utc_seconds && unchanged.uncertainty_seconds==after.uncertainty_seconds,
        "invalid provider observation changed an established timing prediction");
    for(unsigned i=17;i<=67;++i)native.observe({i*400.L,epoch+i*.05L+center_error,i*.05L,
        shared,1e-6,TimingQuality::estimated,9});
    require(native.observation_uncertainty().maximum==shared,
        "query diagnostic retained an evicted observation");
    UtcTimeline contradiction(48000,.001,mode,.001);
    for(unsigned i=0;i<4;++i)contradiction.observe({4800.L+i*4896.L,epoch+.1L+i*.1L,
        .1L+i*.1L,.001,1e-6,TimingQuality::estimated,8});
    rejects([&]{(void)contradiction.predict(19488);});
}
void native_frozen_source_timing() {
    using namespace datapump::audio;
    constexpr auto mode=UtcTimeline::Prediction::timestamp_intersection;
    constexpr long double epoch=1800000000.L;
    constexpr double raw_radius=.014375,shared=.004375,limit=.025;
    UtcTimeline preceding(8000,.001,mode,raw_radius);
    UtcTimeline native(8000,.001,mode,shared,true);
    const FrameTimestamp first{400,epoch+.05L-.010368093L,.05L-.010368093L,raw_radius,1e-6,TimingQuality::estimated,17};
    preceding.observe(first);native.observe(first);
    const auto source_origin=native.predict(400).utc_seconds;
    double worst=0;unsigned old_failures=0,new_failures=0;
    for(unsigned i=2;i<=1000;++i) {
        // Quantized pointer phases recur, without independent jitter or a
        // zero-mean assumption. The SOURCE origin never moves.
        const auto jitter=i%3?.010L:-.010L;
        const auto frame=i*400.L,time=i*.05L;
        const FrameTimestamp stamp{frame,epoch+time+jitter,time+jitter,raw_radius,1e-6,TimingQuality::estimated,17};
        preceding.observe(stamp);native.observe(stamp);
        for(const long double horizon:{-.025L,0.L,.153125L,3.2L}) {
            const auto target=frame+horizon*8000;
            const auto desired=source_origin+(target-400)/8000;
            const auto p=native.predict(target),old=preceding.predict(target);
            require(std::abs(p.utc_seconds-(epoch+target/8000))<=p.uncertainty_seconds,
                "persistent native interval omitted the physical device clock");
            const auto total=std::abs(p.utc_seconds-desired)+p.uncertainty_seconds;
            worst=std::max(worst,static_cast<double>(total));
            require(total<limit && p.uncertainty_seconds>=shared && p.quality==TimingQuality::estimated,
                "native interval moved the immutable source outside Audio50");
            new_failures+=total>limit;
            (void)presentation_correction(p,desired,8000,limit);
            old_failures+=std::abs(old.utc_seconds-desired)+old.uncertainty_seconds>limit;
        }
    }
    require(old_failures>100,"frozen-origin fixture did not retain preceding false-stop evidence");
    std::cout<<"Native frozen-origin history: previous failures="<<old_failures
        <<" new failures="<<new_failures<<" new worst_total_ms="<<worst*1000<<" source_origin_shift_ms=-10.368093\n";
    // A constant unknown period bias may not be averaged away. With no phase
    // diversity the interval stays broad, and an out-of-range clock rejects.
    for(const double bias:{-.010,.010}) {
        UtcTimeline t(8000,.001,mode,shared,true);
        for(unsigned i=1;i<=130;++i)t.observe({i*400.L,epoch+i*.05L+bias,i*.05L,raw_radius,1e-6,TimingQuality::estimated,17});
        require(t.predict(52000).uncertainty_seconds>=raw_radius-1e-8,
            "constant correlated pointer bias was averaged away");
    }
    // Full existing generic clock-envelope endpoints, including UTC slew,
    // shared offsets, fractional positions and extrapolation.
    for(const double drift:{1/(1+.002)-1,0.,1/(1-.002)-1})for(const double offset:{-shared,shared}) {
        UtcTimeline t(48000,.001,mode,shared,true);
        for(unsigned i=1;i<=300;++i) {
            const auto frame=i*2400.L+.375L;
            const auto truth=epoch+frame/48000*(1+drift);
            const auto jitter=i%3==0?.010L:-.010L;
            t.observe({frame,truth+offset+jitter,frame/48000*(1+drift)+offset+jitter,
                raw_radius,1e-6,TimingQuality::estimated,17});
            for(const long double horizon:{-.025L,0.L,.153125L,3.2L}) {
                const auto target=frame+horizon*48000;
                const auto p=t.predict(target);
                const auto actual=epoch+target/48000*(1+drift);
                require(std::abs(p.utc_seconds-actual)<=p.uncertainty_seconds,
                    "native rate propagation omitted a fractional/offset endpoint");
            }
        }
    }
    // Informative constraints survive history eviction; broad-but-compatible
    // later observations cannot reset either source origin or the rate budget.
    UtcTimeline persistent(8000,.001,mode,shared,true),recent(8000,.001,mode,shared);
    for(unsigned i=1;i<=140;++i) {
        const auto time=i*.001L;
        const auto radius=i<=2?shared:.030;
        const auto bias=i<=2?0.L:.010L;
        const FrameTimestamp value{i*8.L,epoch+time+bias,time+bias,radius,1e-6,TimingQuality::estimated,17};
        persistent.observe(value);recent.observe(value);
    }
    require(persistent.predict(1120).uncertainty_seconds<.005 && recent.predict(1120).uncertainty_seconds>.029,
        "persistent phase constraint was forgotten at ring wrap");
    require(persistent.predict_without_history(1120+48000).uncertainty_seconds>.042,
        "silence restart incorrectly retained refined phase evidence during qualification");
    // Continuous UTC slew is still admitted; GUI oscillator precision must not
    // silently replace the native provider's existing generic rate envelope.
    for(const double slew:{-.0005,.0005}) {
        UtcTimeline t(8000,.001,mode,shared,true);
        for(unsigned i=1;i<=160;++i) {
            const auto frame=i*400.L;
            const auto steady=frame/8000*1.0001L;
            const auto truth=epoch+steady*(1+slew);
            t.observe({frame,truth,steady,raw_radius,1e-6,TimingQuality::estimated,17});
            const auto p=t.predict(frame+1225);
            const auto future=epoch+(frame+1225)/8000*1.0001L*(1+slew);
            require(std::abs(p.utc_seconds-future)<=p.uncertainty_seconds,
                "native position bound discarded sound-card drift or continuous UTC slew");
        }
    }
    UtcTimeline duplicate(8000,.001,mode,shared,true);
    duplicate.observe({400,epoch+.05L,.05L,raw_radius,1e-6,TimingQuality::estimated,17});
    duplicate.observe({400,epoch+.07L,.07L,raw_radius,1e-6,TimingQuality::estimated,17});
    const auto unchanged=duplicate.predict(400);
    rejects([&]{duplicate.observe({400,epoch+.09L,.09L,raw_radius,1e-6,TimingQuality::estimated,17});});
    require(duplicate.predict(400).utc_seconds==unchanged.utc_seconds &&
        duplicate.predict(400).uncertainty_seconds==unchanged.uncertainty_seconds,
        "contradictory duplicate observation mutated the retained timeline");
    UtcTimeline wrong_clock(8000,.001,mode,shared,true);
    bool rejected=false;
    for(unsigned i=1;i<=1000 && !rejected;++i) {
        try {wrong_clock.observe({i*400.L,epoch+i*.05L*1.003L,i*.05L*1.003L,
            raw_radius,1e-6,TimingQuality::estimated,17});}catch(const datapump::Error&){rejected=true;}
    }
    require(rejected,"native rate envelope silently forgot a contradictory clock");
    rejects([&]{UtcTimeline bad(8000,.001,UtcTimeline::Prediction::fitted,shared,true);});
    // Screenshot's current point fits, but the full generic cone used at its
    // block end grows by0.1002ms. Do not call this a safe continuation.
    require(protected_timing_frontiers(.02494976,.025049963,.02499,.024999,limit)==ProtectedTimingAction::abort,
        "native block-end forecast ignored uncertainty growth");
    require(protected_timing_frontiers(.020,.0201,.02499,.02501,limit)==ProtectedTimingAction::finish,
        "native stop forecast spent the next-block reaction margin");
    // Recorded failure: full independent cover forecast crosses 25ms while
    // current PCM remains at16.145ms. Reserve valid pulse support, not a second
    // copy of the entire separately keyed3+s cover. Runtime cover checks remain.
    constexpr double observed=.016145462,cone=1./(1.-.002)-1.;
    require(protected_timing_frontiers(observed,observed+cone*.05,
        observed+cone*4.369402,observed+cone*4.419402,limit)==ProtectedTimingAction::finish,
        "recorded conservative full-cover stop was not reproduced");
    require(protected_timing_frontiers(observed,observed+cone*.05,
        observed+cone*.15,observed+cone*.20,limit)==ProtectedTimingAction::proceed,
        "independent cover incorrectly consumes valid-pulse timing reserve");
    require(protected_timing_frontiers(.0251,.0252,.0253,.0254,limit)==ProtectedTimingAction::abort,
        "independent cover exemption excused an actual current UTC violation");
    require(protected_timing_frontiers(.020,.0201,.023,.0231,limit)==ProtectedTimingAction::proceed,
        "native safe stop frontier was rejected");
}
void continuous_guard() {
    using namespace datapump::audio;
    require(protected_timing_action(.001,.001,.0003,3,.05,1e-6,.004)==ProtectedTimingAction::proceed,
        "safe protected tail spuriously stopped");
    require(protected_timing_action(.001,.001,.0003,3,.05,1e-6,.00291)==ProtectedTimingAction::finish,
        "controller spent the one-block protected-stop reaction margin");
    require(protected_timing_action(.001,.002,.0003,3,.05,1e-6,.00291)==ProtectedTimingAction::abort,
        "infeasible protected tail was falsely promised after uncertainty increased");
    for(const auto fs:{64U,6000U,48000U})for(const double initial:{-.00085,-.0001,0.,.0001,.00085})for(const bool steering:{false,true}) {
        Resampler resampler(fs,fs,true);resampler.initialize_timing(.375,initial);
        const auto origin=resampler.source_seconds();resampler.observe_affine_timing(origin,initial);
        ContinuousAffineTimingGuard guard(origin,initial,fs,fs,.01/fs);
        std::array<float,257> input{},output{};std::size_t offset=0;
        double greatest=0;
        for(unsigned block=0;block<400;++block) {
            const auto before=resampler.timing_block();
            const auto size=1+(block*79)%output.size();
            const auto plan=guard.plan(steering?(block%71<35?.0008:-.0008):initial,
                {resampler.source_seconds(),before.frames,resampler.rate_correction()},size,1e-5);
            resampler.begin_timing_block();resampler.set_rate_correction(plan.target,plan.slew);
            std::size_t count=0;
            while(count<size) {
                const auto p=resampler.process(std::span(input).subspan(offset),std::span(output).subspan(count,size-count));
                offset=(offset+p.consumed)%input.size();count+=p.produced;
                require(p.consumed || p.produced,"continuous guard fixture stalled");
            }
            const auto actual=resampler.timing_block();
            const ContinuousAffineTimingGuard::Point end{resampler.source_seconds(),actual.frames,resampler.rate_correction()};
            guard.verify(plan,end,actual.minimum,actual.maximum);
            greatest=std::max(greatest,static_cast<double>(std::max(std::abs(actual.minimum),std::abs(actual.maximum))));
            rejects([&]{guard.verify(plan,end,actual.minimum,actual.maximum);}); // stale certificate
        }
        require(greatest<=.01/fs,"continuous correction reset the original affine allowance");
        rejects([&]{resampler.observe_affine_timing(resampler.source_seconds(),0);});
    }
    ContinuousAffineTimingGuard check(0,0,48000,48000,.01/48000);
    const auto plan=check.plan(.0001,{0,0,0},1,1e-5);
    rejects([&]{check.verify(plan,{1.L/48000,1,0},-1.,1.);}); // safe-looking endpoint, unsafe interior
    const auto shorter=check.plan(0,{0,0,0},29,1e-5);
    check.verify(shorter,{1.L/48000,1,0},0,0); // final short block needs no horizon reset
}

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
    native_timestamp_intervals();native_query_uncertainty();native_frozen_source_timing();
    require(datapump::clock_sync::parse_audio_error("default")==.05&&
        datapump::clock_sync::parse_audio_error("Default")==.05&&
        datapump::clock_sync::parse_audio_error("50ms")==.05,
        "CLI Audio Default no longer resolves to50ms total lag");
    rejects([]{datapump::clock_sync::parse_audio_error("50");});
    continuous_guard();
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
    const auto default_audio=datapump::clock_sync::arrival_map({},epoch,epoch,0,48000,1./48000,0,.025,0);
    const auto zero_audio=datapump::clock_sync::arrival_map({},epoch,epoch,0,48000,1./48000,0,0,0,0);
    require(default_audio && zero_audio &&
        std::abs((default_audio->half_width_samples-zero_audio->half_width_samples)/48000-.05)<1e-12,
        "50 ms total per-station audio interval incorrectly used as a radius");
    const auto positive=datapump::clock_sync::audio_lag_arrival_map({0,0,0},epoch,epoch,0,1000,1./1000,0,.01,0,.05);
    require(positive && std::abs(positive->origin_samples-25)<1e-6 &&
        positive->half_width_samples>=35 && positive->half_width_samples<35.02,
        "positive playback lag lost its center or halved provider uncertainty");
    require(!datapump::clock_sync::audio_lag_arrival_map({},epoch,epoch,0,1000,1./1000,0,.026,0,.05),
        "26 ms timestamp radius qualified inside a 50 ms total lag interval");
    require(datapump::clock_sync::audio_lag_arrival_map({},epoch,epoch,0,1000,1./1000,0,.024,0,.05).has_value(),
        "24 ms timestamp radius rejected a 50 ms total lag interval");
    TimePrediction boundary{1.015L,.014,1./48000,TimingQuality::bounded,0};
    require(presentation_correction(boundary,1,48000,.03).uncertainty_seconds==.014,
        "valid 30 ms presentation allowance rejected");
    boundary.utc_seconds=1.017L;
    rejects([&]{presentation_correction(boundary,1,48000,.03);});
    require(presentation_correction(boundary,1,48000,.04).uncertainty_seconds==.014,
        "larger selected audio error was ignored");
    datapump::clock_sync::validate_audio_error(0.);
    require(datapump::clock_sync::default_audio_error_seconds==.05 &&
        datapump::clock_sync::Policy{}.region_seconds==.001 &&
        datapump::clock_sync::Policy{}.offset_seconds==0,
        "local clock defaults lost the 50 ms audio allowance or restored the Earth-Moon-Earth preset");
    for(const auto error:{-1.,61.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
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
