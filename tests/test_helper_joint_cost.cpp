// Component-only metadata/cost seam; the actual full payload/rolling/cable
// acceptance fixtures remain independent. No known timestamp reaches the cost
// model; this one ready batch tests copying a real trusted synchronous result.
#define main accepted_joint_clock_full_main
#include "test_helper_joint_clock.cpp"
#undef main
#include "datapump/simulation_estimate.hpp"
#include <array>

namespace {
void executed_cost_copy() {
    auto c=config();c.outer_dsss_version=modem::OuterDsssVersion::interleaved_v2;
    const auto original=modem::oscillator_pattern_search(c,modem::utc_transmit_rate_limit(c));
    auto s=search(c,0);s.hypotheses=original.hypotheses;
    s.admitted_clock_half_width_ppm=original.requested_clock_half_width_ppm;
    s.admitted_clock_cell_half_width_ppm=original.clock_step_ppm/2;
    modem::PatternReceiver receiver(c,64*1024*1024,s);
    const auto actual=mapping(5275);const auto raw=frequency(actual,5275*100e-6,.005,99999);
    h::LinkState link;check(link.reset(frequency_policy()),"joint cost frequency policy declined");
    const auto observed=link.observe({raw,actual,{}},100000);
    const auto legacy=safe_legacy_mask(c,s,observed,actual);
    check(receiver.set_acquisition_prior(0,legacy),"joint cost original frequency mask declined");
    const auto observe_until_ready=[&] {
        while(!receiver.acquisition_state().ready) {
            const auto state=receiver.acquisition_state();check(state.supported&&state.input_room_samples,"joint cost observation stalled");
            const auto count=std::min<std::size_t>(113,state.input_room_samples);
            check(receiver.observe_acquisition(std::vector<float>(count))==count,"joint cost observation lost samples");
        }
    };
    const auto clock=synthetic_clock(100);
    observe_until_ready();
    auto scored=receiver.acquisition_scored_support();check(bool(scored),"joint cost lost first ready support");
    auto fit=qualify(clock,scored->first_sample,scored->last_sample,100000);
    // The first actual FFT support starts at sample0, before the first dated
    // marker near.24s. A zero-slew assumption cannot backdate that evidence.
    // Keep this formerly failing condition as an explicit negative regression.
    std::cout<<"joint_cost_initial_clock reason="<<h::punctuation_clock_reason(fit.reason)
        <<" first="<<scored->first_sample<<" first_marker="<<fit.first_observation_sample
        <<" original_pairs="<<s.hypotheses.size()<<'\n';
    check(!fit.qualified_for_acquisition&&fit.reason==h::PunctuationClockReason::uncovered_scored_support&&
        scored->first_sample<fit.first_observation_sample,
        "first joint cost aperture gained clock authority before the first actual marker");
    std::size_t ordinary_batches=0;
    while(!fit.qualified_for_acquisition) {
        check(fit.reason==h::PunctuationClockReason::uncovered_scored_support&&
            scored->first_sample<fit.first_observation_sample,
            "joint cost clock declined for a reason other than the preserved initial support gap");
        check(ordinary_batches<16,"joint cost ordinary batches did not reach actual dated marker support");
        // Score and resolve the real ordinary all-LF frequency-only batch.
        // Do not crop, advance or rewrite the receiver's scored-support dates.
        receiver.process_prior_acquisition();receiver.resolve_acquisition(true);++ordinary_batches;
        observe_until_ready();scored=receiver.acquisition_scored_support();
        check(bool(scored),"joint cost lost actual warmed ready support");
        fit=qualify(clock,scored->first_sample,scored->last_sample,100000);
    }
    const auto before=receiver.acquisition_state();const auto bound=receiver.acquisition_work_bound();
    check(bound&&scored,"joint cost lost actual ready support");
    h::JointClockCellCertificate certificate;
    const auto built=certificate.build(c,request(*scored,100000),fit,{raw,actual,{}},frequency_policy(),raw_domain());
    if(!built)std::cerr<<"joint_cost_decline reason="<<static_cast<unsigned>(certificate.result().reason)
        <<" clock_reason="<<h::punctuation_clock_reason(fit.reason)<<" ordinary_batches="<<ordinary_batches
        <<" first="<<scored->first_sample<<" last="<<scored->last_sample
        <<" original_pairs="<<s.hypotheses.size()<<" original_selected="<<before.selected_pairs<<'\n';
    check(built,"joint cost trusted original full-catalogue builder declined");
    const auto prior=certificate.prior();check(prior.has_value(),"joint cost trusted prior missing");
    simulation::HelperFrequencyInputMap input;input.physical_sample_rate=input.logical_sample_rate=c.sample_rate;
    input.physical_passband_upper_hz=std::nextafter(c.sample_rate/2.,0.);input.payload_filter_hz=5275;
    // This component's synthetic helper is explicitly physical-a/unsteered;
    // a live common-source tag comes only from the actual joined builder.
    input.joint_source_relation=simulation::HelperJointClockSourceRelation::physical_audio_unsteered;
    // Functional default-result negative, not a hostile-caller/security test:
    // this API trusts the joined producer to pass its actual synchronous result.
    modem::PatternAcquisitionClockResult executed;
    check(!simulation::copy_executed_helper_joint_clock(c,*prior,executed,before,*bound,input,1024*1024).proof,
        "default unexecuted result activated conditional cost");
    // The API's bool reports an ordinary accepted link/bit, independently of
    // numerical execution. This zero-PCM cost fixture deliberately supplies no
    // payload; execution is certified by the synchronous result and work delta.
    const auto jobs_before=receiver.fft_work().template_jobs;
    const auto link_accepted=receiver.process_prior_acquisition(*prior,executed);
    const auto jobs_after=receiver.fft_work().template_jobs;
    const auto& selected=certificate.result();
    check(executed.completed&&executed.narrowed&&
        executed.status==modem::PatternAcquisitionClockStatus::active&&
        executed.executed_template_jobs>0&&jobs_after>=jobs_before&&
        executed.executed_template_jobs==jobs_after-jobs_before&&
        executed.original_pairs==selected.original_pairs&&executed.selected_pairs==selected.selected_pairs&&
        executed.original_clock_rates==selected.original_clock_rates&&executed.selected_clock_rates==selected.selected_clock_rates&&
        executed.selected_pairs>0&&executed.selected_pairs<executed.original_pairs,
        "joint cost real native execution declined");
    std::cout<<"joint_cost_execution completed="<<executed.completed<<" narrowed="<<executed.narrowed
        <<" jobs="<<executed.executed_template_jobs<<" selected="<<executed.selected_pairs
        <<'/'<<executed.original_pairs<<" link_accepted="<<link_accepted<<'\n';
    const auto copied=simulation::copy_executed_helper_joint_clock(c,*prior,executed,before,*bound,input,1024*1024);
    check(bool(copied.proof),copied.reason.c_str());
    check(copied.metadata.source_relation==input.joint_source_relation,
        "joint cost copy lost the actual producer source relation");
    auto unknown_source=input;unknown_source.joint_source_relation=simulation::HelperJointClockSourceRelation::unknown;
    check(!simulation::copy_executed_helper_joint_clock(c,*prior,executed,before,*bound,unknown_source,1024*1024).proof&&
        !simulation::same_helper_joint_clock_geometry(copied.proof,c,*prior,unknown_source),
        "unknown source-clock provenance activated or reused an executed joint copy");
    auto changed_source=input;changed_source.joint_source_relation=simulation::HelperJointClockSourceRelation::common_corrected_robust_source;
    check(!simulation::same_helper_joint_clock_geometry(copied.proof,c,*prior,changed_source),
        "changed source-clock relation reused an older joint diagnostic lease");
    check(std::string(simulation::helper_joint_clock_source_domain(changed_source.joint_source_relation)).find(
        "individually unresolved and are not added again")!=std::string::npos&&
        std::string(simulation::helper_joint_clock_source_domain(input.joint_source_relation)).find(
        "unresolved UTC DATA-clock union remain")!=std::string::npos,
        "joint source descriptions conflated common DATA clock with physical-a/UTC union");
    check(copied.metadata.original_pairs==s.hypotheses.size()&&
        copied.metadata.frequency_only_pairs==before.selected_pairs&&
        copied.metadata.joint_pairs==executed.selected_pairs&&
        copied.metadata.joint_clock_rates==executed.selected_clock_rates&&
        copied.metadata.joint_pairs<copied.metadata.frequency_only_pairs&&
        copied.metadata.joint_clock_rates>1,
        "joint cost lost actual pair/clock counts or unresolved UTC union");
    check(!simulation::copy_executed_helper_joint_clock(c,*prior,executed,before,*bound,input,copied.metadata.owned_bytes-1).proof,
        "optional metadata copy consumed unfunded storage");
    auto transformed=input;transformed.frequency_correction_hz=1;
    check(!simulation::copy_executed_helper_joint_clock(c,*prior,executed,before,*bound,transformed,1024*1024).proof,
        "joint cost accepted unproved transformed input basis");
    auto changed=executed;++changed.selected_pairs;
    check(!simulation::copy_executed_helper_joint_clock(c,*prior,changed,before,*bound,input,1024*1024).proof,
        "joint cost accepted mismatched executed mask counts");
    // Runtime publication has exactly two fixed numeric slots in a separate
    // GUI account, independent of the native receiver's DSP reservation.
    const auto native_bytes=receiver.working_bytes(),native_reserved=receiver.reserved_workspace_bytes();
    auto pool=simulation::make_helper_joint_clock_diagnostic_pool(simulation::helper_joint_clock_diagnostic_gui_grant_bytes());
    check(bool(pool),"separate fixed GUI diagnostic pool unavailable");
    auto slot0=simulation::copy_executed_helper_joint_clock(pool,c,*prior,executed,before,*bound,input);
    auto slot1=simulation::copy_executed_helper_joint_clock(pool,c,*prior,executed,before,*bound,input);
    check(slot0.proof&&slot1.proof,"two funded diagnostic slots did not publish");
    auto borrowed=slot0.proof;slot0.proof.reset();
    const auto both=simulation::helper_joint_clock_diagnostic_status(pool);
    check(both.slots==2&&both.occupied_slots==2&&both.slot_bytes>=copied.metadata.owned_bytes&&
        both.allocator_requested_bytes<=simulation::helper_joint_clock_diagnostic_gui_grant_bytes(),
        "GUI diagnostic resource accounting lost an old Snapshot borrower");
    check(!simulation::copy_executed_helper_joint_clock(pool,c,*prior,executed,before,*bound,input).proof,
        "busy diagnostic pool made a third slot or fallback vector allocation");
    slot1.proof.reset();
    auto reused=simulation::copy_executed_helper_joint_clock(pool,c,*prior,executed,before,*bound,input);
    check(bool(reused.proof)&&simulation::helper_joint_clock_diagnostic_status(pool).occupied_slots==2,
        "last-borrower release did not return exactly one diagnostic slot");
    check(native_bytes==receiver.working_bytes()&&native_reserved==receiver.reserved_workspace_bytes(),
        "GUI diagnostic leases consumed ordinary receiver/DSP capacity");
    std::weak_ptr<simulation::HelperJointClockDiagnosticPool> lifetime=pool;pool.reset();
    check(!lifetime.expired(),"Bank/Session release invalidated external numeric borrowers");
    borrowed.reset();reused.proof.reset();
    check(lifetime.expired(),"fixed diagnostic pool outlived its final external borrower");
    receiver.resolve_acquisition(true); // copied proof owns every former borrowed span
    transfer::Options options;options.modem=c;options.key=c.data_key;options.search_seconds=3;
    options.dsp_workspace_bytes=128*1024*1024;options.audio_timing_error_seconds=0;
    const auto tx=transfer::estimate_binary(std::array<std::uint8_t,1>{1},options);
    modem::ChannelConfig channel;channel.clock_error_ppm=100;channel.frequency_offset_hz=0;
    const auto scenario=simulation::helper_executed_joint_scenario(tx,options,channel,copied.proof);
    check(scenario.scenario.has_value(),scenario.reason.c_str());
    check(scenario.reason.find("Independent unsteered helper")!=std::string::npos&&
        scenario.scenario->profiles[0].input.joint_source_relation==input.joint_source_relation,
        "joint scenario lost the copied source relation or described the wrong unresolved domain");
    const auto outcome=simulation::estimate_helper_frequency(tx,options,true,channel,*scenario.scenario,{},1,false,
        100,64,simulation::ReceiverWorkMode::hardware_fallback);
    check(outcome.scenario_available,outcome.reason.c_str());
    check(outcome.reason.find("Independent unsteered helper")!=std::string::npos,
        "joint conditional work described a different source-clock relation");
    auto changed_provenance=*scenario.scenario;
    changed_provenance.profiles[0].input.joint_source_relation=changed_source.joint_source_relation;
    check(!simulation::estimate_helper_frequency(tx,options,true,channel,changed_provenance,{},1,false,
        100,64,simulation::ReceiverWorkMode::hardware_fallback).scenario_available,
        "joint work accepted source provenance different from its immutable executed proof");
    check(outcome.assisted.receiver_workspace_supported&&outcome.assisted.receiver_work_supported&&
        outcome.assisted.hardware_sustained_modeled&&outcome.assisted.hardware_sustained_realtime_ratio<
        outcome.baseline.hardware_sustained_realtime_ratio,"joint cost did not price supported actual subset work");
    check(outcome.assisted.initial_epoch_setup_seconds==outcome.baseline.initial_epoch_setup_seconds&&
        outcome.assisted.new_epoch_setup_seconds==outcome.baseline.new_epoch_setup_seconds&&
        outcome.assisted.hardware_sustained_tracking_realtime_ratio==outcome.baseline.hardware_sustained_tracking_realtime_ratio,
        "joint acquisition discounted ordinary full-owner setup or continuation");
    check(outcome.assisted.epoch_hypotheses==outcome.baseline.epoch_hypotheses&&
        outcome.assisted.timing_phase_groups==outcome.baseline.timing_phase_groups,
        "joint work inferred a known source epoch or removed keyed phase alternatives");
    check(!outcome.conditional_probability_available&&!outcome.qualified_cost_available,
        "joint work metadata invented RX or uncalibrated total CPU");
    auto other=options;other.modem.oscillator_search->lf.accuracy_ppm*=2;
    check(!simulation::helper_executed_joint_scenario(tx,other,channel,copied.proof).scenario,
        "old executed joint domain applied to changed oscillator policy");
    std::cout<<"joint cost ordinary_batches="<<ordinary_batches<<" actual="<<copied.metadata.joint_pairs<<'/'<<copied.metadata.frequency_only_pairs
        <<'/'<<copied.metadata.original_pairs<<" clocks="<<copied.metadata.joint_clock_rates
        <<" retained_bytes="<<copied.metadata.owned_bytes<<" backend="
        <<outcome.assisted.hardware_sustained_realtime_ratio<<'/'<<outcome.baseline.hardware_sustained_realtime_ratio<<'\n';
}
}
int main(){try{executed_cost_copy();std::cout<<"helper joint cost component PASS\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
