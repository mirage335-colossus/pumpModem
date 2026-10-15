#include "scheduled_default_guard_cohort.hpp"
// Reuse the maintained same-global-chord oracle fixture without copying or
// weakening its assertions. This new target executes only the added checks.
#define main maintained_default_linear_events_main
#include "test_scheduled_default_linear_events.cpp"
#undef main
namespace cohort=datapump::modem::detail::scheduled_default_guard_cohort;
static void policy(){check(cohort::parse(nullptr)==cohort::Policy::automatic,"unset policy changed");
    check(cohort::parse("auto")==cohort::Policy::automatic&&cohort::parse("1")==cohort::Policy::single&&cohort::parse("8")==cohort::Policy::eight,"explicit cohort parse changed");
    for(const char* text:{"","0","4","16","1junk"}){bool bad=false;try{cohort::parse(text);}catch(const datapump::Error&){bad=true;}check(bad,"invalid cohort policy accepted");}
    for(std::size_t total:{1U,3U,8U,9U,354U})for(std::size_t origin=0;origin<total;++origin){
        const auto old=cohort::choose(origin,total,false,cohort::Policy::automatic);check(old.first==origin/8*8&&old.outputs==std::min<std::size_t>(8,total-old.first)&&old.width==8,"legacy cohort geometry changed");
        const auto solo=cohort::choose(origin,total,true,cohort::Policy::automatic);check(solo.first==origin&&solo.outputs==1&&solo.width==1,"survivor speculative lanes retained");
        const auto forced=cohort::choose(origin,total,true,cohort::Policy::eight);check(forced.first==old.first&&forced.outputs==old.outputs,"forced8 diagnostic changed");
        const auto one=cohort::choose(origin,total,false,cohort::Policy::single);check(one.first==origin&&one.outputs==1,"forced1 diagnostic changed");}
    bool bad=false;try{cohort::choose(0,0,true,cohort::Policy::automatic);}catch(const datapump::Error&){bad=true;}check(bad,"empty range accepted");
    bad=false;try{cohort::choose(9,9,true,cohort::Policy::automatic);}catch(const datapump::Error&){bad=true;}check(bad,"past range accepted");
}
static void same_complete(const e::EventStats& a,const e::EventStats& b,const e::Geometry& g){
    check(a.valid==b.valid&&a.observed_start==b.observed_start&&a.last_owner==b.last_owner&&a.last_section==b.last_section&&a.last_window==b.last_window,"cohort event metadata changed");
    for(unsigned bit=0;bit<2;++bit){fit_close(a.whole[bit],b.whole[bit]);fit_close(a.chips[bit].cell,b.chips[bit].cell);
        close(a.chips[bit].projected_energy,b.chips[bit].projected_energy,"cohort chip energy changed");check(a.chips[bit].projected_rank==b.chips[bit].projected_rank&&a.chips[bit].nested==b.chips[bit].nested,"cohort chip rank/nesting changed");
        close(a.outer.dot[bit].real(),b.outer.dot[bit].real(),"cohort outer real changed");close(a.outer.dot[bit].imag(),b.outer.dot[bit].imag(),"cohort outer imaginary changed");
        close(a.outer.variance[bit],b.outer.variance[bit],"cohort outer variance changed");check(a.outer.contributing_terms[bit]==b.outer.contributing_terms[bit],"cohort outer terms changed");}
    for(const auto threshold:{0.,5.,27.,91.}){const auto x=e::scores(a,g,threshold),y=e::scores(b,g,threshold);
        check(x.valid&&y.valid&&x.bit==y.bit&&x.outer_presence_active==y.outer_presence_active,"cohort guard branch changed");close(x.score[0],y.score[0],"cohort zero score changed");close(x.score[1],y.score[1],"cohort one score changed");close(x.outer_presence_score,y.outer_presence_score,"cohort outer score changed");}
}
static void full_guard_geometry(std::size_t chip,unsigned q){Fixture f(chip,q,true,1,false);
    const auto produce=[&](e::Geometry g){return e::run(g,e::reservation(g),[&](auto k,auto,auto){return f.cell(k);},
        [&](auto a,auto b,auto emit){for(auto n=a;n<b;++n)emit(n,f.pcm[n]);},[&](auto j,auto a,auto b){return f.classify(j,a,b);},
        [&](auto j,auto n,bool need){return f.row(j,n,need);},[&](auto k){return f.coefficients(k);});};
    const auto wide=produce(f.g);check(wide.valid,"wide fixture failed");
    for(std::size_t j=0;j<8;++j){auto g=f.g;g.origin_first+=j;g.outputs=1;g.observed_source_begin=std::span(f.observed).subspan(j,1);
        const auto single=produce(g);check(single.valid&&single.events.size()==1,"single survivor failed");same_complete(single.events[0],wide.events[j],g);
        auto oracle=f.reference(g.origin_first);oracle.last_owner=oracle.chips[0].cell_index;same_complete(single.events[0],oracle,g);check(single.work.peak_bytes<=e::reservation(g),"single cohort undercharged");
        check(single.work.cell_contractions<wide.work.cell_contractions,"single survivor retained speculative contractions");}
}
int main(){try{policy();full_guard_geometry(8,2);full_guard_geometry(34,4);std::cout<<"adaptive guard cohorts preserve complete chip/outer point statistics, identities, bounds and explicit1/8 controls\n";return 0;}
catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
