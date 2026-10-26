// Keep the existing actual-helper/UTC/full-payload fixture source unchanged.
// Its functions provide the SAME ordinary configuration and proof-producing
// joint builder; this focused mode adds selector/cache arithmetic checks.
#define main helper_joint_original_main
#include "test_helper_joint_clock.cpp"
#undef main

int main(){try {
    const auto c=config();const auto measured=actual_helper(c);auto s=search(c,0);
    s.worker_threads=2;
    auto control_search=s;control_search.reuse_deferred_templates=false;
    constexpr std::size_t budget=64*1024*1024;
    modem::PatternReceiver retained(c,budget,s),control(c,budget,control_search);
    const auto a=safe_legacy_mask(c,s,measured.frequency,measured.actual);auto b=a;
    const auto missing=std::find(a.begin(),a.end(),0);check(missing!=a.end(),"cache fixture has no uncached original RF cell");
    const auto added=static_cast<std::size_t>(missing-a.begin());b[added]=1;
    check(retained.set_acquisition_prior(0,a)&&control.set_acquisition_prior(0,a),"initial selector declined");
    std::mt19937 random(1729);std::normal_distribution<float> noise(0,.02F);
    std::vector<float> pcm(8*c.sample_rate);for(auto& sample:pcm)sample=noise(random);
    std::size_t cursor=0,batch=0;std::uint64_t clock_fft=UINT64_MAX,uncached_fft=0;
    std::size_t resident_cache_bytes=0;
    const auto same=[&] {
        const auto x=retained.candidates(),y=control.candidates();check(x.size()==y.size(),"cache changed diagnostic evidence count");
        for(std::size_t i=0;i<x.size();++i)check(x[i].first_sample==y[i].first_sample && x[i].end_sample==y[i].end_sample &&
            x[i].stream_symbol==y[i].stream_symbol && x[i].stream_phase_samples==y[i].stream_phase_samples &&
            x[i].frequency_hypothesis==y[i].frequency_hypothesis && x[i].frequency_hz==y[i].frequency_hz &&
            x[i].bit==y[i].bit && x[i].score==y[i].score && x[i].alternative_score==y[i].alternative_score &&
            x[i].admission_threshold==y[i].admission_threshold,"cache changed exact original numerical evidence/threshold");
        const auto bx=retained.take_bursts(),by=control.take_bursts();check(bx.size()==by.size(),"cache changed next-poll event count");
        for(std::size_t i=0;i<bx.size();++i)check(bx[i].bits==by[i].bits && bx[i].complete==by[i].complete &&
            bx[i].missing_slots==by[i].missing_slots && bx[i].stream_first_sample==by[i].stream_first_sample &&
            bx[i].stream_first_symbol==by[i].stream_first_symbol && bx[i].stream_phase_samples==by[i].stream_phase_samples &&
            bx[i].end_sample==by[i].end_sample,"cache changed wire prefix/identity/physical completion");
        check(retained.fft_work().threshold_trials==control.fft_work().threshold_trials &&
            retained.fft_work().template_jobs==control.fft_work().template_jobs,
            "selector cache altered original trials or descriptor coverage");
    };
    while(batch<5) {
        const auto state=retained.acquisition_state(),other=control.acquisition_state();
        check(state.ready==other.ready && state.observed_samples==other.observed_samples,"cache changed native observation fence");
        if(!state.ready) {
            const auto count=std::min({std::size_t{113},state.input_room_samples,other.input_room_samples,pcm.size()-cursor});
            check(count,"selector fixture exhausted PCM before five original batches");
            const auto block=std::span(pcm).subspan(cursor,count);
            check(retained.observe_acquisition(block)==count && control.observe_acquisition(block)==count,"selector observation stalled");
            cursor+=count;same();continue;
        }
        if(batch==1) {
            const auto old=retained.working_bytes();
            check(retained.set_acquisition_prior(0,b)&&control.set_acquisition_prior(0,b),"expanded selector declined");
            check(retained.working_bytes()==old,"pure RF selector discarded or grew the paid exact cache");
        } else if(batch==2) {
            const auto old=retained.working_bytes();
            check(retained.clear_acquisition_prior()&&control.clear_acquisition_prior(),"raw audit clear failed");
            check(retained.working_bytes()+b.size()>=old,"raw selector clear discarded the paid exact basis");
        } else if(batch==3) {
            check(retained.set_acquisition_prior(0,a)&&control.set_acquisition_prior(0,a),"clock prerequisite restore failed");
        } else if(batch==4) {
            const auto old=retained.working_bytes();const auto allowance=control.reserved_workspace_bytes()+65536;
            retained.set_workspace_bytes(allowance);control.set_workspace_bytes(allowance);
            check(retained.working_bytes()<old && retained.working_bytes()<=allowance,"mandatory worker/bit pressure did not evict optional cache");
            check(retained.set_acquisition_prior(0,b)&&control.set_acquisition_prior(0,b),"pressure selector restore failed");
        }
        const auto before=retained.fft_work();
        if(batch==3) {
            const auto run=[&](modem::PatternReceiver& receiver) {
                const auto support=receiver.acquisition_scored_support();check(bool(support),"clock cache lost full scored support");
                constexpr auto raw_captured=6ULL*14400;
                const auto clock=qualify(measured.clock,support->first_sample,support->last_sample,raw_captured);
                auto q=request(*support,pcm.size());q.raw_captured_end_sample=raw_captured;
                h::JointClockCellCertificate certificate;
                check(certificate.build(c,q,clock,{measured.last,measured.actual,{}},frequency_policy(),raw_domain()),
                    "actual helper proof did not qualify clock-selector cache exercise");
                modem::PatternAcquisitionClockResult result;receiver.process_prior_acquisition(*certificate.prior(),result);
                check(result.completed && result.narrowed,"qualified clock selector not executed");receiver.resolve_acquisition(true);
            };
            run(retained);run(control);clock_fft=retained.fft_work().fft_jobs-before.fft_jobs;
        } else if(batch==2) {retained.process_acquisition();control.process_acquisition();}
        else {
            retained.process_prior_acquisition();control.process_prior_acquisition();
            retained.resolve_acquisition(true);control.resolve_acquisition(true);
        }
        if(batch==0) {
            resident_cache_bytes=retained.working_bytes()-control.working_bytes();
            check(resident_cache_bytes>1024*1024,"focused selector fixture did not fund an actual helper FFT cache");
        }
        if(batch==1)uncached_fft=retained.fft_work().fft_jobs-before.fft_jobs;
        same();++batch;
    }
    // fft_jobs counts correlation jobs even for a fully cached template; it
    // must remain nonzero. The strict evidence/job/trial comparisons above
    // preserve coverage; actual transform counts below verify expensive work.
    check(clock_fft>0 && clock_fft!=UINT64_MAX && uncached_fft>0,
        "clock/new-original-cell selectors executed no ordinary numerical work");
    check(retained.fft_work().fft_jobs<control.fft_work().fft_jobs &&
        retained.fft_work().tabulated_fp64_transforms<control.fft_work().tabulated_fp64_transforms,
        "selector cache removed no actual expensive transforms");
    std::cout<<"selector cache exact evidence: FFTpairjobs="<<retained.fft_work().fft_jobs<<" vs "<<control.fft_work().fft_jobs
        <<" clock_executed_FFTpairjobs="<<clock_fft<<" newcell_batch_executed_FFTpairjobs="<<uncached_fft<<" retainedbytes="<<resident_cache_bytes<<'\n';
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
