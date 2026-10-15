// Real private/DSSS C8/C34 clock/fractional fixture with raw512 fences and
// variable ACTUAL processing cuts. Reuse maintained test source unchanged.
#define main maintained_default_linear_original_main
#include "test_scheduled_default_linear_original.cpp"
#undef main
using Access=datapump::modem::PatternCorrelatorTimingTestAccess;
using State=Access::State;
static void actual(bool fast,double ppm,bool fhss=false){
    using namespace datapump::modem;namespace journal=detail::scheduled_processing;
    Access::mode("DATAPUMP_SCHED_LAZY_ACQUISITION","1");Access::mode("DATAPUMP_SCHED_LAZY_PARALLEL","0");
    Access::mode("DATAPUMP_SCHED_FINE_CURVES","0");Access::mode("DATAPUMP_SCHED_LINEAR_GRID","4");
    Access::mode("DATAPUMP_SCHED_FRACTIONAL_BASIS","0");Access::mode("DATAPUMP_SCHED_CLOCK_NATIVE_FFT","0");
    auto c=Access::config(fast,3,fhss,SearchArithmetic::default_mode);
    State s(c,Access::search(c,ppm),256*1024*1024,PatternCorrelatorOptions{.scheduled_common_shape_experiment=false});
    Access::check(s.lazy_state()&&s.shaped,"actual bulk fixture lost implicit shaped backend");
    Access::Range r;r.family=0;const auto& f=s.lazy_state()->catalog->families().front();r.first=f.origin_first+1;r.end=std::min(f.origin_end,r.first+(fast?40:12));
    Access::check(r.end-r.first>=8,"actual bulk fixture lacks original timing coverage");
    const auto lane=s.lazy_family_lane(r.family,r.first);r.index=lane.index;r.id=lane.id;
    std::uint64_t full=0;for(auto i=r.first;i<r.end;++i){const auto v=s.lazy_family_lane(r.family,i);full=std::max(full,s.lazy_state()->catalog->due(v,r.index,*s.lazy_state()->cursor));}
    const auto captured=full+1024;
    Access::original_capture(s,s.lazy_family_lane(r.family,r.end-1),r.index,0,captured,.03,881);
    // Replace only fixture metadata before any decoder query. No live cache
    // or projected binding exists yet; PCM remains the original immutable copy.
    const auto capacity=static_cast<std::size_t>(2*(captured/3+1)+128);
    Access::check(capacity<=65536,"variable-cut fixture exceeds bounded metadata preflight");
    s.lazy_state()->processing=std::make_unique<journal::ProcessingJournal>(journal::Limits{capacity,capacity,capacity,s.budget},0);
    s.lazy_state()->processing->append_push(0,captured);s.lazy_state()->screen_capture_end=captured;
    Access::mode("DATAPUMP_SCHED_DEFAULT_LINEAR","1");Access::mode("DATAPUMP_SCHED_DEFAULT_POINT_FFT","1");
    const auto initial=Access::detector(s);const auto pcm=Access::pcm_identity(s);
    std::size_t cut=0;std::uint64_t bulk_builds=0;
    for(std::size_t j=0;j<r.end-r.first;++j){const auto candidate=s.lazy_family_lane(r.family,r.first+j);
        const auto due=s.lazy_state()->catalog->due(candidate,r.index,*s.lazy_state()->cursor);
        while(s.lazy_state()->processing->processing_fences().end()<due){const auto left=due-s.lazy_state()->processing->processing_fences().end();
            const auto size=std::array<std::size_t,4>{7,137,19,3}[cut++%4];
            const auto block=s.lazy_state()->processing->prepare([&](auto maximum){return std::min<std::size_t>({maximum,size,static_cast<std::size_t>(left)});},
                [&](std::uint64_t p){const auto at=p/512*512;return journal::Fence{at,std::min<std::uint64_t>(512,captured-at),p,std::min<std::uint64_t>(512,captured-at)-(p-at)};});
            Access::check(bool(block),"actual bulk fixture failed to publish a real processing fence");s.lazy_state()->processing->advance(*block);}
        s.lazy_state()->block_end=s.lazy_state()->processing->processing_fences().end();
        Access::mode("DATAPUMP_SCHED_DEFAULT_POINT_BULK","0");
        const auto legacy=s.lazy_default_point_fft<float>(candidate,r,24,{});
        Access::mode("DATAPUMP_SCHED_DEFAULT_POINT_BULK","1");
        const auto split=s.lazy_default_point_fft<float>(candidate,r,24,{});
        Access::check(legacy.ready&&split.ready,"actual whole/finalized bulk unexpectedly unavailable");
        for(unsigned bit=0;bit<2;++bit)Access::check(std::abs(legacy.scores.score[bit]-split.scores.score[bit])<=3e-3*(1+std::abs(legacy.scores.score[bit])),"actual bulk score changed beyond native point tolerance");
        if(j==0){bulk_builds=s.lazy_state()->default_bulk_preparations;Access::check(bulk_builds==1&&s.lazy_state()->default_bulk_outputs>=r.end-r.first,"bulk failed to prepare capture-complete future origins");}
        else Access::check(s.lazy_state()->default_bulk_preparations==bulk_builds,"new ACTUAL due fence rebuilt capture-complete FFT bulk");
        Access::check(!s.lazy_state()->transient_reservation&&!s.lazy_state()->serial_execution->screen_active,"actual bulk leaked loan/worker pin");
        Access::check(Access::detector(s)==initial&&Access::pcm_identity(s)==pcm,"pending bulk manufactured trials, progress or absence");
    }
    Access::check(s.lazy_state()->default_bulk_finalized_outputs==r.end-r.first,"actual due finalization omitted an origin");
    Access::mode("DATAPUMP_SCHED_DEFAULT_POINT_BULK",nullptr);Access::mode("DATAPUMP_SCHED_DEFAULT_POINT_FFT",nullptr);Access::mode("DATAPUMP_SCHED_DEFAULT_LINEAR",nullptr);
}
int main(){try{if(!State::lazy_reference_environment()){std::cout<<"Original environment unavailable; actual bulk oracle omitted\n";return 77;}actual(false,300);actual(true,-300);actual(false,-300,true);std::cout<<"real private/DSSS pending bulk reuses captured future FFTs across actual processing cuts without progress or PCM changes\n";return 0;}
catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
