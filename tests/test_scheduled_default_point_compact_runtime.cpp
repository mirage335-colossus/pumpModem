// Actual production caller, original private PCM and real processing fences.
// This additive target preserves every maintained Original/family oracle target.
#define main maintained_default_linear_original_main
#include "test_scheduled_default_linear_original.cpp"
#undef main
using Access=datapump::modem::PatternCorrelatorTimingTestAccess;
using State=Access::State;
static void compact_actual(bool fast,double ppm,bool fhss=false) {
    using namespace datapump::modem;namespace journal=detail::scheduled_processing;
    Access::mode("DATAPUMP_SCHED_LAZY_ACQUISITION","1");Access::mode("DATAPUMP_SCHED_LAZY_PARALLEL","0");
    Access::mode("DATAPUMP_SCHED_FINE_CURVES","0");Access::mode("DATAPUMP_SCHED_LINEAR_GRID","4");
    Access::mode("DATAPUMP_SCHED_FRACTIONAL_BASIS","0");Access::mode("DATAPUMP_SCHED_CLOCK_NATIVE_FFT","0");
    Access::mode("DATAPUMP_SCHED_DEFAULT_LINEAR","1");Access::mode("DATAPUMP_SCHED_DEFAULT_POINT_FFT","1");
    Access::mode("DATAPUMP_SCHED_DEFAULT_POINT_BULK","1");
    auto c=Access::config(fast,3,fhss,SearchArithmetic::default_mode);
    State s(c,Access::search(c,ppm),256*1024*1024,PatternCorrelatorOptions{.scheduled_common_shape_experiment=false});
    Access::check(s.lazy_state()&&s.shaped,"actual compact fixture lost implicit shaped backend");
    Access::Range r;r.family=0;const auto& f=s.lazy_state()->catalog->families().front();r.first=f.origin_first+1;r.end=std::min(f.origin_end,r.first+8);
    Access::check(r.end-r.first==8,"actual compact fixture lacks timing coverage");
    const auto reference=s.lazy_family_lane(r.family,r.first);r.index=reference.index;r.id=reference.id;
    std::uint64_t full=0;for(auto i=r.first;i<r.end;++i){const auto v=s.lazy_family_lane(r.family,i);full=std::max(full,s.lazy_state()->catalog->due(v,r.index,*s.lazy_state()->cursor));}
    const auto captured=full+1024;Access::original_capture(s,s.lazy_family_lane(r.family,r.end-1),r.index,0,captured,.03,881);
    const auto capacity=static_cast<std::size_t>(2*(captured/3+1)+128);
    Access::check(capacity<=65536,"compact processing metadata exceeds fixture bound");
    s.lazy_state()->processing=std::make_unique<journal::ProcessingJournal>(journal::Limits{capacity,capacity,capacity,s.budget},0);
    s.lazy_state()->processing->append_push(0,captured);s.lazy_state()->screen_capture_end=captured;
    const auto initial=Access::detector(s);const auto pcm=Access::pcm_identity(s);std::size_t cut=0;std::uint64_t builds=0;
    for(std::size_t j=0;j<r.end-r.first;++j) {
        const auto candidate=s.lazy_family_lane(r.family,r.first+j);const auto due=s.lazy_state()->catalog->due(candidate,r.index,*s.lazy_state()->cursor);
        while(s.lazy_state()->processing->processing_fences().end()<due){const auto left=due-s.lazy_state()->processing->processing_fences().end();
            const auto size=std::array<std::size_t,4>{7,137,19,3}[cut++%4];
            const auto block=s.lazy_state()->processing->prepare([&](auto maximum){return std::min<std::size_t>({maximum,size,static_cast<std::size_t>(left)});},
                [&](std::uint64_t p){const auto at=p/512*512;return journal::Fence{at,std::min<std::uint64_t>(512,captured-at),p,std::min<std::uint64_t>(512,captured-at)-(p-at)};});
            Access::check(bool(block),"actual compact fixture failed to publish real processing fence");s.lazy_state()->processing->advance(*block);}
        s.lazy_state()->block_end=s.lazy_state()->processing->processing_fences().end();
        Access::mode("DATAPUMP_SCHED_DEFAULT_POINT_COMPACT","0");const auto dense=s.lazy_default_point_fft<float>(candidate,r,24,{});
        Access::mode("DATAPUMP_SCHED_DEFAULT_POINT_COMPACT","1");
        if(j==1) {
            // Simulate a joined external numeric borrow: mandatory shortage must
            // fail without room_for evicting the paired dense comparison Entry.
            const auto budget=s.budget;s.budget=1;auto& execution=*s.lazy_state()->serial_execution;
            Access::check(!execution.screen_active,"compact fixture entered pressure with an existing worker pin");execution.screen_active=true;
            const auto pressure=s.lazy_default_point_fft<float>(candidate,r,24,{});s.budget=budget;
            Access::check(!pressure.ready&&!s.lazy_state()->transient_reservation&&execution.screen_active,
                "compact pressure fallback leaked a loan, cleared external pin or returned ready");execution.screen_active=false;
        }
        const auto compact=s.lazy_default_point_fft<float>(candidate,r,24,{});
        Access::check(dense.ready&&compact.ready,"actual compact runtime failed mandatory geometry/recipe completion");
        for(unsigned bit=0;bit<2;++bit)Access::check(std::abs(dense.scores.score[bit]-compact.scores.score[bit])<=1e-12*(1+std::abs(dense.scores.score[bit])),
            "compact regenerated runtime changed dense chosen-operator score");
        unsigned dense_entries=0,compact_entries=0;
        for(const auto& item:s.lazy_state()->default_fft_pool.entries)if(item&&item->key.pending_bulk&&item->key.family==r.family&&item->key.first==r.first&&item->key.index==r.index) {
            Access::check(item->pending&&item->core_payload_bytes()<=item->reserved,"actual compact Entry core capacities undercharged");
            const auto summary=item->guard_summaries?item->guard_summaries->working_bytes():0;
            Access::check(item->working_bytes()==item->reserved+summary,"compact double-counted or omitted separately charged guard Store");
            if(item->key.compact_bulk) {++compact_entries;Access::check(item->reference_recipe&&item->pending->directory&&item->pending->directory->sealed()&&item->pending->chords.empty(),"actual compact Entry retained dense payloads or lost owned recipe");
                const auto& identity=item->reference_recipe->identity();Access::check(identity.owner==reinterpret_cast<std::uintptr_t>(s.lazy_state())&&identity.epoch_sample==s.scheduled->epoch_sample&&identity.phase==reference.phase_lower&&identity.index==r.index,"actual compact frozen source identity changed");}
            else {++dense_entries;Access::check(!item->reference_recipe&&!item->pending->directory&&!item->pending->chords.empty(),"dense reference runtime silently changed representation");}
        }
        Access::check(dense_entries==1&&compact_entries==1,"representation cache identity aliased or evicted paired entries");
        if(j==0){builds=s.lazy_state()->default_bulk_preparations;Access::check(builds==2,"dense/compact actual callers failed separate captured preparation");}
        else Access::check(s.lazy_state()->default_bulk_preparations==builds,"later actual processing fence rebuilt frozen compact bulk");
        Access::check(!s.lazy_state()->transient_reservation&&!s.lazy_state()->serial_execution->screen_active,"compact actual caller leaked loan/worker pin");
        Access::check(Access::detector(s)==initial&&Access::pcm_identity(s)==pcm,"compact pending work manufactured trials, progress, PCM or absence");
    }
    Access::check(s.lazy_state()->default_bulk_finalized_outputs==2*(r.end-r.first),"paired compact actual due finalization omitted an origin");
    std::stop_source stop;stop.request_stop();bool cancelled=false;
    try{s.lazy_default_point_fft<float>(reference,r,24,stop.get_token());}catch(const datapump::Error&){cancelled=true;}
    Access::check(cancelled&&!s.lazy_state()->transient_reservation&&!s.lazy_state()->serial_execution->screen_active,"cancelled actual compact caller leaked a loan/pin");
    Access::check(Access::detector(s)==initial&&Access::pcm_identity(s)==pcm,"cancelled compact caller changed authoritative receiver state");
    Access::mode("DATAPUMP_SCHED_DEFAULT_POINT_COMPACT",nullptr);Access::mode("DATAPUMP_SCHED_DEFAULT_POINT_BULK",nullptr);
    Access::mode("DATAPUMP_SCHED_DEFAULT_POINT_FFT",nullptr);Access::mode("DATAPUMP_SCHED_DEFAULT_LINEAR",nullptr);
}
int main(){try{if(!State::lazy_reference_environment())return 77;
    Access::mode("DATAPUMP_SCHED_DEFAULT_POINT_COMPACT",nullptr);Access::check(!State::lazy_default_point_compact_requested(),"compact unexpectedly replaced dense default");
    Access::mode("DATAPUMP_SCHED_DEFAULT_POINT_COMPACT","2");bool rejected=false;
    try{State::lazy_default_point_compact_requested();}catch(const datapump::Error&){rejected=true;}
    Access::check(rejected,"compact mode accepted an unsupported flag");Access::mode("DATAPUMP_SCHED_DEFAULT_POINT_COMPACT",nullptr);
    compact_actual(false,300);compact_actual(true,-300);compact_actual(false,-300,true);
    std::cout<<"actual dense/compact PendingBulk callers preserve scores, owned source/Pieces, capacity/store loans, actual fences, cancel and detector state\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
