#include "datapump/triad_modem_v3.hpp"
#include "../src/triad_random_process.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
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
Capture capture(v::StreamChannelConfig cfg,std::size_t chunk){
    cfg.maximum_chunk_samples=chunk;v::StreamingChannel channel(source(),cfg);Capture result;
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
    check(r.minimum_echo_ratio>=0&&r.maximum_echo_ratio<=1&&
          r.maximum_echo_ratio-r.minimum_echo_ratio>.1,
          "bounded echo gain did not visit different in-support regimes");
    check(r.minimum_doppler_hz>=-9&&r.maximum_doppler_hz<=9&&
          r.maximum_doppler_hz-r.minimum_doppler_hz>.1,
          "bounded echo Doppler did not vary inside its public support");
    check(a.report.tail_samples>0,"random interior delay lost the bounded delayed tail");
    cfg.effects.seed++;
    const auto other=capture(cfg,31);
    check(other.samples!=a.samples,"bounded spread ignored its independent simulation seed");
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
    cfg=settings();cfg.effects.static_doppler_hz=100;cfg.path_evolution.common_doppler_wander_hz=1000;
    rejects([&]{v::StreamingChannel channel(source(),cfg);});
}
}
int main(){try{exact_bypass();chunk_state_and_tails();bounded_spread_evolves_and_streams();bounded_spread_phase_integrates_frequency();common_doppler_wander_is_bounded_and_continuous();effects_and_erasures();bounds();std::cout<<"triad streaming channel invariants passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
