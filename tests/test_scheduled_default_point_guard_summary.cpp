// Reuse the maintained event-engine fixture, retaining all of its source and
// assertions. Its separate existing target still runs its unchanged main.
#define main maintained_linear_events_main
#include "test_scheduled_default_linear_events.cpp"
#undef main
#include "scheduled_default_point_guard_summary.hpp"
#include "scheduled_default_point_fft_runtime.hpp"
#include <bit>
#include <limits>
namespace summary=d::scheduled_default_point_guard_summary;
namespace whole_runtime=d::scheduled_default_point_fft_runtime;

void exact_double(double a,double b,const char* message) {
    check(std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b),message);
}
void exact_scores(const e::Scores& a,const e::Scores& b) {
    check(a.valid==b.valid&&a.bit==b.bit&&a.outer_presence_active==b.outer_presence_active,
        "summary valid/winner/outer predicate changed");
    exact_double(a.score[0],b.score[0],"summary zero score changed");
    exact_double(a.score[1],b.score[1],"summary one score changed");
    exact_double(a.outer_presence_score,b.outer_presence_score,"summary outer score changed");
}
void compare_thresholds(const e::EventStats& event,const e::Geometry& g) {
    const auto value=summary::summarize(event,g);
    const std::array thresholds{0.,std::nextafter(0.,1.),1e-12,.125,1.,5.,27.,30.,91.,1000.,1e6,
        std::numeric_limits<double>::max(),-1.,std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN()};
    for(const auto threshold:thresholds)exact_scores(e::scores(event,g,threshold),summary::scores(value,threshold));
    if(g.outer)for(unsigned bit=0;bit<2;++bit) {
        const auto boundary=static_cast<double>(static_cast<long double>(event.outer.contributing_terms[bit])-
            d::CorrelationOuterEvidence::log_constant);
        for(double threshold:{std::nextafter(boundary,-std::numeric_limits<double>::infinity()),boundary,
                std::nextafter(boundary,std::numeric_limits<double>::infinity())})
            exact_scores(e::scores(event,g,threshold),summary::scores(value,threshold));
    }
}
void actual_engine(std::size_t chip,unsigned q,bool perturb) {
    Fixture f(chip,q,perturb,1,false);
    const auto visit=[&](auto a,auto b,auto emit){for(auto n=a;n<b;++n)emit(n,f.pcm[n]);};
    const auto out=e::run(f.g,e::reservation(f.g),[&](auto k,auto,auto){return f.cell(k);},visit,
        [&](auto j,auto a,auto b){return f.classify(j,a,b);},[&](auto j,auto n,bool need){return f.row(j,n,need);},
        [&](auto k){return f.coefficients(k);});
    check(out.valid&&out.events.size()==8,"summary fixture event engine failed");
    for(const auto& event:out.events) {
        compare_thresholds(event,f.g);
        auto no_outer=f.g;no_outer.outer=false;compare_thresholds(event,no_outer);
        const auto value=summary::summarize(event,f.g);
        for(unsigned bit=0;bit<2;++bit) {
            fit_close(value.whole[bit],event.whole[bit]);fit_close(value.chips[bit].cell,event.chips[bit].cell);
            exact_double(value.chips[bit].projected_energy,event.chips[bit].projected_energy,"summary chip energy changed");
            check(value.chips[bit].projected_rank==event.chips[bit].projected_rank&&
                value.chips[bit].cell_index==event.chips[bit].cell_index&&
                value.chips[bit].nested==event.chips[bit].nested,"summary chip state changed");
        }
    }
    auto unsupported=f.g;unsupported.sections=4;
    check(!summary::summarize(out.events[0],unsupported).valid,"summary silently omitted quarter guard");
    unsupported=f.g;unsupported.differential_width=chip*8;
    check(!summary::summarize(out.events[0],unsupported).valid,"summary silently omitted differential guard");
    unsupported=f.g;unsupported.node_spacing=0;
    check(!summary::summarize(out.events[0],unsupported).valid,"summary accepted invalid geometry");
    auto unfinished=out.events[0];unfinished.valid=false;
    check(!summary::summarize(unfinished,f.g).valid,"summary manufactured valid event");
}
e::Geometry single_geometry() {
    e::Geometry g;g.symbol_samples=87040;g.chip_samples=34;g.subdivisions=2;
    g.outputs=g.template_cells=1;g.capture_end=1;g.outer=true;return g;
}
e::EventStats colored_fixture() {
    e::EventStats event;event.valid=true;
    for(unsigned bit=0;bit<2;++bit) {
        event.whole[bit]={std::sqrt((bit?1.:1.1)*500),0,500,500,0,1000,87040};
        event.chips[bit].projected_energy=bit?700:900;event.chips[bit].projected_rank=5120;
        event.outer.variance[bit]=100;event.outer.contributing_terms[bit]=1000;event.outer.dot[bit]={100,0};
    }
    return event;
}
void threshold_and_partial_edges() {
    auto event=colored_fixture();const auto g=single_geometry();compare_thresholds(event,g);
    const auto frozen=summary::summarize(event,g);
    check(e::scores(event,g,30).bit==1&&e::scores(event,g,1e6).bit==0,"threshold-dependent winner fixture inactive");
    check(summary::scores(frozen,30).bit==1&&summary::scores(frozen,1e6).bit==0,"summary froze threshold/winner");
    // The selected outer score alone is checked by the original function.
    event.chips={};event.whole[0].xc=100;event.whole[1].xc=0;
    event.outer.dot[1]={std::numeric_limits<long double>::infinity(),0};
    check(std::isinf(event.outer.score(1)),"nonfinite outer score fixture inactive");
    check(e::scores(event,g,30).valid&&e::scores(event,g,30).bit==0,"unselected outer Inf fixture inactive");
    compare_thresholds(event,g);
    event.whole[0].xc=0;event.whole[1].xc=100;
    check(!e::scores(event,g,30).valid&&e::scores(event,g,30).bit==1,"selected outer Inf fixture inactive");
    compare_thresholds(event,g);
    for(auto count:{std::uint64_t{0},std::uint64_t{1},std::uint64_t{2},std::uint64_t{3}}) {
        event=colored_fixture();for(auto& fit:event.whole)fit.count=count;
        for(auto& chip:event.chips){chip.cell={.25,.125,.5,.25,.125,1,count};chip.projected_rank=0;chip.projected_energy=0;}
        compare_thresholds(event,g);
    }
    event=colored_fixture();event.chips[0].nested=false;event.chips[1].cell={};compare_thresholds(event,g);
}
void store_identity_and_completion() {
    using Store=summary::Store<whole_runtime::Key>;
    auto key=whole_runtime::Key{1000,11,22,3,4,5,13,4,-.3125L,8.5L};
    key.pending_bulk=true;key.bulk_scalar_bytes=sizeof(float);
    const auto value=summary::summarize(colored_fixture(),single_geometry());
    Store store(key,8,3,Store::reservation(3));
    const auto bytes=store.working_bytes();check(bytes<=Store::reservation(3),"summary store undercharged");
    check(store.insert(key,5,value,500,500)&&store.insert(key,1,value,200,500),"sparse summary insertion failed");
    check(store.insert(key,7,value,700,700)&&store.size()==3,"sparse summary last insertion failed");
    check(store.find(key,1,200)&&!store.find(key,1,199),"summary escaped actual published prefix");
    check(!store.find(key,2,1000)&&!store.find(key,8,1000),"summary fabricated missing origin");
    check(store.insert(key,1,value,200,1000)&&store.size()==3,"summary duplicate grew store");
    check(!store.insert(key,1,value,201,1000),"summary changed immutable completion interval");
    check(!store.insert(key,2,value,300,1000)&&store.size()==3,"summary capacity was not bounded");
    for(unsigned field=0;field<13;++field) {
        auto other=key;
        switch(field){case 0:++other.capture;break;case 1:++other.processing;break;case 2:++other.raw_capture;break;
        case 3:++other.index;break;case 4:++other.family;break;case 5:++other.first;break;case 6:++other.end;break;
        case 7:++other.subdivisions;break;case 8:other.physical_base+=.125L;break;case 9:other.node_spacing+=.125L;break;
        case 10:other.pending_bulk=false;break;case 11:other.bulk_scalar_bytes=sizeof(double);break;
        case 12:other.capture=0;other.processing=0;break;}
        check(!store.find(other,1,1000)&&!store.insert(other,1,value,200,1000),"summary identity alias accepted");
    }
    exact_scores(e::scores(colored_fixture(),single_geometry(),30),summary::scores(*store.find(key,1,1000),30));
    std::stop_source stop;stop.request_stop();bool cancelled=false;
    try{(void)store.insert(key,3,value,400,1000,stop.get_token());}catch(const datapump::Error&){cancelled=true;}
    check(cancelled&&store.size()==3&&!store.find(key,3,1000),"cancelled summary publication changed store");
    store.clear();check(store.size()==0&&!store.find(key,1,1000)&&store.working_bytes()==bytes,"summary retirement changed charge or retained value");
    check(!store.insert(key,2,value,301,300)&&!store.find(key,2,1000),"future numerical summary was installed");
    check(store.insert(key,2,value,301,301),"summary resume after actual completion failed");
    bool denied=false;try{Store over(key,8,3,Store::reservation(3)-1);}catch(const datapump::Error&){denied=true;}
    check(denied,"summary preflight did not enforce budget");
    check(sizeof(summary::Summary)<sizeof(e::EventStats)/2,"summary did not remove bulky guard state");
}
int main(){try{if(!d::scheduled_acquisition_certificate::environment<double>())return 77;
    actual_engine(34,2,false);actual_engine(34,4,true);actual_engine(8,4,true);
    threshold_and_partial_edges();store_identity_and_completion();
    std::cout<<"Native coherent guard summaries retain threshold branches, identity, actual completion, cancellation and bounded storage\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
