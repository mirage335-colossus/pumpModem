#include "datapump/triad_modem_v3.hpp"
#include "../src/triad_random_process.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// Tiny deterministic correctness probes. Performance and statistical claims
// belong to the hierarchical abbreviated harness, never full sampled campaigns.
namespace v=datapump::triad::v3;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F>void rejects(F action){try{action();}catch(const std::exception&){return;}throw std::runtime_error("invalid channel configuration accepted");}
constexpr std::uint64_t source_samples=768;
v::Complex sample(std::uint64_t n){
    if(n>=384)return {};
    return .2*std::polar(1.,2*std::numbers::pi*17*static_cast<double>(n)/1024);
}
v::EmittedSource source(){
    v::EmittedSource result;result.format.sample_rate=1024;
    result.geometry=v::geometry(result.format);
    result.geometry.source_samples=source_samples;result.geometry.calendar_origin_sample=0;
    result.minimum_frequency_hz=-17;result.maximum_frequency_hz=17;
    result.sample_at=sample;return result;
}
v::StreamChannelConfig settings(){
    v::StreamChannelConfig result;result.effects.sample_rate=1024;
    result.effects.occupied_bandwidth_hz=v::geometry(source().format).occupied_bandwidth_hz;
    result.effects.rf_hz=30e6;result.effects.noise_enabled=false;
    result.complex_noise_variance=0;result.effects.seed=0x13579bdf;
    return result;
}
struct Capture{std::vector<v::Complex> samples;std::vector<std::uint8_t> valid;v::StreamChannelReport report;};
Capture capture(v::StreamChannelConfig cfg,std::size_t chunk,v::EmittedSource emitted=source()){
    cfg.maximum_chunk_samples=chunk;v::StreamingChannel channel(std::move(emitted),cfg);Capture result;
    result.samples.resize(static_cast<std::size_t>(channel.total_samples()));result.valid.resize(result.samples.size());
    for(std::size_t first=0;first<result.samples.size();){
        const auto count=std::min(chunk,result.samples.size()-first);
        check(channel.read(std::span(result.samples).subspan(first,count),std::span(result.valid).subspan(first,count))==count,"channel truncated its complete tail");
        first+=count;
    }
    check(channel.position()==channel.total_samples(),"channel cursor lost rows");
    check(channel.read({})==0,"channel produced rows after endpoint");
    result.report=channel.report();check(result.report.samples==result.samples.size(),"channel report lost rows");
    return result;
}
void exact_bypass(){
    const auto out=capture(settings(),37);
    check(out.samples.size()>=source_samples,"channel omitted source support");
    for(std::size_t n=0;n<out.samples.size();++n){
        check(out.samples[n]==(n<source_samples?sample(n):v::Complex{}),"None/Off is not an exact bypass");
        check(out.valid[n]==1,"valid source row erased");
    }
}
void chunk_state_and_tails(){
    auto cfg=settings();cfg.effects.spread_delay_ms=21;cfg.effects.spread_doppler_hz=9;
    cfg.effects.tx.rf_ppm=.01;cfg.effects.rx.audio_ppm=2;
    cfg.effects.noise_enabled=true;cfg.complex_noise_variance=.003;
    cfg.frontend.dac.nominal_bits=16;cfg.frontend.adc.nominal_bits=14;
    cfg.frontend.lowpass_hz=200;cfg.frontend.impulse_amplitude=.02;cfg.frontend.impulse_rate_hz=2;
    cfg.path_evolution.random_doppler_width_hz=.5;
    // At 30 MHz the public |delay'| bound contributes RF Doppler. A 1-us
    // excursion over one second retains moving-delay state while fitting the
    // 1,024-Hz fixture clock (112.5-Hz motion allowance per edge).
    cfg.path_evolution.delay_excursion_seconds=1e-6;cfg.path_evolution.delay_knot_seconds=1;
    const auto a=capture(cfg,31),b=capture(cfg,127);
    check(a.samples==b.samples&&a.valid==b.valid,"chunking changed physical channel/random/converter state");
    check(a.report.tail_samples>0,"selected channel lost its complete tail");
    check(a.report.observed_energy==b.report.observed_energy,"chunking changed energy accounting");
    auto echo=settings();echo.effects.spread_delay_ms=21;echo.effects.spread_doppler_hz=1;
    const auto retained=capture(echo,19);
    check(std::abs(retained.samples[390])>0,"source quiet interval gated away a delayed echo");
}
void bounded_spread_evolves_and_streams(){
    auto cfg=settings();cfg.effects.spread_delay_ms=21;cfg.effects.spread_doppler_hz=9;
    cfg.path_evolution.bounded_spread_knot_seconds=.13;
    const auto a=capture(cfg,31),b=capture(cfg,127);
    check(a.samples==b.samples&&a.valid==b.valid,"bounded spread changed across read chunk boundaries");
    check(a.report.bounded_spread.has_value(),"bounded spread realization was not reported");
    const auto& r=*a.report.bounded_spread;
    check(r.excess_delay_seconds>=0&&r.excess_delay_seconds<=.021&&r.processed_rows==a.samples.size(),
          "bounded spread delay or physical-row accounting escaped its public support");
    check(r.minimum_echo_ratio&&r.maximum_echo_ratio&&
          *r.minimum_echo_ratio>=0&&*r.maximum_echo_ratio<=1&&
          *r.maximum_echo_ratio-*r.minimum_echo_ratio>.1&&
          !r.fixed_direct_power_fraction,
          "bounded echo gain did not visit different in-support regimes");
    check(r.minimum_doppler_hz>=-9&&r.maximum_doppler_hz<=9&&
          r.maximum_doppler_hz-r.minimum_doppler_hz>.1,
          "bounded echo Doppler did not vary inside its public support");
    check(r.minimum_combined_physical_doppler_hz==r.minimum_doppler_hz&&
          r.maximum_combined_physical_doppler_hz==r.maximum_doppler_hz,
          "combined trajectory differs from relative Doppler with common wander off");
    check(a.report.tail_samples>0,"random interior delay lost the bounded delayed tail");
    cfg.effects.seed++;
    const auto other=capture(cfg,31);
    check(other.samples!=a.samples,"bounded spread ignored its independent simulation seed");
}
void bounded_spread_fixed_powers(){
    auto cfg=settings();cfg.effects.spread_delay_ms=21;cfg.effects.spread_doppler_hz=9;
    cfg.path_evolution.bounded_spread_knot_seconds=.13;
    const auto constant_source=[] {
        auto value=source();value.minimum_frequency_hz=value.maximum_frequency_hz=0;
        value.sample_at=[](std::uint64_t n){return n<384?v::Complex{.2,0}:v::Complex{};};
        return value;
    };
    const auto original=capture(cfg,31,constant_source());
    cfg.path_evolution.bounded_spread_direct_power_fraction=1;
    const auto direct=capture(cfg,31,constant_source());
    cfg.path_evolution.bounded_spread_direct_power_fraction=0;
    const auto echo=capture(cfg,31,constant_source());
    constexpr double fraction=.233;
    cfg.path_evolution.bounded_spread_direct_power_fraction=fraction;
    const auto mixed=capture(cfg,31,constant_source());
    const auto split=capture(cfg,127,constant_source());
    check(direct.samples.size()==echo.samples.size()&&echo.samples.size()==mixed.samples.size()&&
          original.samples.size()==mixed.samples.size(),"fixed ray power changed complete capture support");
    check(mixed.samples==split.samples&&mixed.valid==split.valid,
          "fixed ray power changed across read chunk boundaries");
    const auto& fixed=*mixed.report.bounded_spread;
    check(fixed.fixed_direct_power_fraction==fraction&&fixed.minimum_echo_ratio&&fixed.maximum_echo_ratio&&
          *fixed.minimum_echo_ratio==*fixed.maximum_echo_ratio&&
          std::abs(*fixed.minimum_echo_ratio-std::sqrt((1-fraction)/fraction))<1e-14,
          "fixed ray powers or actual echo ratio were not reported");
    check(!echo.report.bounded_spread->minimum_echo_ratio&&!echo.report.bounded_spread->maximum_echo_ratio&&
          echo.report.bounded_spread->fixed_direct_power_fraction==0,
          "zero direct power reported a defined or nonfinite echo ratio");
    check(direct.report.bounded_spread->minimum_echo_ratio==0&&
          direct.report.bounded_spread->maximum_echo_ratio==0,
          "zero echo power did not report zero amplitude ratio");
    const auto seed=cfg.path_seed.value_or(cfg.effects.seed)^0x424f554e44535052ULL;
    v::detail::BoundedRandomSpread state(.021,9,.13,seed);
    bool checked_echo_interior=false,retained_echo_tail=false;
    for(std::size_t n=0;n<mixed.samples.size();++n){
        const auto path=state.at(static_cast<long double>(n)/1024);
        check(direct.samples[n]==(n<384?v::Complex{.2,0}:v::Complex{}),
              "unit direct power changed the unshifted constant source");
        const auto expected=std::sqrt(fraction)*direct.samples[n]+std::sqrt(1-fraction)*echo.samples[n];
        check(std::abs(mixed.samples[n]-expected)<2e-15,
              "fixed path powers changed phase, delay, tails or normalization");
        const auto prior=path.direct_gain*direct.samples[n]+path.echo_gain*echo.samples[n];
        check(std::abs(original.samples[n]-prior)<2e-15,
              "absent override changed the original bounded amplitude law");
        const long double delayed=static_cast<long double>(n)-path.delay_seconds*1024.L;
        if(delayed>=8&&delayed<376){
            const auto cycles=std::remainder(static_cast<long double>(cfg.effects.rf_hz)*path.delay_seconds,1.L);
            const auto expected_echo=.2*std::polar(1.,-2*std::numbers::pi*static_cast<double>(cycles))*
                std::polar(1.,path.phase_radians);
            check(std::abs(echo.samples[n]-expected_echo)<2e-14,
                  "pure echo lost the existing carrier-delay or changing Doppler phase");
            checked_echo_interior=true;
        }
        retained_echo_tail|=n>=384&&std::abs(echo.samples[n])>1e-6;
    }
    check(checked_echo_interior&&retained_echo_tail&&echo.report.tail_samples>0,
          "pure delayed path lost its analytic interior or post-source tail");

    // No path weighting may alter separately seeded thermal noise or the
    // existing common-motion/selective processes through source silence.
    cfg.effects.signal_enabled=false;cfg.effects.noise_enabled=true;cfg.complex_noise_variance=.003;
    cfg.path_evolution.common_doppler_wander_hz=6;
    cfg.path_evolution.common_doppler_knot_seconds=.15;
    cfg.effects.perturbation_slow_depth=.25;cfg.effects.perturbation_fast_depth=.65;
    cfg.selective_model.modulation_rate_hz=2;
    cfg.selective_model.coefficient_law=v::SelectiveCoefficientLaw::GaussianErf;
    cfg.path_evolution.bounded_spread_direct_power_fraction.reset();
    const auto noise=capture(cfg,29);
    for(const double power:{0.,fraction,1.}){
        cfg.path_evolution.bounded_spread_direct_power_fraction=power;
        const auto weighted=capture(cfg,113);
        check(noise.samples==weighted.samples&&noise.valid==weighted.valid,
              "fixed ray powers changed noise innovations through source silence");
        check(noise.report.bounded_spread->excess_delay_seconds==weighted.report.bounded_spread->excess_delay_seconds&&
              noise.report.bounded_spread->minimum_doppler_hz==weighted.report.bounded_spread->minimum_doppler_hz&&
              noise.report.bounded_spread->maximum_doppler_hz==weighted.report.bounded_spread->maximum_doppler_hz&&
              noise.report.bounded_spread->minimum_combined_physical_doppler_hz==weighted.report.bounded_spread->minimum_combined_physical_doppler_hz&&
              noise.report.bounded_spread->maximum_combined_physical_doppler_hz==weighted.report.bounded_spread->maximum_combined_physical_doppler_hz&&
              noise.report.common_doppler_wander->minimum_hz==weighted.report.common_doppler_wander->minimum_hz&&
              noise.report.common_doppler_wander->maximum_hz==weighted.report.common_doppler_wander->maximum_hz,
              "fixed ray powers changed existing path or common-frequency state");
    }
}
void combined_doppler_reports_simultaneous_physical_trajectory(){
    auto cfg=settings();cfg.effects.spread_delay_ms=21;cfg.effects.spread_doppler_hz=9;
    cfg.effects.capture_lead_seconds=.04;cfg.effects.rx.audio_ppm=-50;
    cfg.path_evolution.bounded_spread_knot_seconds=.13;
    cfg.path_evolution.common_doppler_wander_hz=6;cfg.path_evolution.common_doppler_knot_seconds=.15;
    auto emitted=source();emitted.minimum_frequency_hz=emitted.maximum_frequency_hz=0;
    emitted.sample_at=[](std::uint64_t n){return n<384?v::Complex{.2,0}:v::Complex{};};
    double prior_min=0,prior_max=0;
    for(const double power:{0.,1.}){
        cfg.path_evolution.bounded_spread_direct_power_fraction=power;
        const auto out=capture(cfg,power?113:29,emitted);
        v::detail::BoundedRandomSpread echo(.021,9,.13,cfg.effects.seed^0x424f554e44535052ULL);
        v::detail::BoundedRandomSpread common(0,6,.15,cfg.effects.seed^0x434f4d4d4f4e4652ULL);
        double minimum=0,maximum=0;bool checked_interior=false;
        for(std::size_t n=0;n<out.samples.size();++n){
            const long double physical=static_cast<long double>(n)/(1024.L*(1+cfg.effects.rx.audio_ppm*1e-6))-
                cfg.effects.capture_lead_seconds;
            const auto e=echo.at(physical),c=common.at(physical);
            const auto sum=e.doppler_hz+c.doppler_hz;
            if(n==0)minimum=maximum=sum;
            else {minimum=std::min(minimum,sum);maximum=std::max(maximum,sum);}
            const auto source_row=(physical-(power?0:e.delay_seconds))*1024;
            if(source_row>=8&&source_row<376){
                auto expected=.2*std::polar(1.,c.phase_radians);
                if(!power){
                    const auto cycles=std::remainder(static_cast<long double>(cfg.effects.rf_hz)*e.delay_seconds,1.L);
                    expected*=std::polar(1.,-2*std::numbers::pi*static_cast<double>(cycles))*std::polar(1.,e.phase_radians);
                }
                check(std::abs(out.samples[n]-expected)<2e-14,"endpoint phase differs from simultaneous common/relative trajectory");
                checked_interior=true;
            }
        }
        const auto& report=*out.report.bounded_spread;
        check(checked_interior&&report.minimum_combined_physical_doppler_hz==minimum&&
              report.maximum_combined_physical_doppler_hz==maximum&&minimum>=-15&&maximum<=15,
              "combined Doppler report lost simultaneous extrema or physical-time units");
        if(power)check(minimum==prior_min&&maximum==prior_max,"ray endpoint weighting changed combined trajectory report");
        prior_min=minimum;prior_max=maximum;
    }
}
void bounded_spread_phase_integrates_frequency(){
    using datapump::triad::v3::detail::BoundedRandomSpread;
    BoundedRandomSpread field(.01,12,.05,0x12345678);
    auto previous=field.at(-.1L);
    for(int i=1;i<=100;++i){
        const long double time=-.1L+i*.001L;
        const auto state=field.at(time);
        check(state.delay_seconds>=0&&state.delay_seconds<=.01&&state.echo_ratio>=0&&state.echo_ratio<=1&&
              state.doppler_hz>=-12&&state.doppler_hz<=12&&
              std::abs(state.direct_gain*state.direct_gain+state.echo_gain*state.echo_gain-1)<1e-12,
              "bounded path state violated its parameter or power limits");
        const double delta=std::remainder(state.phase_radians-previous.phase_radians,2*std::numbers::pi);
        const double expected=2*std::numbers::pi*.001*.5*(state.doppler_hz+previous.doppler_hz);
        check(std::abs(std::remainder(delta-expected,2*std::numbers::pi))<1e-9,
              "echo phase did not integrate the continuous bounded Doppler path");
        previous=state;
    }
}
void common_doppler_wander_is_bounded_and_continuous(){
    auto cfg=settings();cfg.effects.static_doppler_hz=1.5;
    cfg.path_evolution.common_doppler_wander_hz=6;
    cfg.path_evolution.common_doppler_knot_seconds=.15;
    const auto a=capture(cfg,29),b=capture(cfg,113);
    check(a.samples==b.samples&&a.valid==b.valid,
          "common Doppler phase changed across read chunk boundaries");
    check(a.samples!=capture(settings(),29).samples,
          "common Doppler wander did not affect the complete waveform");
    check(a.report.common_doppler_wander.has_value(),
          "common Doppler realization was not reported");
    const auto& r=*a.report.common_doppler_wander;
    check(r.bound_hz==6&&r.processed_rows==a.samples.size()&&
          r.minimum_hz>=-6&&r.maximum_hz<=6&&r.maximum_hz-r.minimum_hz>.1,
          "common Doppler wander exceeded its configured support or did not evolve");
}
void effects_and_erasures(){
    auto cfg=settings();cfg.effects.noise_enabled=true;cfg.complex_noise_variance=.001;
    cfg.effects.fhss=true;cfg.effects.hop_offsets_hz={0};
    cfg.noise_seed=19;const auto baseline=capture(cfg,41);
    cfg.noise_seed=20;const auto different=capture(cfg,41);
    check(baseline.samples!=different.samples,"independent thermal seed ignored");
    cfg.noise_seed=19;cfg.missed_receiver_dwell_indices={1};const auto erased=capture(cfg,41);
    std::size_t missing=0;
    for(std::size_t n=0;n<baseline.samples.size();++n){
        if(erased.valid[n])check(baseline.samples[n]==erased.samples[n],"observation erasure changed later physical state");
        else{++missing;check(erased.samples[n]==v::Complex{},"erased row leaked observed PCM");}
    }
    check(missing>0&&erased.report.erased_samples==missing,"erasure accounting lost missing rows");
    cfg=settings();cfg.effects.spread_doppler_hz=2;cfg.effects.perturbation_slow_depth=.25;
    cfg.effects.perturbation_fast_depth=.65;cfg.selective_model.modulation_rate_hz=2;
    cfg.selective_model.coefficient_law=v::SelectiveCoefficientLaw::GaussianErf;
    const auto a=capture(cfg,29),b=capture(cfg,113);
    check(a.samples==b.samples,"random selective response changed across chunks");
    check(a.report.selective_perturbation.has_value(),"selected modulation was silently omitted");
    check(a.samples!=capture(settings(),29).samples,"selected channel effect is an accidental bypass");
}
// Every job on its own thread, the last one first.
void threads(std::size_t jobs,const std::function<void(std::size_t)>& job){
    std::vector<std::thread> workers;
    for(std::size_t i=jobs;i-->1;)workers.emplace_back([&job,i]{job(i);});
    if(jobs)job(0);
    for(auto& worker:workers)worker.join();
}
bool same_bits(const std::vector<v::Complex>& a,const std::vector<v::Complex>& b){
    return a.size()==b.size()&&(a.empty()||!std::memcmp(a.data(),b.data(),a.size()*sizeof(v::Complex)));
}
void lanes_reproduce_the_lone_reader(){
    // Fractional source coordinates, both fixed-time laws and every state
    // that a block prepares ahead of the reader.
    auto changing=settings();changing.effects.tx.audio_ppm=3;changing.effects.rx.audio_ppm=-2;
    changing.effects.timing_offset_seconds=.0123;changing.effects.capture_lead_seconds=.05;
    changing.effects.spread_delay_ms=21;changing.effects.spread_doppler_hz=9;
    changing.path_evolution.bounded_spread_knot_seconds=.13;
    changing.path_evolution.common_doppler_wander_hz=6;changing.path_evolution.common_doppler_knot_seconds=.15;
    changing.effects.perturbation_slow_depth=.25;changing.effects.perturbation_fast_depth=.65;
    changing.selective_model.modulation_rate_hz=2;
    changing.selective_model.coefficient_law=v::SelectiveCoefficientLaw::GaussianErf;
    changing.effects.noise_enabled=true;changing.complex_noise_variance=.003;
    changing.effects.tx.phase_noise_rad_per_sqrt_second=.02;changing.effects.rx.phase_noise_rad_per_sqrt_second=.03;
    auto diffuse=settings();diffuse.effects.rx.audio_ppm=40;
    diffuse.effects.spread_delay_ms=21;diffuse.effects.spread_doppler_hz=2;
    diffuse.path_evolution.random_doppler_width_hz=.5;
    diffuse.path_evolution.delay_excursion_seconds=1e-6;diffuse.path_evolution.delay_knot_seconds=1;
    for(const auto& base:{changing,diffuse,settings()}){
        const auto alone=capture(base,4096);
        check(same_bits(alone.samples,capture(base,1).samples),"a block prepared ahead differs from single rows");
        for(const unsigned count:{2U,3U,32U}){
            auto cfg=base;cfg.lanes={count,threads};
            const auto shared=capture(cfg,4096);
            check(same_bits(alone.samples,shared.samples)&&alone.valid==shared.valid,
                  "lanes changed captured samples");
            check(std::bit_cast<std::uint64_t>(alone.report.signal_energy)==
                      std::bit_cast<std::uint64_t>(shared.report.signal_energy)&&
                  std::bit_cast<std::uint64_t>(alone.report.observed_energy)==
                      std::bit_cast<std::uint64_t>(shared.report.observed_energy)&&
                  alone.report.bounded_spread.has_value()==shared.report.bounded_spread.has_value()&&
                  (!alone.report.bounded_spread||
                   (alone.report.bounded_spread->minimum_combined_physical_doppler_hz==
                        shared.report.bounded_spread->minimum_combined_physical_doppler_hz&&
                    alone.report.bounded_spread->maximum_echo_ratio==shared.report.bounded_spread->maximum_echo_ratio)),
                  "lanes changed the channel report");
        }
    }
    // A provider that leaves a job out, or fails, must fail the read.
    auto cfg=changing;
    cfg.lanes={3,[](std::size_t jobs,const std::function<void(std::size_t)>& job){for(std::size_t i=1;i<jobs;++i)job(i);}};
    rejects([&]{capture(cfg,4096);});
    cfg.lanes={3,[](std::size_t,const std::function<void(std::size_t)>&){throw std::runtime_error("no workers");}};
    rejects([&]{capture(cfg,4096);});
    cfg.lanes={3,{}};rejects([&]{v::StreamingChannel channel(source(),cfg);});
}
void source_failure_reaches_the_reader_in_order(){
    // The emitted source fails once in the middle of a block. Rows before
    // the first one that needs the sample are delivered and the failure is
    // reported there. A lone reader can continue the same realization; with
    // lanes other states have already passed that row and the channel ends.
    auto base=settings();base.effects.rx.audio_ppm=25;base.effects.spread_delay_ms=21;base.effects.spread_doppler_hz=9;
    base.effects.noise_enabled=true;base.complex_noise_variance=.003;
    struct Outcome{std::vector<v::Complex> samples;std::size_t failed_at=0,failures=0;};
    const auto run=[&](v::StreamChannelConfig cfg,std::size_t chunk,bool resume){
        bool failed=false;auto emitted=source();
        emitted.sample_at=[&failed](std::uint64_t n){
            if(n==300&&!failed){failed=true;throw std::runtime_error("source paused");}
            return sample(n);};
        cfg.maximum_chunk_samples=chunk;v::StreamingChannel channel(std::move(emitted),cfg);
        Outcome out;out.samples.assign(static_cast<std::size_t>(channel.total_samples()),v::Complex{7,7});
        while(channel.position()<channel.total_samples()){
            const auto first=static_cast<std::size_t>(channel.position());
            const auto count=std::min(chunk,out.samples.size()-first);
            try{channel.read(std::span(out.samples).subspan(first,count));}
            catch(const std::runtime_error& error){
                check(std::string(error.what())=="source paused","source failure was replaced");
                if(!out.failures)out.failed_at=static_cast<std::size_t>(channel.position());
                if(++out.failures>1||!resume)break;
            }
        }
        return out;
    };
    const auto rows=run(base,1,true);
    check(rows.failures==1&&rows.failed_at>200&&rows.failed_at<300,
          "source failure was not reported once at the first row that needs the sample");
    for(const std::size_t chunk:{std::size_t{97},std::size_t{4096}}){
        const auto block=run(base,chunk,true);
        check(block.failures==1&&block.failed_at==rows.failed_at&&same_bits(block.samples,rows.samples),
              "a block reported a source failure at another row or resumed another realization");
        auto cfg=base;cfg.lanes={4,threads};
        const auto shared=run(cfg,chunk,false);
        check(shared.failures==1&&shared.failed_at==rows.failed_at&&
              std::equal(shared.samples.begin(),shared.samples.begin()+static_cast<std::ptrdiff_t>(rows.failed_at),
                         rows.samples.begin(),[](v::Complex a,v::Complex b){return !std::memcmp(&a,&b,sizeof a);}),
              "lanes reported a source failure at another row");
        // The same channel reports that failure again instead of continuing.
        if(chunk==4096)check(run(cfg,chunk,true).failures==2,"a channel with lanes continued after a failed read");
    }
}
void coordinate_forms_match_the_standard_ones(){
    namespace d=v::detail;
    const auto same=[](long double a,long double b){return (a==b&&std::signbit(a)==std::signbit(b))||(std::isnan(a)&&std::isnan(b));};
    std::vector<long double> values{0.L,-0.L,.5L,-.5L,1.L,-1.L,1.5L,-1.5L,2.5L,-2.5L,
        std::numeric_limits<long double>::denorm_min(),-std::numeric_limits<long double>::denorm_min(),
        0x1p51L,-0x1p51L,0x1p52L,-0x1p52L,0x1p62L,-0x1p62L,0x1p63L,0x1.8p63L,
        std::numeric_limits<long double>::infinity(),-std::numeric_limits<long double>::infinity(),
        std::numeric_limits<long double>::quiet_NaN()};
    // Integers, their neighbours in long double and in double, and halves.
    for(const long double whole:{0.L,1.L,2.L,7.L,1024.L,123456789.L,0x1p40L,0x1p50L,0x1p51L-1}){
        for(const long double sign:{1.L,-1.L}){
            const auto x=sign*whole;
            values.insert(values.end(),{x,std::nextafter(x,2*x+1),std::nextafter(x,-2*x-1),x+.5L,x-.5L,
                static_cast<long double>(std::nextafter(static_cast<double>(x),1e300)),
                static_cast<long double>(std::nextafter(static_cast<double>(x),-1e300)),
                x+0x1p-60L,x-0x1p-60L,x+.4999999999999999999L,x+.5000000000000000001L});
        }
    }
    std::uint64_t state=0x243f6a8885a308d3ULL;
    for(int i=0;i<200000;++i){
        state=state*6364136223846793005ULL+1442695040888963407ULL;
        values.push_back(static_cast<long double>(static_cast<std::int64_t>(state))*0x1p-12L*((i%5)?0x1p-20L:1.L));
    }
    for(const auto x:values){
        check(same(d::floor_coordinate(x),std::floor(x)),"floor of a coordinate differs from std::floor");
        check(same(d::ceil_coordinate(x),std::ceil(x)),"ceil of a coordinate differs from std::ceil");
        if(!(std::abs(x)<0x1p63L))continue;
        check(d::signed_index(x)==static_cast<std::int64_t>(x),"coordinate truncation differs from the standard conversion");
        const auto whole=d::floor_whole(x);
        check(same(whole.value,std::floor(x))&&whole.index==static_cast<std::int64_t>(std::floor(x)),
              "coordinate floor and index disagree with the standard forms");
        if(x>-1)check(d::unsigned_index(x)==static_cast<std::uint64_t>(x),
                      "unsigned coordinate index differs from the standard conversion");
    }
    check(d::unsigned_index(0x1.8p63L)==static_cast<std::uint64_t>(0x1.8p63L),"large unsigned index differs");
}
void bounds(){
    auto cfg=settings();cfg.maximum_chunk_samples=0;rejects([&]{v::StreamingChannel channel(source(),cfg);});
    cfg=settings();cfg.effects.sample_rate=0;rejects([&]{v::StreamingChannel channel(source(),cfg);});
    cfg=settings();cfg.complex_noise_variance=-1;rejects([&]{v::StreamingChannel channel(source(),cfg);});
    cfg=settings();cfg.missed_receiver_dwell_indices={1,1};rejects([&]{v::StreamingChannel channel(source(),cfg);});
    auto missing=source();missing.sample_at={};cfg=settings();rejects([&]{v::StreamingChannel channel(missing,cfg);});
    cfg=settings();cfg.path_evolution.delay_excursion_seconds=1;rejects([&]{v::StreamingChannel channel(source(),cfg);});
    cfg=settings();cfg.path_evolution.bounded_spread_knot_seconds=.1;rejects([&]{v::StreamingChannel channel(source(),cfg);});
    cfg=settings();cfg.effects.spread_delay_ms=21;cfg.path_evolution.bounded_spread_knot_seconds=.1;
    cfg.path_evolution.random_doppler_width_hz=.5;rejects([&]{v::StreamingChannel channel(source(),cfg);});
    cfg=settings();cfg.effects.spread_delay_ms=21;cfg.path_evolution.bounded_spread_knot_seconds=.1;
    cfg.path_evolution.outage_start_seconds=0;cfg.path_evolution.outage_end_seconds=1;
    rejects([&]{v::StreamingChannel channel(source(),cfg);});
    cfg=settings();cfg.path_evolution.bounded_spread_direct_power_fraction=.5;
    rejects([&]{v::StreamingChannel channel(source(),cfg);});
    for(const double power:{-.01,1.01,std::numeric_limits<double>::infinity(),
                            std::numeric_limits<double>::quiet_NaN()}){
        cfg=settings();cfg.effects.spread_delay_ms=21;cfg.path_evolution.bounded_spread_knot_seconds=.1;
        cfg.path_evolution.bounded_spread_direct_power_fraction=power;
        rejects([&]{v::StreamingChannel channel(source(),cfg);});
    }
    cfg=settings();cfg.effects.static_doppler_hz=100;cfg.path_evolution.common_doppler_wander_hz=1000;
    rejects([&]{v::StreamingChannel channel(source(),cfg);});
}
}
int main(){try{exact_bypass();chunk_state_and_tails();bounded_spread_evolves_and_streams();bounded_spread_fixed_powers();combined_doppler_reports_simultaneous_physical_trajectory();bounded_spread_phase_integrates_frequency();common_doppler_wander_is_bounded_and_continuous();effects_and_erasures();lanes_reproduce_the_lone_reader();source_failure_reaches_the_reader_in_order();coordinate_forms_match_the_standard_ones();bounds();std::cout<<"triad streaming channel invariants passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
