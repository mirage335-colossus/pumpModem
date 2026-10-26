#include "../src/live_helper_frequency_snapshot.hpp"
#include <cstdlib>
#include <iostream>

namespace d=datapump::live::detail;
namespace h=datapump::helper;
namespace m=datapump::modem;
namespace {
template<class T> concept HasMarkerClock=requires(T value){value.clock;};
static_assert(!HasMarkerClock<d::HelperOwnedFrequencyFrame>);
void check(bool value,const char* why){if(!value){std::cerr<<why<<'\n';std::exit(1);}}
void exact_owned_frequency_with_actual_clock_frame() {
    m::Config c;c.sample_rate=6000;c.carrier_hz=1500;c.oscillator_search.emplace();
    c.oscillator_search->reference=m::OscillatorReference::shared_radio;
    c.oscillator_search->rf_shift_hz=30000000;
    const h::CurrentLinkProvider provider{7,.008726646259971648};
    h::Reference raw;raw.sample_rate=48960;raw.sample=raw.frequency_sample=489600;
    raw.frequency_first_sample=487600;raw.frequency_hz=5062.70625;
    raw.frequency_uncertainty_hz=.1;raw.frequency_fit=h::FrequencyFit::local_polynomial;raw.frequency_valid=true;
    const datapump::audio::CaptureTransportRecipe transport{
        datapump::audio::CaptureTransportKind::fixed_nominal_audio,raw.sample_rate,c.sample_rate,0,0};
    const auto source=d::helper_frequency_publication(c,2062.5,raw,12,provider,0,raw.sample+200,transport);
    check(bool(source),"owned publication unavailable");
    d::LiveHelperClockSnapshot snapshot;snapshot.domain=source->raw_domain;
    snapshot.capture_known=true;snapshot.captured_first_sample=0;
    snapshot.captured_end_sample=source->raw_captured_end+2ULL*raw.sample_rate;snapshot.expected_helper_hz=2062.5;
    snapshot.transport_recipe=transport;
    snapshot.clock.reset(snapshot.domain);
    for(unsigned i=0;i<16;++i) {
        h::Reference marker;marker.sample_rate=raw.sample_rate;
        const auto n=static_cast<std::uint64_t>((.24L+.4L*i)*raw.sample_rate);
        marker.sample=n+100;marker.punctuation={1,n,n-100,n+100,0,.01,true};
        check(snapshot.clock.observe(marker,snapshot.domain,snapshot.captured_end_sample)==h::PunctuationClockReason::accepted,
            "actual marker fixture was not admitted");
    }
    const auto actual_end=snapshot.captured_end_sample,first=snapshot.captured_first_sample;
    const auto count=snapshot.clock.observation_count();
    check(d::bind_helper_frequency_publication(snapshot,*source,c,2062.5,provider.phase_noise_radians_per_sqrt_second),
        "matching raw History source declined");
    check(snapshot.frequency_observation_frontier==source->raw_captured_end&&
        snapshot.frequency.sample==raw.sample,"owned original source date was replaced by decoder today");
    check(snapshot.captured_end_sample==actual_end&&snapshot.captured_first_sample==first&&
        snapshot.clock.observation_count()==count&&snapshot.domain==source->raw_domain,
        "frequency binding changed actual marker/capture domain or frontier");
    check(!snapshot.frequency.phase_valid&&!snapshot.frequency.phase_observation_known&&!snapshot.frequency.punctuation.known,
        "frequency binding manufactured phase/marker authority");
    m::PatternAcquisitionScoredSupport scored;scored.first_sample=30000;scored.last_sample=40000;
    scored.consumed_end_sample=40000;scored.domain.sample_rate=c.sample_rate;
    const auto full_raw_last=d::helper_frequency_scored_raw_last(snapshot,c,scored,0,0,50000);
    check(bool(full_raw_last),"full retrospective mapped support did not fit SourceDate");
    check(!d::live_helper_clock_frequency_input(c,snapshot,1).valid,
        "owned SourceDate accepted without an actual full scored-support bound");
    check(d::live_helper_clock_frequency_input(c,snapshot,1,h::JointClockSourceRelation::physical_audio_unsteered,
        {},full_raw_last).valid,"covered SourceDate did not retain current frequency qualification");
    auto later=scored;later.first_sample=59000;later.last_sample=later.consumed_end_sample=61000;
    check(!d::helper_frequency_scored_raw_last(snapshot,c,later,0,0,62000),
        "actual captured future raw support borrowed an old SourceDate");
    auto unowned=snapshot;unowned.frequency_observation_frontier.reset();
    check(!d::live_helper_clock_frequency_input(c,unowned,1).valid,"unowned old record bypassed current actual-end freshness");
    auto mismatch=*source;++mismatch.raw_domain.provider_identity;
    check(!d::bind_helper_frequency_publication(snapshot,mismatch,c,2062.5,provider.phase_noise_radians_per_sqrt_second),
        "foreign raw provider borrowed actual marker frame");
    auto clipped=snapshot;clipped.captured_first_sample=source->first_sample+1;
    check(!d::bind_helper_frequency_publication(clipped,*source,c,2062.5,provider.phase_noise_radians_per_sqrt_second),
        "unavailable original fit prefix was clipped into History");
    auto future=*source;future.raw_captured_end=snapshot.captured_end_sample+1;future.mapping.settled_end_sample=future.raw_captured_end;
    check(!d::bind_helper_frequency_publication(snapshot,future,c,2062.5,provider.phase_noise_radians_per_sqrt_second),
        "future source capture end was admitted");
    check(!d::bind_helper_frequency_publication(snapshot,*source,c,2062.5,0),"raw provider model binding changed silently");
    auto moved_recipe=*source;++moved_recipe.transport_recipe.raw_zero;
    check(!d::bind_helper_frequency_publication(snapshot,moved_recipe,c,2062.5,provider.phase_noise_radians_per_sqrt_second),
        "different original transport zero borrowed actual History support");
}
void whole_ingress_right_adjoint() {
    using Kind=datapump::audio::CaptureTransportKind;
    const auto source=[](std::uint32_t raw_rate,std::uint32_t logical_rate,Kind kind,
            std::uint64_t raw_zero=0,std::uint64_t logical_zero=0) {
        m::Config c;c.sample_rate=logical_rate;c.carrier_hz=1500;c.oscillator_search.emplace();
        h::Reference r;r.sample_rate=raw_rate;r.sample=r.frequency_sample=1;r.frequency_valid=true;
        r.frequency_hz=2062.5;r.frequency_uncertainty_hz=.1;r.frequency_fit=h::FrequencyFit::local_polynomial;
        const auto p=d::helper_frequency_publication(c,2062.5,r,12,{7,.01},0,1000000,
            {kind,raw_rate,logical_rate,raw_zero,logical_zero});
        check(bool(p),"right-adjoint source fixture unavailable");return *p;
    };
    const auto end=[](auto p,std::uint64_t n) {
        p.raw_captured_end=p.mapping.settled_end_sample=n;return p;
    };
    auto identity=source(16000,16000,Kind::fixed_nominal_audio,100,50);
    check(d::helper_frequency_ingress_raw_last(end(identity,101),16000,30,21)==100,
        "last actually captured identity endpoint or nonzero transport zeros were lost");
    check(!d::helper_frequency_ingress_raw_last(end(identity,100),16000,30,21),
        "exclusive SourceDate admitted equality at the first unavailable raw sample");
    check(!d::helper_frequency_ingress_raw_last(identity,16000,0,50),"empty transport prefix gained input authority");
    auto down=source(48000,16000,Kind::fixed_nominal_audio,100,50);
    check(d::helper_frequency_ingress_raw_last(end(down,276),16000,30,31)==275,
        "nominal3:1 right halo or checked bank origin was omitted");
    check(!d::helper_frequency_ingress_raw_last(end(down,275),16000,30,31),
        "nominal floor-only projection bypassed the FIR right halo");
    auto multi=source(96000,8000,Kind::fixed_nominal_audio);
    check(d::helper_frequency_ingress_raw_last(end(multi,13925),8000,0,1101)==13924,
        "multistage right adjoint differs from complete transport support");
    auto half=source(57600,14400,Kind::sampled_physical_halfband);
    check(d::helper_frequency_ingress_raw_last(end(half,385),14400,0,1)==384,
        "startup zero-extended halfband prefix was categorically rejected");
    check(!d::helper_frequency_ingress_raw_last(end(half,384),14400,0,1),
        "simulation right halo was omitted at startup");
    check(!d::helper_frequency_ingress_raw_last(half,14400,UINT64_MAX,1),"bank transport origin overflow wrapped");
    check(!d::helper_frequency_ingress_raw_last(half,14400,0,UINT64_MAX),"raw adjoint multiplication overflow wrapped");
    auto unknown=source(16000,16000,Kind::unknown);
    check(!d::helper_frequency_ingress_raw_last(unknown,16000,0,10),"equal Fs invented an unknown transport binding");
    auto wrong=identity;++wrong.raw_domain.provider_identity;
    check(!d::helper_frequency_ingress_raw_last(wrong,16000,30,21),"foreign provider borrowed an owned transport");
    wrong=identity;++wrong.transport_recipe.logical_rate;
    check(!d::helper_frequency_ingress_raw_last(wrong,16000,30,21),"different logical Fs borrowed a transport mapping");
    const auto map=d::LiveHelperClockCaptureMap::nominal_audio(
        d::LiveHelperClockTransportBasis::fixed_nominal_unshifted,identity.raw_domain,16000,UINT64_MAX,0);
    check(map&&!map->right_adjoint_last(2),"raw transport zero overflow wrapped");
}
void frequency_only_frame_without_clock_history() {
    m::Config c;c.sample_rate=6000;c.carrier_hz=1500;c.oscillator_search.emplace();
    c.oscillator_search->reference=m::OscillatorReference::shared_radio;
    c.oscillator_search->rf_shift_hz=30000000;
    c.oscillator_search->lf.accuracy_ppm=c.oscillator_search->rf.accuracy_ppm=100;
    c.oscillator_search->margin=3;
    h::Reference raw;raw.sample_rate=48960;raw.sample=raw.frequency_sample=489600;
    raw.frequency_first_sample=487600;raw.frequency_hz=5062.70625;raw.frequency_uncertainty_hz=.1;
    raw.frequency_fit=h::FrequencyFit::local_polynomial;raw.frequency_valid=true;
    raw.phase_valid=raw.timing_valid=raw.phase_observation_known=true;raw.punctuation.known=true;
    const auto source=d::helper_frequency_publication(c,2062.5,raw,12,{7,.01},0,raw.sample+200,
        {datapump::audio::CaptureTransportKind::fixed_nominal_audio,raw.sample_rate,c.sample_rate,0,0});
    check(bool(source),"standalone raw frequency source unavailable");
    const auto frame=d::helper_owned_frequency_frame(*source,c,2062.5);
    check(frame&&frame->actual_mapping==&source->mapping&&
        frame->frequency_observation_frontier==source->raw_captured_end&&
        frame->captured_first_sample==source->mapping.settled_first_sample&&
        frame->captured_end_sample==source->raw_captured_end,
        "frequency-only frame changed actual source domain/bounds/date");
    check(!frame->frequency.phase_valid&&!frame->frequency.timing_valid&&
        !frame->frequency.phase_observation_known&&!frame->frequency.punctuation.known,
        "frequency-only frame manufactured clock or phase authority");
    m::PatternAcquisitionScoredSupport scored;scored.first_sample=30000;scored.last_sample=40000;
    scored.consumed_end_sample=40000;scored.domain.sample_rate=c.sample_rate;
    const auto last=d::helper_frequency_scored_raw_last(*frame,c,scored,0,0,50000);
    check(bool(last),"full native support unavailable without optional clock History");
    check(!d::helper_owned_frequency_input(c,*frame,h::JointClockSourceRelation::physical_audio_unsteered,{},{}).valid,
        "frequency-only frame accepted without full native scored support");
    check(d::helper_owned_frequency_input(c,*frame,h::JointClockSourceRelation::physical_audio_unsteered,{},last).valid,
        "valid raw frequency depended on optional clock History funding");
    auto later=scored;later.first_sample=59000;later.last_sample=later.consumed_end_sample=61000;
    check(!d::helper_frequency_scored_raw_last(*frame,c,later,0,0,62000),
        "frequency-only native support escaped original SourceDate");
    auto no_mapping=*frame;no_mapping.actual_mapping=nullptr;
    check(!d::helper_owned_frequency_input(c,no_mapping,h::JointClockSourceRelation::physical_audio_unsteered,{},last).valid,
        "missing owned mapping fell back to synthetic legacy mapping");
    auto wrong_mapping=source->mapping;++wrong_mapping.phase_segment;
    no_mapping.actual_mapping=&wrong_mapping;
    check(!d::helper_owned_frequency_input(c,no_mapping,h::JointClockSourceRelation::physical_audio_unsteered,{},last).valid,
        "foreign raw gauge borrowed the frequency-only frame");
    auto unknown=*source;unknown.transport_recipe={};
    check(!d::helper_owned_frequency_frame(unknown,c,2062.5),"unknown transport created a frequency-only frame");
}
}
int main(){exact_owned_frequency_with_actual_clock_frame();whole_ingress_right_adjoint();
    frequency_only_frame_without_clock_history();
    std::cout<<"helper owned frequency/actual snapshot binding checks passed\n";}
