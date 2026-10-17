#include "scheduled_default_point_guard_strip.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace d=datapump::modem::detail;
namespace a=d::scheduled_default_point_guard_archive;
namespace point=d::scheduled_default_linear_cellstats;
namespace events=d::scheduled_default_linear_events;
namespace fft=d::scheduled_linear_grid_fft;
namespace {
void check(bool x,const char* message){if(!x)throw std::runtime_error(message);}
bool strip_arithmetic_supported(){return d::scheduled_acquisition_certificate::environment<double>()&&
    std::numeric_limits<long double>::digits>std::numeric_limits<double>::digits;}
template<class F>void fails(F&& f,const char* message){bool bad=false;try{f();}catch(const datapump::Error&){bad=true;}check(bad,message);}
void close(double x,double y,const char* message){check(std::isfinite(x)&&std::isfinite(y)&&std::abs(x-y)<=2e-12*std::max({1.,std::abs(x),std::abs(y)}),message);}
struct Fixture {
    a::Layout l{-.375L,8.5L,0,94,7,0,11,0,3,2};
    std::vector<d::CorrelationProjection> pcm;
    std::vector<events::Cell> cells;
    Fixture():pcm(l.capture_end),cells(l.template_cells) {
        for(std::uint64_t n=0;n<l.capture_end;++n) {
            const auto x=.31*std::cos(.13*n)+.19*std::sin(.21*n)+.07;
            const auto o=std::polar(1.,.731*n);
            pcm[n]={x*o.real(),x*o.imag(),o.real()*o.real(),o.imag()*o.imag(),o.real()*o.imag(),x*x};
        }
        for(auto& c:cells){c.chord.supported=true;c.chord.residual={0,0};c.chord.left={fft::Wide{.4,.2},fft::Wide{-.2,.3}};
            c.chord.right={fft::Wide{.5,-.1},fft::Wide{.3,.4}};}
    }
    long double node(std::uint64_t n)const{return (static_cast<long double>(n)-l.physical_base)/l.node_spacing;}
    std::uint64_t lower(std::size_t cell)const {auto n=l.capture_first;while(n<l.capture_end&&node(n)<cell)++n;return n;}
    void capture(a::Archive& archive,std::size_t first,std::size_t end)const {
        a::EdgeCapture capture(archive);
        for(auto n=lower(first);n<lower(end);++n) {
            const auto u=node(n);const auto cell=static_cast<std::size_t>(std::floor(u));
            capture.add(cell,n,static_cast<double>(u-cell),pcm[n]);
        }
        capture.finish();
    }
    void moments(a::Archive& archive)const {
        point::Moments m(l.input_cells,point::Moments::reservation(l.input_cells));
        for(auto n=l.capture_first;n<l.capture_end;++n){const auto u=node(n);if(u>=0&&u<l.input_cells)m.add(n,u,pcm[n],1);}
        m.finish();archive.install_moments(l.input_first,m.rows(),m.energy(),m.count());
    }
    void fill(a::Archive& archive)const {
        // Overlapping fragment halos before Moments are legal and exact.
        capture(archive,0,7);capture(archive,4,11);moments(archive);archive.install_cells(0,cells);archive.seal();
    }
};
void roundtrip(unsigned width) {
    Fixture f;const auto bytes=a::Archive::reservation(f.l,width);a::Archive archive(f.l,bytes,{},width);f.fill(archive);
    check(archive.edge_width()==width&&archive.working_bytes()==bytes,"edge capacity charge changed");
    check(archive.stored_edge_rows()==f.l.input_cells*2*width,"duplicate halo rows counted twice");
    for(std::size_t cell=0;cell<f.l.input_cells;++cell) {
        const auto* e=archive.edge(cell);check(e&&e->count==archive.moment(cell).count&&a::valid_edges(*e,f.l,cell,width),"edge support changed");
        check(e->head_count==width&&e->tail_count==width,"full cell endpoint width changed");
        for(unsigned k=0;k<width;++k) {
            check(a::same_projection(e->head[k].projection,f.pcm[e->first+k]),"head projection changed");
            check(a::same_projection(e->tail[k].projection,f.pcm[e->end-width+k]),"tail projection changed");
        }
    }
    const std::size_t cell=3;const auto* e=archive.edge(cell);const auto first=e->first+width,end=e->end-width;
    const auto stripped=a::strip(archive.moment(cell),*e,first,end,f.l,cell);
    if(!strip_arithmetic_supported()) {
        check(!stripped.known&&!stripped.count&&stripped.energy==0,"unsupported subtraction published an interior strip");
        return;
    }
    check(stripped.known&&stripped.count==end-first&&stripped.count>2,"valid interior strip became unknown");
    fft::InputRow direct{};long double energy=0;std::array<d::CorrelationFit,2> fits;
    for(auto n=first;n<end;++n) {
        const auto t=static_cast<double>(f.node(n)-cell);const auto planes=a::edge_planes({n,f.pcm[n],t});
        for(unsigned k=0;k<8;++k)direct[k]+=planes[k];energy+=f.pcm[n].energy;
        for(unsigned bit=0;bit<2;++bit)fits[bit].add(f.pcm[n],(1-t)*f.cells[cell].chord.left[bit]+t*f.cells[cell].chord.right[bit],1);
    }
    for(unsigned k=0;k<8;++k){close(stripped.row[k].real(),direct[k].real(),"stripped real plane changed");close(stripped.row[k].imag(),direct[k].imag(),"stripped imag plane changed");}
    close(static_cast<double>(stripped.energy),static_cast<double>(energy),"stripped raw energy changed");
    const auto contracted=point::contract(stripped.row,f.cells[cell].chord,static_cast<double>(stripped.energy),stripped.count);
    check(contracted.valid&&!contracted.needs_refinement,"strip lost native rank2");
    for(unsigned bit=0;bit<2;++bit) {
        const auto& x=contracted.fits[bit];const auto& y=fits[bit];
        check(x.count==y.count,"stripped fit count changed");close(x.xc,y.xc,"stripped xc changed");close(x.xs,y.xs,"stripped xs changed");
        close(x.cc,y.cc,"stripped cc changed");close(x.ss,y.ss,"stripped ss changed");close(x.cs,y.cs,"stripped cs changed");
    }
    const auto measured=.6*stripped.row[0]+.9*stripped.row[1];const auto expected=.6*direct[0]+.9*direct[1];
    close(measured.real(),expected.real(),"stripped outer real moment changed");close(measured.imag(),expected.imag(),"stripped outer imag moment changed");
}
void holes_small_cells_and_unknowns() {
    Fixture f;const auto bytes=a::Archive::reservation(f.l,2);
    {
        a::Archive archive(f.l,bytes,{},2);f.moments(archive);archive.install_cells(0,f.cells);archive.seal();
        check(!archive.edge(3)&&archive.stored_edge_rows()==0,"missing edge packet became a zero");
    }
    for(std::uint64_t count=1;count<=4;++count) {
        auto small=f;small.l.capture_first=f.lower(3);small.l.capture_end=small.l.capture_first+count;
        a::Archive archive(small.l,a::Archive::reservation(small.l,2),{},2);small.capture(archive,3,4);small.moments(archive);
        archive.install_cells(0,small.cells);archive.seal();const auto* e=archive.edge(3);
        check(e&&e->head_count+e->tail_count==count&&archive.stored_edge_rows()==count,"small endpoints overlap or duplicate count");
        for(auto n=e->first;n<e->end;++n)check(a::find_edge(*e,n),"small endpoint union lost a raw row");
        if(count<=2)check(!a::strip(archive.moment(3),*e,e->first,e->end,small.l,3).known,"singleton/2row interior became bulk");
    }
    a::Archive archive(f.l,bytes,{},2);f.fill(archive);const auto* e=archive.edge(3);const auto input=archive.moment(3);
    const auto first=e->first+2,end=e->end-2;
    auto bad=*e;bad.head[0].t+=.01;check(!a::strip(input,bad,first,end,f.l,3).known,"wrong t admitted strip");
    bad=*e;bad.head[0].physical++;check(!a::strip(input,bad,first,end,f.l,3).known,"wrong physical endpoint admitted");
    bad=*e;bad.head_count=1;check(!a::strip(input,bad,first,end,f.l,3).known,"missing deleted endpoint admitted");
    auto mismatch=input;++mismatch.count;check(!a::strip(mismatch,*e,first,end,f.l,3).known,"moment/edge count mismatch admitted");
    mismatch=input;mismatch.energy=0;check(!a::strip(mismatch,*e,first,end,f.l,3).known,"negative residual energy clamped");
    auto changed=*input.row;changed[5]={0,0};mismatch=input;mismatch.row=&changed;
    check(!a::strip(mismatch,*e,first,end,f.l,3).known,"negative trace residual clamped");
    changed=*input.row;changed[0]={std::numeric_limits<double>::quiet_NaN(),0};
    check(!a::strip(mismatch,*e,first,end,f.l,3).known,"nonfinite moment admitted strip");
    check(!a::strip(input,*e,e->first+3,e->end,f.l,3).known,"uncached removed row invented");
    check(!a::strip(input,*e,e->first,e->first+2,f.l,3).known,"too-short bulk interior admitted");
    auto wrong=f.l;wrong.physical_base+=.1L;check(!a::strip(input,*e,first,end,wrong,3).known,"different grid admitted strip");
}
void validation_budget_and_cancel() {
    Fixture f;const auto bytes=a::Archive::reservation(f.l,2);
    check(bytes>a::Archive::reservation(f.l)&&bytes<=a::Archive::maximum_bytes,"edge rows uncharged");
    fails([&]{a::Archive x(f.l,a::Archive::reservation(f.l),{},2);},"ungranted edge allocation occurred");
    fails([&]{(void)a::Archive::reservation(f.l,3);},"unsupported edge width accepted");
    {
        a::Archive x(f.l,bytes,{},2);f.capture(x,0,7);x.install_cells(0,f.cells);
        fails([&]{x.seal();},"edge packet without matching Moments sealed");check(!x.sealed()&&!x.edge(0),"bad seal exposed edges");
    }
    {
        a::Archive x(f.l,bytes,{},2);f.capture(x,0,7);auto changed=f;changed.pcm[0].energy+=.01;
        fails([&]{changed.capture(x,0,7);},"conflicting overlapping projection accepted");check(!x.sealed()&&!x.stored_edge_rows(),"conflict retained usable archive");
    }
    {
        a::Archive x(f.l,bytes,{},2);{a::EdgeCapture unfinished(x);const auto n=f.lower(0);unfinished.add(0,n,static_cast<double>(f.node(n)),f.pcm[n]);}
        fails([&]{x.install_cells(0,f.cells);},"abandoned capture did not poison");
    }
    {
        a::Archive x(f.l,bytes,{},2);f.moments(x);x.install_cells(0,f.cells);a::EdgeCapture live(x);
        fails([&]{x.seal();},"live unfinished capture escaped through seal");
        check(!x.sealed()&&!x.cell(0)&&!x.edge(0),"live capture allowed publication before destruction");
    }
    {
        a::Archive x(f.l,bytes,{},2);a::EdgeCapture c(x);const auto n=f.lower(0);c.add(0,n,static_cast<double>(f.node(n)),f.pcm[n]);
        fails([&]{c.add(0,n+2,static_cast<double>(f.node(n+2)),f.pcm[n+2]);},"raw gap accepted");
    }
    {
        a::Archive x(f.l,bytes,{},2);std::stop_source stop;a::EdgeCapture c(x,stop.get_token());const auto n=f.lower(0);
        c.add(0,n,static_cast<double>(f.node(n)),f.pcm[n]);stop.request_stop();
        fails([&]{c.finish();},"edge capture cancellation ignored");check(!x.sealed()&&!x.edge(0),"cancelled capture exposed edges");
    }
    {
        a::Archive x(f.l,a::Archive::reservation(f.l));f.moments(x);x.install_cells(0,f.cells);x.seal();
        check(x.edge_width()==0&&!x.edge(0)&&x.stored_edge_rows()==0,"legacy noedge API changed");
    }
}
}
int main(){try {
    roundtrip(1);roundtrip(2);holes_small_cells_and_unknowns();validation_budget_and_cancel();
    if(!strip_arithmetic_supported())std::cout<<"unsupported strip arithmetic refusal verified\n";
    std::cout<<"Default point guard endpoint source cases passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
