#include "scheduled_default_point_bulk.hpp"
// Preserve every maintained reference assertion; this target runs only the
// new split tests while sharing the existing independent PCM/mask fixture.
#define main maintained_default_point_family_main
#include "test_scheduled_default_point_family.cpp"
#undef main
namespace bulk=d::scheduled_default_point_bulk;
template<class T>concept AdmissionReady=requires(T x){x.ready;};
static_assert(!AdmissionReady<bulk::PendingBulk>);
static_assert(std::tuple_size_v<bulk::Fields::value_type> ==5);
static std::uint64_t identity(const bulk::PendingBulk& p){std::uint64_t h=0;
    for(const auto& row:p.fields)for(const auto& bit:row)for(const auto x:bit)h=(h*131)^std::bit_cast<std::uint64_t>(x);return h;}
static void extend_capture(Fixture& f,std::uint64_t end){const auto old=f.pcm.size();f.pcm.resize(end);
    for(auto n=old;n<end;++n){const auto x=.37*std::sin(.13*n)+.29*std::cos(.041*n)+.07*std::sin(.007*n*n);const auto o=std::polar(1.,.731*n);
        f.pcm[n]={x*o.real(),x*o.imag(),o.real()*o.real(),o.imag()*o.imag(),o.real()*o.imag(),x*x};}f.g.capture_end=end;}
template<class T>static void paired(){
    Fixture f(3,33);extend_capture(f,f.g.capture_end+512);
    const auto old=family::run<T>(f.g,family::reservation<T>(f.g),f.cells(),f.visit(),f.selections,f.fallback());
    std::uint64_t cell_calls=0,visit_rows=0;
    const auto cells=[&](auto k,auto,auto){++cell_calls;return f.cell(k);};
    const auto visit=[&](auto a,auto b,auto emit){check(a>=f.g.capture_first&&b<=f.g.capture_end,"future data escaped frozen capture");
        for(auto n=a;n<b;++n){++visit_rows;emit(n,f.pcm[n]);}};
    const auto pending=bulk::prepare<T>(f.g,bulk::reservation<T>(f.g),cells,visit);
    check(pending.complete&&pending.fields.size()==33&&pending.chords.size()==f.g.template_cells,"bulk omitted captured outputs");
    check(cell_calls==f.g.template_cells&&pending.work.transforms.inverse_ffts==16,"bulk recomputed templates per origin");
    const auto frozen=identity(pending),shape_calls=cell_calls;const auto tolerance=std::is_same_v<T,float>?9e-5:3e-10;
    std::size_t first=0;
    for(std::size_t through:{3U,9U,19U,33U}){
        auto g=f.g;g.origin_first+=first;g.outputs=through-first;
        const auto selections=std::span<const observation::Selection>(f.selections).subspan(first,g.outputs);
        const auto result=bulk::finalize_due_batch(pending,g.origin_first,selections,bulk::finalize_reservation(g),visit,f.fallback());
        check(result.whole.size()==g.outputs,"due finalize omitted a current lane");
        for(std::size_t j=0;j<g.outputs;++j)for(unsigned b=0;b<2;++b){const auto&a=result.whole[j][b];const auto&e=old.whole[first+j][b];
            check(a.count==e.count&&result.valid[j]==old.valid[first+j],"actual Selection/count/valid changed");
            close(a.energy,e.energy,3e-13,"actual selected energy changed");close(a.xc,e.xc,tolerance,"bulk xc differs");close(a.xs,e.xs,tolerance,"bulk xs differs");
            close(a.cc,e.cc,tolerance,"bulk cc differs");close(a.ss,e.ss,tolerance,"bulk ss differs");close(a.cs,e.cs,tolerance,"bulk cs differs");
            close(a.score(),e.score(),tolerance*8,"bulk point score differs");}
        check(result.work.peak_bytes<=bulk::finalize_reservation(g),"due finalizer exceeded workspace");
        check(identity(pending)==frozen&&cell_calls==shape_calls,"actual finalize changed/rebuilt frozen bulk");first=through;
    }
    check(pending.work.peak_bytes<=bulk::reservation<T>(f.g)&&visit_rows>0,"bulk memory/input accounting absent");
    std::stop_source stopped;stopped.request_stop();
    fails([&]{bulk::finalize_lane(pending,f.g.origin_first,f.selections[0],bulk::finalize_reservation(f.g),visit,f.fallback(),stopped.get_token());},"cancelled finalize returned ready fields");
    check(identity(pending)==frozen,"cancelled finalize damaged reusable bulk");
    fails([&]{bulk::prepare<T>(f.g,1,cells,visit);},"bulk ignored workspace bound");
    fails([&]{bulk::prepare<T>(f.g,bulk::reservation<T>(f.g),cells,visit,stopped.get_token());},"bulk cancellation ignored");
    auto wrong=f.selections[0];++wrong.count;
    fails([&]{bulk::finalize_lane(pending,f.g.origin_first,wrong,bulk::finalize_reservation(f.g),visit,f.fallback());},"bad actual count accepted");
    const auto missing=[&](auto a,auto b,auto emit){for(auto n=a+1;n<b;++n)emit(n,f.pcm[n]);};
    fails([&]{bulk::finalize_lane(pending,f.g.origin_first,f.selections[0],bulk::finalize_reservation(f.g),missing,f.fallback());},"lost correction history silently accepted");
    auto partial=f.g;partial.capture_end=static_cast<std::uint64_t>(std::ceil(partial.physical_base+
        (partial.template_cells+(partial.origin_first+14)*partial.step)*partial.node_spacing))+2;
    const auto prefix=bulk::capture_complete_prefix(partial);check(prefix>0&&prefix<partial.outputs,"partial capture prefix classifier ineffective");
    fails([&]{bulk::prepare<T>(partial,bulk::reservation<T>(partial),cells,visit);},"incomplete padded apertures were prepared");
    partial.outputs=prefix;const auto partial_bulk=bulk::prepare<T>(partial,bulk::reservation<T>(partial),cells,visit);
    check(partial_bulk.fields.size()==prefix,"partial capture manufactured extra outputs");
}
int main(){try{paired<float>();paired<double>();std::cout<<"capture-complete pending bulk and actual-due finalize preserve same-chord fields, masks, E/N, cancellation and bounds\n";return 0;}
catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
