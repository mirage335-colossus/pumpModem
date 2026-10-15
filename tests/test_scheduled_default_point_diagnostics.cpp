// Real private/DSSS native guard result plus bounded chronological history
// pressure. The maintained independent Original oracle remains unchanged apart
// from an explicit reference control and a friend accessor (separate patch).
#define main maintained_default_linear_original_main
#include "test_scheduled_default_linear_original.cpp"
#undef main
#include "scheduled_default_point_diagnostics.hpp"
using Access=datapump::modem::PatternCorrelatorTimingTestAccess;
using State=Access::State;
namespace diag=datapump::modem::detail::scheduled_default_point_diagnostics;
namespace guard=datapump::modem::detail::scheduled_default_point_guard_summary;
using datapump::modem::PatternEvidence;
static void identical(const PatternEvidence& a,const PatternEvidence& b) {
    Access::check(a.first_sample==b.first_sample&&a.end_sample==b.end_sample&&a.stream_symbol==b.stream_symbol&&
        a.bit==b.bit&&a.stream_phase_samples==b.stream_phase_samples&&a.completed_event==b.completed_event&&
        a.outer_presence_active==b.outer_presence_active&&a.frequency_hypothesis==b.frequency_hypothesis,
        "chosen history identity/winner/outer fields changed");
    for(const auto pair:std::array{std::pair{a.frequency_hz,b.frequency_hz},std::pair{a.score,b.score},
            std::pair{a.alternative_score,b.alternative_score},std::pair{a.admission_threshold,b.admission_threshold},
            std::pair{a.outer_presence_score,b.outer_presence_score}})
        Access::check(std::bit_cast<std::uint64_t>(pair.first)==std::bit_cast<std::uint64_t>(pair.second),
            "chosen history numerical field changed");
}
static void parser() {
    Access::check(diag::policy(nullptr)==diag::Policy::chosen&&diag::policy("chosen")==diag::Policy::chosen&&
        diag::policy("original")==diag::Policy::original,"diagnostic policy default/reference changed");
    bool denied=false;try{(void)diag::policy("automatic");}catch(const datapump::Error&){denied=true;}
    Access::check(denied,"unknown diagnostic policy silently selected a producer");
    Access::check(std::string_view(diag::statistic(diag::Outcome::original_fallback))=="Original deferred fallback"&&
        std::string_view(diag::reason(diag::Outcome::original_fallback))=="native-full-guard-unavailable",
        "fallback durable trace label lost");
    datapump::modem::detail::scheduled_default_linear_events::Scores equality;
    equality.valid=true;equality.score={30.,0.};
    Access::check(!datapump::modem::detail::scheduled_default_linear_events::below_rejection_floor(equality,30.),
        "admission equality entered diagnostic-only rejection branch");
}
static void actual_history() {
    using namespace datapump::modem;
    Access::mode("DATAPUMP_SCHED_LAZY_ACQUISITION","1");Access::mode("DATAPUMP_SCHED_LAZY_PARALLEL","0");
    Access::mode("DATAPUMP_SCHED_FINE_CURVES","0");Access::mode("DATAPUMP_SCHED_LINEAR_GRID","4");
    Access::mode("DATAPUMP_SCHED_FRACTIONAL_BASIS","0");Access::mode("DATAPUMP_SCHED_CLOCK_NATIVE_FFT","0");
    Access::mode("DATAPUMP_SCHED_DEFAULT_LINEAR","1");Access::mode("DATAPUMP_SCHED_DEFAULT_POINT_FFT","1");
    Access::mode("DATAPUMP_SCHED_DEFAULT_POINT_DIAGNOSTICS","chosen");
    auto c=Access::config(true,3,true,SearchArithmetic::default_mode);auto search=Access::search(c,-300);
    search.candidate_limit=2;search.retain_score=5;
    PatternCorrelator rx(c,search,256*1024*1024,PatternCorrelatorOptions{.scheduled_common_shape_experiment=false});
    auto& s=Access::state(rx);Access::check(s.lazy_state()&&s.shaped,"chosen diagnostic fixture lost lazy shaped backend");
    Access::Range range;range.family=0;const auto& f=s.lazy_state()->catalog->families().front();
    range.first=f.origin_first+1;range.end=std::min(f.origin_end,range.first+8);
    Access::check(range.end-range.first>=2,"chosen diagnostic fixture lacks timing alternatives");
    const auto lane=s.lazy_family_lane(range.family,range.first);range.index=lane.index;range.id=lane.id;
    std::uint64_t due=0;for(auto i=range.first;i<range.end;++i)
        due=std::max(due,s.lazy_state()->catalog->due(s.lazy_family_lane(range.family,i),range.index,*s.lazy_state()->cursor));
    Access::original_capture(s,lane,range.index,0,due+512,.1,883);
    s.lazy_state()->screen_capture_end=s.lazy_state()->capture_end;s.lazy_state()->block_end=s.lazy_state()->capture_end;
    const auto end=s.lazy_state()->catalog->due(lane,range.index,*s.lazy_state()->cursor);
    const auto first_threshold=Access::charged_threshold(s,1);
    guard::Summary native;
    const auto full=s.lazy_default_linear_point(lane,range,first_threshold,{},true,&native);
    Access::check(full.ready&&native.valid&&native.observed_start!=detail::scheduled_default_linear_events::no_owner,
        "real chosen fullguard/observed_start sink unavailable");
    const auto pcm=Access::pcm_identity(s);const auto before=rx.provisional();const auto sample=s.sample,processed=s.lazy_state()->processed,trials=s.trials;
    detail::scheduled_default_linear_runtime::Decision rejected;rejected.ready=rejected.scores.valid=true;
    rejected.scores.score={1.,.5};
    const auto commit=[&](std::uint64_t event,const guard::Summary* ready,std::stop_token stop={}) {
        const auto threshold=Access::charged_threshold(s,event);
        const auto outcome=s.lazy_default_point_diagnostic(lane,range,range.index,event,event,
            s.lazy_state()->block_end,threshold,rejected,true,ready,stop);
        s.lazy_state()->history_generation=event;return outcome;
    };
    // Deliberately opposite sides of retention across producers: the chosen
    // policy is the explicit experimental screen hierarchy, not a claim that
    // FP32 FFT fields bound native fullguard fields.
    Access::check(std::max(full.scores.score[0],full.scores.score[1])>=search.retain_score,
        "cross-producer retention boundary fixture inactive");
    Access::check(commit(1,&native)==diag::Outcome::chosen_fft_excluded&&s.history.empty()&&
        !s.lazy_state()->diagnostics->oldest()&&s.lazy_state()->diagnostics->resolved_through()==1,
        "below-retain chosen screen was not eagerly resolved");
    rejected.scores.score={search.retain_score,search.retain_score-1};
    Access::check(commit(2,&native)==diag::Outcome::chosen_native_guards,"retention equality skipped native chosen guards");
    rejected.scores.score={10.,9.};
    const auto expected=diag::evidence(native,{end,range.index,2,lane.phase_lower,
        c.carrier_hz+s.search.frequency_offsets_hz[lane.frequency],Access::charged_threshold(s,2)});
    Access::check(expected&&s.history.size()==1,"chosen retainer missing");identical(s.history.back(),*expected);
    // A cache miss must obtain the same native fullguard immediately. It must
    // never replace the detector's already-decided synthetic rejection.
    s.lazy_state()->default_linear_cache.reset();const auto lookups=s.lazy_state()->default_chosen_diagnostic_guard_lookups;
    Access::check(commit(3,nullptr)==diag::Outcome::chosen_native_guards&&
        s.lazy_state()->default_chosen_diagnostic_guard_lookups==lookups+1,"native guard miss was not calculated");
    const auto expected3=diag::evidence(native,{end,range.index,3,lane.phase_lower,
        c.carrier_hz+s.search.frequency_offsets_hz[lane.frequency],Access::charged_threshold(s,3)});
    Access::check(bool(expected3),"native summary invalid after threshold change");identical(s.history.back(),*expected3);
    // Reuse one genuinely completed numerical result to stress ONLY bounded
    // metadata/FIFO behavior; this is not a whole-reception/performance claim.
    const auto equality=diag::evidence(native,{end,range.index,4,lane.phase_lower,
        c.carrier_hz+s.search.frequency_offsets_hz[lane.frequency],Access::charged_threshold(s,4)});
    Access::check(bool(equality),"native retention equality fixture unavailable");
    s.search.retain_score=equality->score;rejected.scores.score={equality->score,equality->score};
    Access::check(commit(4,&native)==diag::Outcome::chosen_native_guards&&s.history.back().completed_event==4,
        "native score equal to retention floor was discarded");
    s.search.retain_score=search.retain_score;rejected.scores.score={10.,9.};
    const auto history_capacity=s.history.capacity(),journal_bytes=s.lazy_state()->diagnostics->working_bytes();
    for(std::uint64_t event=5;event<=1000;++event)
        Access::check(commit(event,&native)==diag::Outcome::chosen_native_guards,"long chosen history unexpectedly deferred");
    Access::check(s.history.size()==2&&s.history[0].completed_event==999&&s.history[1].completed_event==1000&&
        !s.lazy_state()->diagnostics->oldest()&&s.lazy_state()->diagnostic_materializations==0,
        "chosen history pressure grew replay payload or changed last-C FIFO");
    Access::check(s.history.capacity()==history_capacity&&s.lazy_state()->diagnostics->working_bytes()==journal_bytes,
        "metadata-only long chosen history grew receiver allocations");
    const auto all=rx.candidates(),last=rx.candidates(1);Access::check(all.size()==2&&last.size()==1&&
        last.back().completed_event==all.back().completed_event&&rx.candidates(0).empty(),"chosen query limit changed FIFO");
    std::stop_source stop;stop.request_stop();const auto generation=rx.diagnostic_history_generation();bool cancelled=false;
    try{(void)rx.candidates(1,stop.get_token());}catch(const datapump::Error&){cancelled=true;}
    Access::check(cancelled&&rx.diagnostic_history_generation()==generation&&s.history.size()==2,
        "cancelled chosen query changed history identity");
    cancelled=false;try{(void)commit(1001,&native,stop.get_token());}catch(const datapump::Error&){cancelled=true;}
    Access::check(cancelled&&rx.diagnostic_history_generation()==generation&&s.history.back().completed_event==1000,
        "cancelled chosen publication committed a partial event");
    // Force a genuine optional-workspace failure on the uncached native path.
    // Restore the budget before independently materializing Original fallback.
    s.lazy_state()->default_linear_cache.reset();s.lazy_state()->default_fft_pool.clear();
    Access::check(s.lazy_state()->default_point_guard_archive.clear(),"archive unexpectedly borrowed");
    const auto budget=s.budget;s.budget=0;
    const auto fallback=commit(1001,nullptr);s.budget=budget;
    Access::check(fallback==diag::Outcome::original_fallback&&s.lazy_state()->default_chosen_diagnostic_original_fallbacks==1,
        "resource failure did not record explicit Original fallback");
    const auto pending=s.lazy_state()->diagnostics->oldest();
    Access::check(pending&&pending->ordinal==1001&&pending->provenance==detail::scheduled_diagnostics::RejectionProvenance::default_point,
        "Original fallback lost its saved trial/rejection identity");
    const auto reference=s.lazy_diagnostic_evidence(lane.id,range.index,1001,1001,{},
        detail::scheduled_diagnostics::RejectionProvenance::default_point);
    const auto fallback_last=rx.candidates(1);Access::check(fallback_last.size()==1,"Original fallback retainer missing");
    identical(fallback_last[0],reference);Access::check(s.lazy_state()->diagnostic_materializations==1,"fallback did not materialize exactly once");
    Access::mode("DATAPUMP_SCHED_DEFAULT_POINT_DIAGNOSTICS","original");
    Access::check(commit(1002,&native)==diag::Outcome::original_requested,"explicit Original reference reused chosen summary");
    Access::mode("DATAPUMP_SCHED_DEFAULT_POINT_DIAGNOSTICS","chosen");
    Access::check(commit(1003,&native)==diag::Outcome::chosen_native_guards&&commit(1004,&native)==diag::Outcome::chosen_native_guards,
        "mixed eager history failed");
    const auto before_query=s.lazy_state()->diagnostic_materializations;
    const auto mixed=rx.candidates();Access::check(mixed.size()==2&&mixed[0].completed_event==1003&&mixed[1].completed_event==1004&&
        s.lazy_state()->diagnostic_materializations==before_query&&!s.lazy_state()->diagnostics->oldest(),
        "older Original event displaced two confirmed newer chosen retainers");
    const auto after=rx.provisional();Access::check(s.trials==trials&&s.sample==sample&&s.lazy_state()->processed==processed&&
        before.bits==after.bits&&before.end_sample==after.end_sample&&before.complete==after.complete&&rx.take_bursts().empty()&&
        Access::pcm_identity(s)==pcm,"diagnostic policy changed receiver progress, completion or PCM");
    Access::check(!s.lazy_state()->transient_reservation,"chosen diagnostic left a workspace loan");
    for(const auto* key:{"DATAPUMP_SCHED_DEFAULT_POINT_DIAGNOSTICS","DATAPUMP_SCHED_DEFAULT_POINT_FFT","DATAPUMP_SCHED_DEFAULT_LINEAR",
        "DATAPUMP_SCHED_LAZY_ACQUISITION","DATAPUMP_SCHED_LAZY_PARALLEL","DATAPUMP_SCHED_FINE_CURVES","DATAPUMP_SCHED_LINEAR_GRID",
        "DATAPUMP_SCHED_FRACTIONAL_BASIS","DATAPUMP_SCHED_CLOCK_NATIVE_FFT"})Access::mode(key,nullptr);
}
struct LoopResult {
    std::uint64_t trials=0,generation=0,rejected=0,chosen=0,pcm=0;
    std::vector<PatternEvidence> history;
};
static LoopResult actual_dispatch(bool interrupt) {
    using namespace datapump::modem;namespace journal=detail::scheduled_processing;namespace resume=detail::scheduled_resume;
    for(const auto* key:{"DATAPUMP_SCHED_LAZY_ACQUISITION","DATAPUMP_SCHED_DEFAULT_LINEAR",
        "DATAPUMP_SCHED_DEFAULT_LINEAR_PRIMARY","DATAPUMP_SCHED_DEFAULT_POINT_FFT"})Access::mode(key,"1");
    Access::mode("DATAPUMP_SCHED_LAZY_PARALLEL","0");Access::mode("DATAPUMP_SCHED_FINE_CURVES","0");
    Access::mode("DATAPUMP_SCHED_LINEAR_GRID","4");Access::mode("DATAPUMP_SCHED_FRACTIONAL_BASIS","0");
    Access::mode("DATAPUMP_SCHED_CLOCK_NATIVE_FFT","0");Access::mode("DATAPUMP_SCHED_DEFAULT_POINT_DIAGNOSTICS","chosen");
    auto c=Access::config(true,3,false,SearchArithmetic::default_mode);auto search=Access::search(c,-300);
    search.start_uncertainty_seconds=8./c.sample_rate;search.candidate_limit=4;search.retain_score=5;
    PatternCorrelator rx(c,search,256*1024*1024,PatternCorrelatorOptions{.scheduled_common_shape_experiment=false});
    auto& state=Access::state(rx);Access::check(state.lazy_state()!=nullptr,"actual diagnostic dispatch lost lazy backend");
    const auto& family=state.lazy_state()->catalog->families().front();
    const auto lane=state.lazy_family_lane(0,family.origin_first);
    const auto end=static_cast<std::uint64_t>(std::ceil(state.lazy_state()->initial_complete_due))+1024;
    Access::check(end&&end<500000,"actual diagnostic dispatch fixture exceeded bounded PCM scope");
    Access::original_capture(state,lane,lane.index,0,end,0.,887);
    const auto pcm=Access::pcm_identity(state);
    // Captured immutable raw512 blocks are real; publish original processing
    // fences through the actual flush loop rather than pre-publishing them.
    state.lazy_state()->processing=std::make_unique<journal::ProcessingJournal>(journal::Limits{2048,2048,2048,state.budget},0);
    state.lazy_state()->processing->append_push(0,end);state.lazy_state()->processed=0;
    state.lazy_state()->screen_capture_end=0;state.lazy_state()->block_end=0;
    state.lazy_state()->pending_since=std::chrono::steady_clock::now()-std::chrono::seconds(2);
    struct CancelAtCommit {std::stop_source stop;bool armed=false,fired=false;} control;
    control.armed=interrupt;
    const auto observe=[](resume::Boundary boundary,std::uint64_t,void* opaque)noexcept {
        auto& c=*static_cast<CancelAtCommit*>(opaque);
        if(c.armed&&!c.fired&&boundary==resume::Boundary::event_committed){c.fired=true;c.stop.request_stop();}
    };
    bool cancelled=false;
    {resume::Scope scope({observe,&control});try{rx.process_pending(control.stop.get_token());}
        catch(const datapump::Error&){if(!control.stop.stop_requested())throw;cancelled=true;}}
    if(interrupt) {
        Access::check(cancelled&&control.fired&&state.trials==1&&state.lazy_state()->history_generation==1,
            "actual rejection cancellation lost or duplicated first trial/event");
        rx.process_pending();
    }
    Access::check(state.lazy_state()->processed==state.lazy_state()->capture_end&&state.lazy_state()->default_linear_rejections>0&&
        state.lazy_state()->default_chosen_diagnostic_resolved>0,"real rejection loop did not consume chosen policy");
    const auto pending=rx.provisional();Access::check(pending.bits.empty()&&!pending.complete&&rx.take_bursts().empty(),
        "noise diagnostic dispatch manufactured bits or physical completion");
    LoopResult out{state.trials,state.lazy_state()->history_generation,state.lazy_state()->default_linear_rejections,
        state.lazy_state()->default_chosen_diagnostic_resolved,pcm,rx.candidates()};
    Access::check(pcm==Access::pcm_identity(state)&&!state.lazy_state()->transient_reservation,
        "real rejection diagnostic policy changed PCM or leaked a loan");
    for(const auto* key:{"DATAPUMP_SCHED_LAZY_ACQUISITION","DATAPUMP_SCHED_DEFAULT_LINEAR",
        "DATAPUMP_SCHED_DEFAULT_LINEAR_PRIMARY","DATAPUMP_SCHED_DEFAULT_POINT_FFT","DATAPUMP_SCHED_LAZY_PARALLEL",
        "DATAPUMP_SCHED_FINE_CURVES","DATAPUMP_SCHED_LINEAR_GRID","DATAPUMP_SCHED_FRACTIONAL_BASIS",
        "DATAPUMP_SCHED_CLOCK_NATIVE_FFT","DATAPUMP_SCHED_DEFAULT_POINT_DIAGNOSTICS"})Access::mode(key,nullptr);
    return out;
}
static void actual_dispatch_retry() {
    const auto original=actual_dispatch(false),resumed=actual_dispatch(true);
    Access::check(original.pcm==resumed.pcm&&original.trials==resumed.trials&&original.generation==resumed.generation&&
        original.rejected==resumed.rejected&&original.chosen==resumed.chosen&&original.history.size()==resumed.history.size(),
        "actual rejection retry changed PCM/trial/event/FIFO chronology");
    for(std::size_t i=0;i<original.history.size();++i)identical(original.history[i],resumed.history[i]);
}
int main(){try{if(!State::lazy_reference_environment())return 77;parser();actual_history();actual_dispatch_retry();
    std::cout<<"screen-gated chosen diagnostics, native retainers, explicit Original fallback/reference, FIFO/limits/cancellation/pressure checked\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
