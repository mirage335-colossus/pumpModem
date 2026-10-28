#include "live_helper_clock.hpp"
#include "live_helper_prior.hpp"
#include "datapump/resampler.hpp"
#include "datapump/channel.hpp"
#include "datapump/streaming_modem.hpp"
#include <algorithm>
#include <iostream>
#include <vector>

using namespace datapump;
namespace d=datapump::live::detail;
namespace h=datapump::helper;
namespace {
void check(bool value,const char* why){if(!value)throw Error(why);}
h::PunctuationClockDomain domain(std::uint32_t rate){return {7,11,13,17,rate};}
modem::PatternAcquisitionScoredSupport support(std::uint64_t first,std::uint64_t last,
        std::uint32_t rate,std::uint64_t origin=0) {
    modem::PatternAcquisitionScoredSupport s;
    s.first_sample=first;s.last_sample=last;s.consumed_end_sample=last;
    s.domain.sample_rate=rate;s.domain.input_sample_origin=origin;return s;
}
void nominal_maps() {
    using Basis=d::LiveHelperClockTransportBasis;
    const auto identity=d::LiveHelperClockCaptureMap::nominal_audio(
        Basis::fixed_nominal_unshifted,domain(16000),16000,0,0);
    check(bool(identity),"fixed nominal identity capture declined");
    auto s=support(20,30,16000,1000);
    auto r=identity->expand(s,2000,8,0,10000);
    check(r && r.first_sample==3012 && r.last_sample==3038,"filter center was delayed twice or its support omitted");
    const auto down=d::LiveHelperClockCaptureMap::nominal_audio(
        Basis::fixed_nominal_unshifted,domain(48000),16000,0,0);
    r=down->expand(s,2000,8,0,10000);
    check(r && r.first_sample==8891 && r.last_sample==9259,"nominal3:1 support missed the actual FIR radius enclosure");
    check(!down->expand(s,2000,8,8892,10000),"unavailable prefix was clipped to fit the clock aperture");
    check(!down->expand(s,2000,8,0,9259),"exclusive raw end authorized its unobserved endpoint");
    check(bool(down->expand(s,2000,8,0,9260)),"last actually observed raw endpoint was rejected");
    const auto multi=d::LiveHelperClockCaptureMap::nominal_audio(
        Basis::fixed_nominal_unshifted,domain(96000),8000,0,0);
    r=multi->expand(support(1000,1100,8000),0,0,0,20000);
    check(r && r.first_sample==11276 && r.last_sample==13924,"actual nominal multistage floor/FIR support was not composed");
    const auto up=d::LiveHelperClockCaptureMap::nominal_audio(
        Basis::fixed_nominal_unshifted,domain(8000),48000,0,0);
    r=up->expand(support(1000,1001,48000),0,0,0,1000);
    check(r && r.first_sample==118 && r.last_sample==214,"fractional upsampling source floor changed support");
    const auto n=(1ULL<<54)+17;
    r=down->expand(support(n,n+1,16000),0,0,0,UINT64_MAX);
    check(r && r.first_sample==3*n-145 && r.last_sample==3*(n+1)+145,"large nominal dates lost integer-rational precision");
    const auto dated=d::LiveHelperClockCaptureMap::nominal_audio(
        Basis::fixed_nominal_unshifted,domain(16000),16000,UINT64_MAX-100,0);
    r=dated->expand(support(50,51,16000),0,0,0,UINT64_MAX);
    check(r && r.first_sample==UINT64_MAX-50 && r.last_sample==UINT64_MAX-49,"checked transport zero was not applied");
    check(!dated->expand(support(100,101,16000),0,0,0,UINT64_MAX),"raw transport origin overflow wrapped");
    check(!identity->expand(support(1,2,16000,UINT64_MAX),1,0,0,UINT64_MAX),"absolute receiver/bank origin overflow wrapped");
    check(!d::LiveHelperClockCaptureMap::nominal_audio(Basis::unknown,domain(48000),16000,0,0),
        "unknown/steered transport inherited nominal authority");
    check(!d::LiveHelperClockCaptureMap::simulation_halfband(Basis::fixed_nominal_unshifted,domain(48000),16000),
        "nonexistent3x simulation halfband geometry was invented");
}
void halfband_maps() {
    using Basis=d::LiveHelperClockTransportBasis;
    for(const auto factor:{4U,8U,16U,32U}) {
        const auto map=d::LiveHelperClockCaptureMap::simulation_halfband(
            Basis::fixed_nominal_unshifted,domain(14400*factor),14400);
        check(bool(map),"actual simulation halfband factor declined");
        const auto r=map->expand(support(100,110,14400,1000),2000,288,0,1000000);
        const auto radius=128ULL*(factor-1);
        check(r && r.first_sample==2812ULL*factor-radius && r.last_sample==3398ULL*factor+radius,
            "simulation prime/FIR support did not preserve exposed centered timestamps");
        check(!map->expand(support(0,1,14400),0,0,0,1000000),
            "simulation's initial zero-extended FIR history became observed marker evidence");
    }
}
h::Reference marker(double ppm,unsigned i,std::uint32_t rate) {
    const auto actual=(.24L+.4L*i)*rate/(1+static_cast<long double>(ppm)*1e-6L);
    const auto sample=static_cast<std::uint64_t>(std::floor(actual));
    h::Reference r;r.sample_rate=rate;r.sample=sample+100;
    r.punctuation={1,sample,sample-100,sample+100,static_cast<double>(actual-sample),.01,true};
    r.frequency_hz=5275+5275*ppm*1e-6;r.frequency_uncertainty_hz=.00001;
    r.frequency_first_sample=r.sample-100;r.frequency_sample=r.sample;
    r.frequency_fit=h::FrequencyFit::local_polynomial;r.frequency_valid=true;return r;
}
void history_lifecycle() {
    const auto raw=domain(57600);d::LiveHelperClockHistory history;
    history.reset(raw,5275);check(history.capture(raw,0,400000),"actual raw capture was not admitted");
    for(unsigned i=0;i<16;++i)
        check(history.observe(marker(100,i,raw.sample_rate),raw)==h::PunctuationClockReason::accepted,
            "distinct actual raw publication was omitted");
    d::LiveHelperClockSnapshot snapshot;
    check(history.snapshot_to(snapshot) && snapshot.clock.observation_count()==16 && snapshot.frequency_known,
        "bounded snapshot lost actual markers or independent raw frequency");
    const auto latest=marker(100,15,raw.sample_rate);
    check(history.observe(latest,raw)==h::PunctuationClockReason::duplicate_marker,
        "carried nominal publication became another actual observation");
    auto wrong=raw;++wrong.provider_identity;
    check(history.observe(latest,wrong)==h::PunctuationClockReason::domain_mismatch,
        "provider mismatch silently reset the active raw domain");
    check(history.snapshot_to(snapshot) && snapshot.clock.observation_count()==16,
        "delayed foreign provider erased newer raw clock history");
    check(!history.capture(raw,400001,410000),"discontinuous raw capture was treated as contiguous");
    check(!history.snapshot_to(snapshot) && !snapshot.capture_known,
        "discontinuity left a stale valid snapshot available");
    check(!history.capture(raw,400000,410000),"discontinuity repaired itself without explicit provider/gauge reset");
    history.reset(wrong,5275);check(history.capture(wrong,0,400000),"explicit provider handoff did not reset optional history");
    check(history.observe(latest,raw)==h::PunctuationClockReason::domain_mismatch,"old provider rolled a new owner back");
    auto tone=latest;tone.punctuation.known=false;
    check(history.observe(tone,wrong)==h::PunctuationClockReason::invalid_marker,
        "missing marker unexpectedly qualified clock");
    check(history.snapshot_to(snapshot) && snapshot.frequency_known && snapshot.clock.observation_count()==0,
        "punctuation failure invalidated independent frequency assistance");
}
void joint_bridge() {
    modem::Config c;c.sample_rate=14400;c.carrier_hz=1500;c.bandwidth_hz=360;
    c.pattern_symbols=true;c.constellation_bits=1;c.spreading_factor=64;c.scramble=true;c.dsss=true;c.dsss_factor=10;
    c.stream_epoch=1800000000;c.spreading_seed[3]=51;c.dsss_seed[13]=171;c.data_key=Crypto(c.spreading_seed);
    c.oscillator_search=modem::OscillatorSearchConfig{{100,0},{1,0},30000000,3};
    auto hops=std::make_shared<modem::FhssPlan>();hops->channels=3;hops->dwell_seconds=.4;
    hops->shifts[0]=1000000;hops->shifts[1]=1010000;hops->shifts[2]=1033000;
    hops->clock_error_seconds=hops->peer_clock_error_seconds=.001;hops->audio_error_seconds=.002;
    c.fake_hopping=modem::resolve_fhss_power(*hops,c);
    const auto raw=domain(57600);d::LiveHelperClockHistory history;history.reset(raw,5275);
    check(history.capture(raw,0,400000),"joint bridge raw capture failed");
    for(unsigned i=0;i<16;++i)check(history.observe(marker(100,i,raw.sample_rate),raw)==h::PunctuationClockReason::accepted,
        "joint bridge actual raw marker rejected");
    std::vector<modem::PatternFrequencyRateHypothesis> pairs;
    const auto step=.25*c.sample_rate/modem::symbol_sample_count(c);
    for(int f=-7;f<=7;++f)for(const auto ppm:{-600.,-400.,-200.,0.,200.,400.,600.})pairs.push_back({f*step,ppm});
    const auto utc=d::helper_utc_frequency_allowance(c,true,5275);check(utc.valid,"ordinary UTC envelope unavailable");
    const auto legacy=d::helper_prior_mask(pairs,5275*100e-6,
        d::helper_prior_detail::sum(.00001,utc.radius_hz).upper,c.carrier_hz-5275,
        .125*c.sample_rate/modem::symbol_sample_count(c),100.09,600.09,false);
    check(legacy.admitted && legacy.origin_hz==0,"production legacy all-LF frequency mask declined");
    auto s=support(10000,50000,14400);s.consumed_end_sample=48000;
    s.domain={pairs,std::span(legacy.cells).first(pairs.size()),0,0,0,5120,14400,0,.3515625,600.09,100.09};
    s.domain.legacy_mask_id=modem::detail::clock_prior_detail::mask_id(s.domain.legacy_frequency_mask);
    const auto map=d::LiveHelperClockCaptureMap::simulation_halfband(
        d::LiveHelperClockTransportBasis::fixed_nominal_unshifted,raw,14400);
    check(bool(map),"supported simulation transport map was unavailable");
    d::LiveHelperClockContext context;
    const auto refresh=[&](audio::CaptureTransportKind kind=audio::CaptureTransportKind::sampled_physical_halfband) {
        auto& snapshot=context.snapshot_target();
        const auto copied=history.snapshot_to(snapshot);
        snapshot.transport_recipe={kind,raw.sample_rate,14400,0,0};
        return copied;
    };
    check(refresh(),"raw snapshot not copied to the funded bank owner");
    d::LiveHelperClockRequest q;q.scored=s;q.fixed_filter_radius=288;q.input_captured_end_sample=99000;
    q.maximum_marker_age_samples=2*raw.sample_rate;
    q.clock_model=h::PunctuationClockModel::constant_rate_over_requested_support;q.mono_image_sign=1;
    check(context.prepare(c,q),"actual raw snapshot/full fixed-FIR support did not produce a joint certificate");
    check(context.result().raw_first_sample==38464 && context.result().raw_last_sample==201536,
        "bridge clipped or guessed the complete FFT/refinement/filter support");
    const auto prior=context.prior();check(prior.has_value(),"accepted bridge omitted joined-call prior");
    modem::PatternAcquisitionClockResult accepted;
    check(modem::detail::pattern_fft_clock_prior(*prior,s,accepted).has_value() &&
          accepted.selected_clock_rates>=3 && accepted.selected_clock_rates<accepted.original_clock_rates,
        "bridge confused physical clock with unresolved UTC DATA-clock alternatives");
    check(!context.prepare(c,q) && !context.prior() &&
          context.result().reason==d::LiveHelperClockReason::invalid_snapshot,
        "later batch reused a consumed snapshot and its stale captured frontier");
    check(refresh(),"fresh phase-view decline snapshot copy failed");
    q.scored.domain.input_phase_view_id=1;
    check(!context.prepare(c,q) && !context.prior() &&
          context.result().reason==d::LiveHelperClockReason::unsupported_input_view,
        "phase-view decline left a stale borrowed certificate");
    check(!context.prepare(c,q) && context.result().reason==d::LiveHelperClockReason::invalid_snapshot,
        "declined preparation did not consume its snapshot");
    check(refresh(),"fresh unavailable-end snapshot copy failed");
    q.scored=s;q.input_captured_end_sample=s.last_sample;
    check(!context.prepare(c,q) && !context.prior() &&
          context.result().reason==d::LiveHelperClockReason::unavailable_scored_support,
        "unobserved filtered endpoint was clipped to force qualification");
    check(refresh(),"fresh unavailable-prefix snapshot copy failed");
    q.input_captured_end_sample=99000;q.fixed_filter_radius=10001;
    check(!context.prepare(c,q) && !context.prior(),"unavailable filter prefix was silently dropped");
    check(refresh(),"second raw snapshot copy failed");
    q.fixed_filter_radius=288;q.clock_model=h::PunctuationClockModel::unknown;
    check(!context.prepare(c,q) && context.result().clock==h::PunctuationClockReason::unknown_cross_time_model,
        "preset accuracy silently supplied an affine thermal model");
    check(refresh(audio::CaptureTransportKind::unknown),"unknown-recipe test snapshot copy failed");
    q.clock_model=h::PunctuationClockModel::constant_rate_over_requested_support;
    check(!context.prepare(c,q) && !context.prior() &&
          context.result().reason==d::LiveHelperClockReason::unsupported_input_view,
        "raw/nominal rate equality manufactured actual transport provenance");
    check(refresh(audio::CaptureTransportKind::fixed_nominal_audio),"hardware conditional test snapshot copy failed");
    check(context.prepare(c,q) && !context.clock_result().explicit_zero_slew,
        "retrospectively bracketed conditional hardware aperture was permanently disabled");
    check(refresh(audio::CaptureTransportKind::fixed_nominal_audio),"hardware horizon test snapshot copy failed");
    q.declared_rate_slew_ppm_per_second=0;
    check(!context.prepare(c,q) && !context.prior() &&
          context.result().reason==d::LiveHelperClockReason::unsupported_input_view,
        "hardware clock acquired a made-up post-marker zero-slew horizon");

    // Reproduce the live authority mismatch without changing any threshold:
    // an independently current raw fit can be wider than the optional endpoint
    // mean previously used by the legacy prerequisite. The joint builder must
    // reject that prerequisite, then accept one derived from the EXACT same
    // raw record/model/support as its own input. No mean is forged into a raw
    // Reference and no joint cells are AND-clipped to make the old mask fit.
    d::LiveHelperClockSnapshot raw_frame;
    check(history.snapshot_to(raw_frame),"same-frequency regression raw snapshot unavailable");
    raw_frame.transport_recipe={audio::CaptureTransportKind::sampled_physical_halfband,raw.sample_rate,14400,0,0};
    raw_frame.frequency.frequency_uncertainty_hz=1.14;
    raw_frame.frequency.phase_valid=false;raw_frame.frequency.timing_valid=false;
    raw_frame.frequency.punctuation.known=false;
    const auto frequency=d::live_helper_clock_frequency_input(c,raw_frame,1);
    check(frequency.valid && frequency.reference.frequency_uncertainty_hz==1.14 &&
          frequency.reference.frequency_first_sample==raw_frame.frequency.frequency_first_sample &&
          frequency.reference.frequency_sample==raw_frame.frequency.frequency_sample &&
          frequency.reference.frequency_fit==raw_frame.frequency.frequency_fit &&
          frequency.mapping.capture_generation==raw.capture_generation &&
          frequency.mapping.timebase_identity==raw.input_view && frequency.mapping.phase_segment==raw.rf_gauge &&
          frequency.mapping.settled_first_sample==raw_frame.captured_first_sample &&
          frequency.mapping.settled_end_sample==raw_frame.captured_end_sample &&
          !frequency.model.audio_maximum_fractional_slew_per_second &&
          !frequency.model.rf_maximum_fractional_slew_per_second,
        "raw prerequisite changed provider frequency support/model or required optional punctuation");
    std::vector<modem::PatternFrequencyRateHypothesis> wide_pairs;
    for(int f=-10;f<=10;++f)for(const auto ppm:{-600.,-400.,-200.,0.,200.,400.,600.})wide_pairs.push_back({f*step,ppm});
    const auto old_mean=d::helper_prior_mask(wide_pairs,5275*100e-6,
        d::helper_prior_detail::sum(.00001,utc.radius_hz).upper,c.carrier_hz-5275,
        .125*c.sample_rate/modem::symbol_sample_count(c),100.09,600.09,false);
    const auto same_raw=d::helper_prior_mask(wide_pairs,frequency.offset_hz,
        d::helper_prior_detail::sum(frequency.uncertainty_hz,utc.radius_hz).upper,c.carrier_hz-5275,
        .125*c.sample_rate/modem::symbol_sample_count(c),100.09,600.09,false);
    check(old_mean.admitted && same_raw.admitted && same_raw.selected>old_mean.selected,
        "same-record regression did not exercise a wider independently current raw prerequisite");
    auto raw_support=s;
    raw_support.domain.original_pairs=wide_pairs;
    raw_support.domain.legacy_frequency_mask=std::span(old_mean.cells).first(wide_pairs.size());
    raw_support.domain.legacy_mask_id=modem::detail::clock_prior_detail::mask_id(raw_support.domain.legacy_frequency_mask);
    q.scored=raw_support;q.declared_rate_slew_ppm_per_second.reset();
    context.snapshot_target()=raw_frame;
    check(!context.prepare(c,q) && context.clock_result().qualified_for_acquisition &&
          context.result().cells.reason==h::JointClockCellReason::legacy_frequency_mask_too_narrow,
        "wider raw joint cells silently waived or intersected the narrower mean prerequisite");
    raw_support.domain.legacy_frequency_mask=std::span(same_raw.cells).first(wide_pairs.size());
    raw_support.domain.legacy_mask_id=modem::detail::clock_prior_detail::mask_id(raw_support.domain.legacy_frequency_mask);
    q.scored=raw_support;context.snapshot_target()=raw_frame;
    check(context.prepare(c,q) && context.prior() &&
          context.result().cells.reason==h::JointClockCellReason::accepted &&
          context.result().cells.selected_clock_rates<context.result().cells.original_clock_rates,
        "same actual raw publication/model did not support the UTC-preserving joint mask");
    auto invalid_frame=raw_frame;invalid_frame.frequency.sample=invalid_frame.captured_end_sample;
    check(!d::live_helper_clock_frequency_input(c,invalid_frame,1).valid,
        "exclusive captured endpoint gained current raw-frequency authority");
    invalid_frame=raw_frame;invalid_frame.frequency.sample_rate=14400;
    check(!d::live_helper_clock_frequency_input(c,invalid_frame,1).valid,
        "logical-rate frequency record was silently substituted for the raw provider");

    // A queued owned frequency record keeps its original actual observed end;
    // current marker/capture support stays separate and cannot be backdated.
    auto retrospective=raw_frame;
    retrospective.frequency=d::HelperMeasurement(marker(100,7,raw.sample_rate));
    retrospective.frequency.frequency_uncertainty_hz=raw_frame.frequency.frequency_uncertainty_hz;
    const auto scored_raw=map->expand(raw_support,0,288,retrospective.captured_first_sample,
        retrospective.captured_end_sample);
    check(bool(scored_raw),"retrospective fixture lacks its actual full mapped scored support");
    // Synthetic unit dates isolate the exact exclusive boundary; actual Live
    // obtains this immutable value from the owned publication, not this query.
    const auto source_date=scored_raw.last_sample+1;
    const auto undated=d::live_helper_clock_frequency_input(c,retrospective,1);
    check(!undated.valid && undated.measured.reason==h::LinkReason::stale_measurement,
        "an unowned old raw fit silently became current after capture advanced");
    retrospective.frequency_observation_frontier=source_date;
    check(!d::live_helper_clock_frequency_input(c,retrospective,1).valid,
        "owned raw frequency skipped proof of full scored FFT/FIR/refinement support");
    const auto dated=d::live_helper_clock_frequency_input(c,retrospective,1,
        h::JointClockSourceRelation::physical_audio_unsteered,{},scored_raw.last_sample);
    check(dated.valid && dated.reference.frequency_sample==retrospective.frequency.frequency_sample &&
          dated.reference.frequency_first_sample==retrospective.frequency.frequency_first_sample &&
          dated.mapping.settled_end_sample==retrospective.captured_end_sample &&
          dated.measured.state_sample==retrospective.frequency.frequency_sample,
        "owned SourceDate changed raw fit support or replaced actual capture bounds");
    check(!d::live_helper_clock_frequency_input(c,retrospective,1,
            h::JointClockSourceRelation::physical_audio_unsteered,{},source_date).valid,
        "owned frequency narrowed a scored aperture one sample past its observed support");
    auto retrospective_request=q;
    retrospective_request.maximum_marker_age_samples=10ULL*raw.sample_rate;
    context.snapshot_target()=retrospective;
    check(context.prepare(c,retrospective_request) && context.prior() &&
          context.result().raw_captured_end_sample==retrospective.captured_end_sample,
        "joint validation re-dated the owned frequency at the later marker frontier");
    invalid_frame=retrospective;invalid_frame.frequency_observation_frontier=invalid_frame.frequency.sample;
    check(!d::live_helper_clock_frequency_input(c,invalid_frame,1,
            h::JointClockSourceRelation::physical_audio_unsteered,{},scored_raw.last_sample).valid,
        "frequency publication at its exclusive SourceDate was treated as observed");
    invalid_frame=retrospective;invalid_frame.frequency_observation_frontier=invalid_frame.captured_end_sample+1;
    check(!d::live_helper_clock_frequency_input(c,invalid_frame,1,
            h::JointClockSourceRelation::physical_audio_unsteered,{},scored_raw.last_sample).valid,
        "owned frequency SourceDate escaped actual captured PCM");
    invalid_frame=retrospective;invalid_frame.frequency_observation_frontier=scored_raw.last_sample;
    context.snapshot_target()=invalid_frame;
    check(!context.prepare(c,retrospective_request) &&
          context.result().reason==d::LiveHelperClockReason::invalid_frequency_policy,
        "joint preparation ignored the full mapped support's exclusive SourceDate boundary");
    retrospective.captured_end_sample+=2ULL*raw.sample_rate;
    retrospective_request.maximum_marker_age_samples=2ULL*raw.sample_rate;
    context.snapshot_target()=retrospective;
    check(!context.prepare(c,retrospective_request) &&
          context.result().clock==h::PunctuationClockReason::stale_marker,
        "old frequency SourceDate backdated actual punctuation freshness");
    retrospective_request.maximum_marker_age_samples=10ULL*raw.sample_rate;
    retrospective_request.scored.last_sample=90000;
    retrospective_request.input_captured_end_sample=130000;
    retrospective.frequency_observation_frontier=raw_frame.captured_end_sample;
    retrospective.frequency=raw_frame.frequency;
    context.snapshot_target()=retrospective;
    check(!context.prepare(c,retrospective_request) && !context.prior() &&
          context.result().reason==d::LiveHelperClockReason::unqualified_audio_clock &&
          context.result().raw_first_sample==38464 && context.result().raw_last_sample==361536 &&
          context.result().clock==h::PunctuationClockReason::unsupported_future_model,
        "owned frequency SourceDate authorized post-marker clock use without an explicit future model");
    // The SourceDate permits this captured frequency support, not new markers.
    // Keep a separate left-support negative: its right edge is bracketed by
    // actual markers, but its mapped first sample precedes the first marker.
    retrospective_request.scored.first_sample=3600;
    retrospective_request.scored.last_sample=50000;
    context.snapshot_target()=retrospective;
    check(!context.prepare(c,retrospective_request) && !context.prior() &&
          context.result().reason==d::LiveHelperClockReason::unqualified_audio_clock &&
          context.result().raw_first_sample==12864 && context.result().raw_last_sample==201536 &&
          context.result().clock==h::PunctuationClockReason::uncovered_scored_support,
        "owned frequency SourceDate authorized clock support before the first actual marker");
}
void common_source_cells() {
    modem::Config c;c.sample_rate=14400;c.carrier_hz=1500;c.bandwidth_hz=360;
    c.pattern_symbols=true;c.constellation_bits=1;c.spreading_factor=64;c.scramble=true;c.dsss=true;c.dsss_factor=10;
    c.oscillator_search=modem::OscillatorSearchConfig{{100,0},{1,0},30000000,3};
    const auto raw=domain(57600);const auto step=.25*c.sample_rate/modem::symbol_sample_count(c);
    std::vector<modem::PatternFrequencyRateHypothesis> pairs;
    for(int f=-16;f<=16;++f)for(const auto ppm:{-600.,-400.,-200.,0.,200.,400.,600.})pairs.push_back({f*step,ppm});
    std::size_t maximum_selected=0;
    for(const auto H:{5275.,-5275.})for(const auto b:{-550.,-100.,100.,550.}) {
        d::LiveHelperClockHistory history;history.reset(raw,H);
        check(history.capture(raw,0,400000),"common source actual capture declined");
        for(unsigned i=0;i<16;++i)check(history.observe(marker(b,i,raw.sample_rate),raw)==h::PunctuationClockReason::accepted,
            "common-source marker outside the physical-only box was discarded");
        d::LiveHelperClockSnapshot frame;check(history.snapshot_to(frame),"common-source snapshot unavailable");
        frame.transport_recipe={audio::CaptureTransportKind::sampled_physical_halfband,raw.sample_rate,c.sample_rate,0,0};
        const auto relation=h::JointClockSourceRelation::common_corrected_robust_source;
        const auto frequency=d::live_helper_clock_frequency_input(c,frame,H>0?1:-1,relation,600.09);
        check(frequency.valid && frequency.model.audio_accuracy_fraction>=600.09e-6 &&
              frequency.model.rf_accuracy_fraction<4e-6,"common source replaced physical RF or kept the narrower a-only prior");
        const auto legacy=d::helper_prior_mask(pairs,frequency.offset_hz,frequency.uncertainty_hz,
            c.carrier_hz-H,.125*c.sample_rate/modem::symbol_sample_count(c),100.09,600.09,false);
        check(legacy.admitted,"common source full original DATA-clock frequency strip not covered");
        auto s=support(10000,50000,c.sample_rate);s.consumed_end_sample=48000;
        s.domain={pairs,std::span(legacy.cells).first(pairs.size()),0,0,0,5120,c.sample_rate,0,.3515625,600.09,100.09};
        s.domain.legacy_mask_id=modem::detail::clock_prior_detail::mask_id(s.domain.legacy_frequency_mask);
        d::LiveHelperClockRequest q;q.scored=s;q.fixed_filter_radius=288;q.input_captured_end_sample=99000;
        q.maximum_marker_age_samples=2*raw.sample_rate;q.clock_model=h::PunctuationClockModel::constant_rate_over_requested_support;
        q.mono_image_sign=H>0?1:-1;q.source_relation=relation;
        d::LiveHelperClockContext context;context.snapshot_target()=frame;
        check(context.prepare(c,q),"shared source b fit was expanded by UTC c a second time");
        const auto prior=context.prior();check(prior.has_value(),"shared source prior missing");
        check(context.result().cells.source_relation==relation && context.result().cells.utc_correction_fraction>0 &&
              context.result().cells.selected_clock_rates<=2,
            "common source resolved c as zero or retained duplicate independent UTC lanes");
        maximum_selected=std::max(maximum_selected,context.result().cells.selected_pairs);
        // All compatible physical-a/UTC-c branches remain possible. Their
        // product b was MEASURED; no individual branch is selected or set0.
        const auto C=modem::utc_transmit_rate_limit(c);
        const auto& fit=context.clock_result();
        for(const auto measured_b:{fit.physical_audio_lower_ppm,b,fit.physical_audio_upper_ppm})
            for(unsigned i=0;i<=40;++i) {
                const long double correction=-C+2*static_cast<long double>(C)*i/40;
                const long double audio=((1+static_cast<long double>(measured_b)*1e-6L)/(1+correction)-1)*1e6L;
                if(std::abs(audio)>300)continue;
                for(const auto helper:{frequency.measured.helper_residual_hz.lower,frequency.measured.helper_residual_hz.upper}) {
                    const auto target=static_cast<long double>(helper)+(c.carrier_hz-H)*static_cast<long double>(measured_b)*1e-6L;
                    bool covered=false;
                    for(std::size_t f=0;f<pairs.size();++f)if(prior->pair_mask()[f])
                        covered|=std::abs(static_cast<long double>(pairs[f].clock_error_ppm)-measured_b)<=100.09L &&
                            std::abs(static_cast<long double>(pairs[f].frequency_offset_hz)-target)<=.3515625L;
                    check(covered,"common source omitted a compatible nonzero UTC/physical-clock truth cell");
                }
            }
        if(std::abs(b)>300) {
            q.source_relation=h::JointClockSourceRelation::physical_audio_unsteered;
            context.snapshot_target()=frame;
            check(!context.prepare(c,q),"unknown/separate source relation inherited the wider measured DATA-clock authority");
        }
        q.source_relation=relation;q.scored.domain.input_phase_view_id=1;context.snapshot_target()=frame;
        check(!context.prepare(c,q) && context.result().reason==d::LiveHelperClockReason::unsupported_input_view,
            "common-source attestation authorized an unrelated phase/time-warped input");
    }
    std::cout<<"common source maximum selected original cells="<<maximum_selected<<" of "<<pairs.size()<<"\n";
}
// Actual composite auxiliary observations carry only marker provenance.
// Frequency/phase/timing assistance never depends on this optional worker.
std::optional<h::Reference> take_auxiliary_marker(h::AuxiliaryMetadataReceiver& receiver,
        std::uint32_t rate,std::uint64_t captured_end) {
    check(!receiver.take_update(),"unmodulated auxiliary clock fixture invented metadata");
    const auto marker=receiver.take_punctuation_update();
    if(!marker.known)return {};
    check(marker.first_sample<=marker.sample && marker.sample<=marker.last_sample && marker.last_sample<captured_end,
        "composite auxiliary marker escaped its actual captured support");
    check(marker.last_sample-marker.first_sample+1>=static_cast<std::uint64_t>(.09*rate),
        "composite auxiliary marker omitted raw filter dependencies");
    h::Reference evidence;evidence.sample_rate=rate;evidence.sample=marker.last_sample;evidence.punctuation=marker;
    check(!evidence.frequency_valid && !evidence.phase_valid && !evidence.timing_valid,
        "auxiliary clock observation acquired frequency/phase/timing authority");
    return evidence;
}
// Actual hardware composition seam: the real adjustable Resampler calls the
// real Generator with its continuous corrected SOURCE seconds, exactly as
// PlaybackSource/Live do. The receiver sees only output PCM and dated markers.
void actual_corrected_source() {
    constexpr std::uint32_t rate=57600;
    for(const auto correction:{-.0003,.0003}) {
        h::Options options;options.power_w=15000;options.data_power_w=4;options.timecode=false;
        const auto plan=h::plan(options,1500,100,rate);
        h::Generator generator(options,plan,1800000000,true);
        audio::Resampler playback(14400,rate,true);playback.initialize_timing(0,correction);
        long double first_source=0,last_source=0;std::uint64_t generated=0;
        playback.set_output_transform([&](float payload,long double seconds,std::uint32_t output_rate) {
            if(!generated)first_source=seconds;last_source=seconds;++generated;
            return generator.mix(payload,seconds,output_rate);
        });
        h::AuxiliaryMetadataReceiver receiver(rate,plan.auxiliary_frequency_hz,plan.frequency_hz,{}, {},
            h::AuxiliaryMetadataReceiver::workspace_required(rate));
        h::PunctuationClock clock;
        const auto raw=domain(rate);clock.reset(raw);
        std::vector<float> input(14400*5,0),output(113);
        std::size_t consumed=0;std::uint64_t observed=0;h::Reference first,last;std::size_t distinct=0;
        while(!playback.finished()) {
            const auto progress=playback.process(std::span(input).subspan(consumed),output,true);consumed+=progress.consumed;
            if(progress.produced) {
                receiver.push(std::span(output).first(progress.produced),observed);observed+=progress.produced;
                receiver.advance();
                if(auto r=take_auxiliary_marker(receiver,rate,observed)) {
                    const auto status=clock.observe(*r,raw,observed);
                    check(status==h::PunctuationClockReason::accepted || status==h::PunctuationClockReason::duplicate_marker,
                        "actual corrected-source auxiliary marker lost its clock provenance");
                    if(status==h::PunctuationClockReason::accepted){if(!distinct++)first=*r;last=*r;}
                }
            }
            check(progress.consumed || progress.produced || playback.finished(),"actual corrected source stalled");
        }
        check(generated==observed && observed>rate*4 && distinct>=8,"actual corrected helper source did not expose distinct markers");
        const auto rate_truth=(last_source-first_source)*rate/(generated-1)-1;
        check(std::abs(rate_truth-correction)<1e-10L,"helper callback did not retain the DATA resampler correction");
        h::PunctuationClockRequest q{first.punctuation.sample+100,last.punctuation.sample-100,observed,2ULL*rate,
            h::PunctuationClockModel::constant_rate_over_requested_support,{}};
        const auto fit=clock.qualify({-600.09,600.09},q);
        check(fit.qualified_for_acquisition && fit.physical_audio_lower_ppm<=correction*1e6 &&
              fit.physical_audio_upper_ppm>=correction*1e6,
            "actual hardware-source Generator/Resampler/Receiver lost nonzero UTC correction");
        std::cout<<"actual corrected source c="<<correction*1e6<<"ppm measured=["<<fit.physical_audio_lower_ppm<<","<<fit.physical_audio_upper_ppm<<"]\n";
    }
}
void actual_sampled_common_source() {
    for(const auto b:{-550.,550.}) {
        modem::Config c;c.sample_rate=14400;c.carrier_hz=1500;c.bandwidth_hz=360;
        c.pattern_symbols=true;c.constellation_bits=1;c.spreading_factor=64;c.scramble=true;c.dsss=true;c.dsss_factor=10;
        c.oscillator_search=modem::OscillatorSearchConfig{{100,0},{1,0},30000000,3};
        c.stream_epoch=1800000000;c.spreading_seed[3]=51;c.dsss_seed[13]=171;c.data_key=Crypto(c.spreading_seed);
        h::Options options;options.power_w=15000;options.data_power_w=4;options.timecode=false;
        modem::ChannelConfig impairment;impairment.clock_error_ppm=b;impairment.frequency_offset_hz=0;
        impairment.snr_db=200;
        modem::SampledSimulationChannel channel(c,impairment,options);
        const auto rate=channel.physical_sample_rate();const auto raw=domain(rate);
        const auto plan=h::plan(options,h::placement_band(c),rate,0,modem::outer_dsss_transmit_gain(c));
        h::AuxiliaryMetadataReceiver receiver(rate,plan.auxiliary_frequency_hz,plan.frequency_hz,{}, {},
            h::AuxiliaryMetadataReceiver::workspace_required(rate));
        h::PunctuationClock clock;clock.reset(raw);
        h::Reference first,last;std::size_t distinct=0;std::uint64_t raw_end=0;
        channel.set_physical_observer([&](std::span<const float> samples,std::uint32_t fs,std::uint64_t at) {
            check(fs==rate && at==raw_end,"actual sampled common-source capture lost contiguous coordinates");
            receiver.push(samples,at);raw_end=at+samples.size();
            receiver.advance();
            if(auto r=take_auxiliary_marker(receiver,fs,raw_end)) {
                const auto status=clock.observe(*r,raw,raw_end);
                check(status==h::PunctuationClockReason::accepted || status==h::PunctuationClockReason::duplicate_marker,
                    "actual sampled-source auxiliary marker lost its clock provenance");
                if(status==h::PunctuationClockReason::accepted){if(!distinct++)first=*r;last=*r;}
            }
        });
        modem::StreamingTransmitter source(Bytes{0,1,0,1},c);std::vector<float> output(113);
        while(channel.received_samples()<6ULL*c.sample_rate) {
            const auto count=channel.read(source,output);check(count,"actual sampled source ended before its declared clock aperture");
        }
        check(distinct>=8,"actual sampled Generator/Receiver lacked distinct source markers");
        h::PunctuationClockRequest q{first.punctuation.sample+100,last.punctuation.sample-100,raw_end,2ULL*rate,
            h::PunctuationClockModel::constant_rate_over_requested_support,0.};
        const auto fit=clock.qualify({-600.09,600.09},q);
        check(fit.qualified_for_acquisition && fit.physical_audio_lower_ppm<=b && fit.physical_audio_upper_ppm>=b,
            "actual sampled helper did not share the DATA source clock outside the physical-a-only prior");
        const auto a=b>0?275.L:-275.L;
        const auto correction=((1+static_cast<long double>(b)*1e-6L)/(1+a*1e-6L)-1)*1e6L;
        const auto C=modem::utc_transmit_rate_limit(c);
        check(C>0 && std::abs(a)<=c.oscillator_search->margin*c.oscillator_search->lf.accuracy_ppm &&
              correction!=0 && std::abs(correction)<=static_cast<long double>(C)*1e6L,
            "outside-a source case lost a compatible nonzero permitted UTC factor");
        std::cout<<"actual sampled shared source b="<<b<<"ppm compatible a="<<static_cast<double>(a)
            <<"ppm c="<<static_cast<double>(correction)<<"ppm measured=["<<fit.physical_audio_lower_ppm
            <<","<<fit.physical_audio_upper_ppm<<"]\n";
    }
}
}
int main() {
    try {
        nominal_maps();halfband_maps();history_lifecycle();joint_bridge();common_source_cells();actual_corrected_source();actual_sampled_common_source();
        std::cout<<"PASS Live helper raw-clock bridge maps, provenance, support and UTC-preserving certificate\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
