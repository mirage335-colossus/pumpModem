#define main accepted_joint_clock_full_main
#include "test_helper_joint_clock.cpp"
#undef main

namespace {
void query_geometry() {
    const auto c=config();auto s=search(c,0);
    modem::PatternReceiver receiver(c,64*1024*1024,s);
    const auto actual=mapping(5275);const auto raw=frequency(actual,5275*100e-6,.005,99999);
    h::LinkState link;check(link.reset(frequency_policy()),"query frequency policy declined");
    const auto observed=link.observe({raw,actual,{}},100000);
    const auto legacy=safe_legacy_mask(c,s,observed,actual);
    check(receiver.set_acquisition_prior(0,legacy),"query original frequency mask declined");
    check(!receiver.acquisition_scored_support(),"unready query invented a scored support");
    while(!receiver.acquisition_state().ready) {
        const auto state=receiver.acquisition_state();check(state.supported && state.input_room_samples,"query split seam unsupported/stalled");
        const auto count=std::min<std::size_t>(113,state.input_room_samples);
        check(receiver.observe_acquisition(std::vector<float>(count))==count,"query observation stalled");
    }
    const auto state=receiver.acquisition_state();const auto bytes=receiver.working_bytes(),reserved=receiver.reserved_workspace_bytes();
    const auto work=receiver.fft_work();const auto scored=receiver.acquisition_scored_support();
    check(scored.has_value(),"ready native B query omitted complete support");
    check(scored->first_sample<=scored->last_sample && scored->consumed_end_sample==state.observed_samples &&
          scored->last_sample>=state.observed_samples,"full refinement cover was clipped to consumed PCM");
    check(scored->domain.original_pairs.data()==receiver.acquisition_hypotheses().data() &&
          scored->domain.original_pairs.size()==s.hypotheses.size() &&
          scored->domain.sample_rate==c.sample_rate && scored->domain.symbol_samples==modem::symbol_sample_count(c) &&
          scored->domain.frequency_origin_hz==0 && scored->domain.input_phase_view_id==0 &&
          scored->domain.legacy_mask_id==modem::detail::clock_prior_detail::mask_id(scored->domain.legacy_frequency_mask),
        "query changed original catalogue/domain coordinates");
    check(bytes==receiver.working_bytes() && reserved==receiver.reserved_workspace_bytes() &&
          work.template_jobs==receiver.fft_work().template_jobs && work.threshold_trials==receiver.fft_work().threshold_trials &&
          receiver.cold_acquisition_only(),"read-only support query allocated/scored/created payload state");
    const auto bound=receiver.acquisition_work_bound();check(bound && bound->ready,"new clock query lost original work-bound API");
    s.input_sample_origin=UINT64_MAX-100;
    modem::PatternReceiver overflow(c,64*1024*1024,s);check(overflow.set_acquisition_prior(0,legacy),"overflow query setup declined");
    while(!overflow.acquisition_state().ready) {
        const auto current=overflow.acquisition_state();const auto count=std::min<std::size_t>(113,current.input_room_samples);
        check(count && overflow.observe_acquisition(std::vector<float>(count))==count,"overflow query observation stalled");
    }
    check(!overflow.acquisition_scored_support(),"query wrapped absolute input origin plus full support");
    std::cout<<"native query support=["<<scored->first_sample<<','<<scored->last_sample<<"] consumed_end="
        <<scored->consumed_end_sample<<" native_working_bytes="<<bytes<<" native_reserved_bytes="<<reserved
        <<" one_batch_descriptor_bound="<<bound->maximum_fft_pair_jobs<<'\n';
}
void resources() {
    using D=h::joint_clock_detail::Interval;
    constexpr auto major_fixed_build_objects=sizeof(std::array<double,modem::maximum_pattern_rate_hypotheses>)+
        sizeof(std::array<D,modem::maximum_pattern_rate_hypotheses>)+
        sizeof(std::array<std::uint16_t,modem::maximum_pattern_frequency_rate_hypotheses>)+
        sizeof(std::array<double,2*modem::maximum_pattern_rate_hypotheses+2>)+sizeof(h::LinkState)+sizeof(h::LinkResult);
    std::cout<<"resource_bytes owner="<<sizeof(h::JointClockCellCertificate)<<" request="<<sizeof(h::JointClockCellRequest)
        <<" prior="<<sizeof(modem::PatternAcquisitionClockPrior)<<" core_context="<<sizeof(modem::detail::PatternFftClockPrior)
        <<" core_result="<<sizeof(modem::PatternAcquisitionClockResult)<<" support="<<sizeof(modem::PatternAcquisitionScoredSupport)
        <<" link_state="<<sizeof(h::LinkState)<<" link_result="<<sizeof(h::LinkResult)
        <<" major_fixed_build_objects="<<major_fixed_build_objects
        <<" punctuation_result_borrowed="<<sizeof(h::PunctuationClockResult)<<'\n';
}
}
int main(){try {
    resources();cells_and_provenance();query_geometry();
    std::cout<<"joint clock pure geometry/query passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
