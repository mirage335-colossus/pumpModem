#include "scheduled_default_prepared_events.hpp"
#include "scheduled_default_point_family.hpp"
#include "scheduled_default_point_guard_archive_rebuild.hpp"
#include "scheduled_default_point_fft_runtime.hpp"
#include <memory>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace datapump::modem;
namespace d=datapump::modem::detail;
namespace e=d::scheduled_default_linear_events;
namespace grid=d::scheduled_linear_grid;
namespace p=d::scheduled_default_prepared_events;
namespace archive=d::scheduled_default_point_guard_archive;
namespace point=d::scheduled_default_linear_cellstats;
using Complex=std::complex<double>;
void check(bool x,const char* message){if(!x)throw std::runtime_error(message);}
void close(double x,double y,const char* message){check(std::isfinite(x)&&std::isfinite(y)&&std::abs(x-y)<2e-9*std::max({1.,std::abs(x),std::abs(y)}),message);}
void fit_close(const d::CorrelationFit& a,const d::CorrelationFit& b){check(a.count==b.count,"count changed");
    close(a.xc,b.xc,"xc changed");close(a.xs,b.xs,"xs changed");close(a.cc,b.cc,"cc changed");
    close(a.ss,b.ss,"ss changed");close(a.cs,b.cs,"cs changed");close(a.energy,b.energy,"raw energy changed");}
void append_fit(d::CorrelationFit& a,const d::CorrelationFit& b){a.xc+=b.xc;a.xs+=b.xs;a.cc+=b.cc;a.ss+=b.ss;a.cs+=b.cs;a.energy+=b.energy;a.count+=b.count;}
struct Fixture {
    e::Geometry g;
    std::vector<d::CorrelationProjection> pcm;
    std::array<long double,8> observed{};
    std::uint64_t first_active=2,last_active=0;
    std::size_t chip=0,q=0;
    bool perturb=false;
    explicit Fixture(std::size_t c,unsigned subdivisions,bool changed,unsigned sections,bool differential):chip(c),q(subdivisions),perturb(changed) {
        g.physical_base=-.3125L;g.node_spacing=static_cast<long double>(chip)/q;
        g.chip_samples=chip;g.symbol_samples=64*chip;g.subdivisions=q;g.sections=sections;
        g.differential_width=differential?8*chip:0;g.outer=true;
        g.template_cells=64*q+42;g.outputs=8;g.origin_first=3;g.step=q/2;g.fragment_cells=37;
        last_active=64*q+41;g.capture_first=static_cast<std::uint64_t>(std::ceil(g.node_spacing*5));
        g.capture_end=static_cast<std::uint64_t>(std::ceil(g.physical_base+(g.template_cells+(g.origin_first+7)*g.step)*g.node_spacing));
        for(std::size_t j=0;j<8;++j)observed[j]=g.capture_first>0?3*chip/8.L:0;
        g.observed_source_begin=observed;pcm.resize(g.capture_end);
        Complex carrier;const auto step=std::polar(1.,.71);
        for(std::size_t n=0;n<pcm.size();++n){if(n%512==0)carrier=std::polar(1.,.71*n);
            const auto x=.3*std::sin(.113*n)+.17*std::cos(.301*n)+.09*std::sin(.017*n*n);
            pcm[n]={x*carrier.real(),x*carrier.imag(),carrier.real()*carrier.real(),carrier.imag()*carrier.imag(),carrier.real()*carrier.imag(),x*x};carrier*=step;}
    }
    long double node(std::uint64_t n)const{return (static_cast<long double>(n)-g.physical_base)/g.node_spacing;}
    std::uint64_t source_node(std::size_t k)const {
        const std::size_t a=16*q,b=24*q;
        if(k<2)return e::no_owner;k-=2;if(k<a)return k;k-=a;
        if(k<17)return e::no_owner;k-=17;if(k<b)return a+k;k-=b;
        if(k<23)return e::no_owner;k-=23;if(k<24*q)return a+b+k;return e::no_owner;
    }
    double gain(long double s)const {
        const auto cell=static_cast<std::uint64_t>(std::floor(s));
        const std::uint64_t group=cell<16*q?0:cell<40*q?16*q:40*q;
        const auto end=group+(group==0?16*q:24*q);const auto phase=s-group;
        const auto a=std::clamp(static_cast<double>(phase/q),0.,1.),b=std::clamp(static_cast<double>((end-s)/q),0.,1.);
        return a*a*(3-2*a)*b*b*(3-2*b);
    }
    e::Pair value(long double s)const {
        const auto x=static_cast<double>(s/q);
        return {Complex{.42*std::sin(.193*x)+.11,.31*std::cos(.117*x)},Complex{.29*std::cos(.139*x)-.08,.37*std::sin(.173*x)}};
    }
    e::Cell cell(std::size_t k)const {
        e::Cell c;const auto sn=source_node(k);if(sn==e::no_owner){c.chord.supported=true;c.chord.observed=false;c.outer_zero=true;return c;}
        c.source_node=sn;c.outer_gain_left=gain(sn);c.outer_gain_right=gain(sn+1);
        c.chord.observed=true;c.chord.supported=sn!=0&&sn!=16*q&&sn!=40*q&&sn+1!=64*q;
        c.chord.residual={0,0};const auto l=value(sn),r=value(sn+1);
        for(unsigned bit=0;bit<2;++bit){c.chord.left[bit]=c.outer_gain_left*l[bit];c.chord.right[bit]=c.outer_gain_right*r[bit];}
        // A chosen quiet global chord may still overlap Original observations
        // in another translated lane. It retains E/N and has explicit public
        // outer-zero metadata; private zero alone is never that proof.
        if(sn==7*q+1){c.chord.left=c.chord.right={};c.outer_zero=true;c.source_node=e::no_owner;}
        return c;
    }
    e::Row row(std::size_t j,std::uint64_t n,bool needq)const {
        e::Row r;if(n<g.capture_first||n>=g.capture_end)return r;
        const auto u=node(n)-j*g.step;if(u<0||u>=g.template_cells)return r;
        const auto k=static_cast<std::size_t>(std::floor(u));const auto sn=source_node(k);if(sn==e::no_owner)return r;
        const auto native=static_cast<long double>(sn)+(u-k);
        const auto change=perturb?(static_cast<long double>(j%3)-1)*.0003L:0;
        r.source=native*chip/q*(1+change)+.00013L*(static_cast<long double>(j%3)-1);
        r.gain=gain(native);if(r.source<0||r.source>=g.symbol_samples||r.gain<=0)return r;
        r.owner=static_cast<std::uint64_t>(std::floor(r.source/chip));r.observed=true;
        if(needq){r.q=value(native);for(auto& x:r.q)x*=r.gain;}return r;
    }
    e::Partition classify(std::size_t j,std::uint64_t first,std::uint64_t end)const {
        e::Partition out;if(first==end)return {true,false};
        // A pure oracle fixture exhaustively verifies the complete selected set.
        // The production callback instead proves this using Original pieces.
        for(auto n=first;n<end;++n){const auto r=row(j,n,false);if(!r.observed)return {};
            const e::Partition p{true,true,r.owner,e::window_at(g,r.source),e::section_at(g,r.source)};
            if(n==first)out=p;else if(p.owner!=out.owner||p.window!=out.window||p.section!=out.section)return {};}
        if((first/7)%11==0)return {}; // same chosen template despite forced partition
        return out;
    }
    e::Pair coefficients(std::uint64_t k)const{return {std::polar(.71,.17*k),std::polar(.63,-.23*k)};}
    void outer(e::EventStats& out,const e::Cell& c,const e::Row& row,const d::CorrelationProjection& p,double t)const {
        if(c.chord.supported&&c.outer_zero)return;
        const Complex z{p.xc,p.xs};
        if(c.chord.supported){const auto owner=c.source_node/q,phase=c.source_node%q;
            for(int d=-8;d<=8;++d){if(d<0&&owner<static_cast<std::uint64_t>(-d))continue;
                const auto k=d<0?owner-static_cast<std::uint64_t>(-d):owner+d;if(k>=64)continue;
                const auto l=c.outer_gain_left*pattern_pulse(static_cast<double>(phase)/q-d-.5);
                const auto r=c.outer_gain_right*pattern_pulse(static_cast<double>(phase+1)/q-d-.5);
                out.outer.add(k,((1-t)*l+t*r)*z,[&](auto k){return coefficients(k);});}
        }else pattern_pulse_each(static_cast<double>(row.source),g.symbol_samples,g.chip_samples,[&](auto k,double p){
            out.outer.add(k,(row.gain*p)*z,[&](auto k){return coefficients(k);});});
    }
    e::EventStats reference(std::size_t j)const {
        e::EventStats out;
        for(std::uint64_t n=g.capture_first;n<g.capture_end;++n){auto rowvalue=row(j,n,true);if(!rowvalue.observed)continue;
            const auto u=node(n)-j*g.step,k=std::floor(u);const auto c=cell(static_cast<std::size_t>(k));const auto t=static_cast<double>(u-k);
            if(c.chord.supported)for(unsigned b=0;b<2;++b)rowvalue.q[b]=(1-t)*c.chord.left[b]+t*c.chord.right[b];
            const auto section=e::section_at(g,rowvalue.source);const auto window=e::window_at(g,rowvalue.source);
            out.last_owner=rowvalue.owner;out.last_section=section;out.last_window=window;
            if(out.observed_start==e::no_owner)out.observed_start=n;
            for(unsigned b=0;b<2;++b){d::CorrelationFit f;f.add(pcm[n],rowvalue.q[b],1);append_fit(out.whole[b],f);out.chips[b].add_fit(f,rowvalue.owner);
                if(g.sections>1){auto& s=out.drift[b];while(s.section<section){if(e::full_rank(s.active))out.quarter_ranks[b]|=1U<<s.section;
                    const auto x=s.active.explained();s.explained_sum+=x;s.largest_explained=std::max(s.largest_explained,x);s.active={};++s.section;}append_fit(s.active,f);}
                if(g.differential_width){auto& w=out.differential[b];if(!w.initialized||w.window_index!=window){if(w.initialized){
                        w.expected_count=w.window_index*g.differential_width>=observed[j-g.origin_first]?w.active.count:0;w.finish(g.symbol_samples,g.differential_width);}
                        w.active={};w.initialized=true;w.window_index=window;}append_fit(w.active,f);}
            }
            if(g.outer)outer(out,c,rowvalue,pcm[n],t);
        }
        if(g.differential_width)for(auto& w:out.differential)if(w.initialized)
            w.expected_count=w.window_index*g.differential_width>=observed[j-g.origin_first]?w.active.count:0;
        if(g.outer)out.outer.finish([&](auto k){return coefficients(k);});out.valid=true;return out;
    }
};

void event_close(const e::EventStats& actual,const e::EventStats& expected,const e::Geometry& g) {
    check(actual.valid==expected.valid&&actual.observed_start==expected.observed_start,"prepared validity/crop changed");
    check(actual.last_owner==expected.last_owner&&actual.last_section==expected.last_section&&actual.last_window==expected.last_window,"prepared partition end changed");
    for(unsigned b=0;b<2;++b) {
        fit_close(actual.whole[b],expected.whole[b]);fit_close(actual.chips[b].cell,expected.chips[b].cell);
        close(actual.chips[b].projected_energy,expected.chips[b].projected_energy,"prepared chip projection changed");
        check(actual.chips[b].projected_rank==expected.chips[b].projected_rank&&actual.chips[b].nested==expected.chips[b].nested,"prepared chip rank/nesting changed");
        if(g.sections>1) {
            fit_close(actual.drift[b].active,expected.drift[b].active);
            close(actual.drift[b].explained_sum,expected.drift[b].explained_sum,"prepared quarter projection changed");
            close(actual.drift[b].largest_explained,expected.drift[b].largest_explained,"prepared quarter maximum changed");
            check(actual.quarter_ranks[b]==expected.quarter_ranks[b],"prepared quarter rank changed");
        }
        if(g.differential_width) {
            const auto& a=actual.differential[b];const auto& r=expected.differential[b];
            fit_close(a.active,r.active);check(a.initialized==r.initialized&&a.window_index==r.window_index&&a.expected_count==r.expected_count,"prepared differential completeness changed");
            check(a.evidence.pairs==r.evidence.pairs&&a.evidence.pending==r.evidence.pending,"prepared differential pair changed");
            close(a.evidence.squared_magnitudes,r.evidence.squared_magnitudes,"prepared differential mass changed");
            for(unsigned k=0;k<4;++k){close(a.evidence.sums[k].real(),r.evidence.sums[k].real(),"prepared differential real changed");
                close(a.evidence.sums[k].imag(),r.evidence.sums[k].imag(),"prepared differential imag changed");}
            close(a.score(g.symbol_samples,g.differential_width),r.score(g.symbol_samples,g.differential_width),"prepared differential evidence changed");
        }
        if(g.outer) {
            close(actual.outer.dot[b].real(),expected.outer.dot[b].real(),"prepared outer real changed");
            close(actual.outer.dot[b].imag(),expected.outer.dot[b].imag(),"prepared outer imag changed");
            close(actual.outer.variance[b],expected.outer.variance[b],"prepared outer variance changed");
            check(actual.outer.contributing_terms[b]==expected.outer.contributing_terms[b],"prepared outer support changed");
            close(actual.outer.score(b),expected.outer.score(b),"prepared outer score changed");
        }
    }
    for(double threshold:{0.,5.,27.,91.}) {
        const auto a=e::scores(actual,g,threshold),b=e::scores(expected,g,threshold);
        check(a.valid&&b.valid&&a.bit==b.bit&&a.outer_presence_active==b.outer_presence_active,"prepared guard branch changed");
        close(a.score[0],b.score[0],"prepared zero score changed");close(a.score[1],b.score[1],"prepared one score changed");
    }
}
e::Cell normalized_cell(const Fixture& f,std::size_t k) {
    auto c=f.cell(k);if(c.chord.supported&&!c.chord.observed){c.chord.left={};c.chord.right={};c.chord.observed=true;}return c;
}
std::unique_ptr<archive::Archive> prepare(const Fixture& f,bool holes) {
    const auto& g=f.g;const auto first=g.origin_first*g.step,extent=g.template_cells+(g.outputs-1)*g.step;
    archive::Layout l{g.physical_base,g.node_spacing,g.capture_first,g.capture_end,g.template_cells,first,extent,g.origin_first,g.outputs,g.step};
    auto a=std::make_unique<archive::Archive>(l,archive::Archive::reservation(l));
    for(std::size_t k=0;k<g.template_cells;++k){const auto c=normalized_cell(f,k);a->install_cells(k,std::span(&c,1));}
    point::Moments m(extent,point::Moments::reservation(extent));
    for(auto n=g.capture_first;n<g.capture_end;++n){const auto u=f.node(n)-first;if(u>=0&&u<extent)m.add(n,u,f.pcm[n],1,true);}m.finish();
    for(std::size_t k=0;k<extent;++k)if(!holes||k%11!=0)
        a->install_moments(first+k,m.rows().subspan(k,1),m.energy().subspan(k,1),m.count().subspan(k,1));
    a->seal();return a;
}

void rebuilt_archive_and_completed_summaries(std::size_t chip,unsigned q) {
    namespace rebuild=d::scheduled_default_point_guard_archive_rebuild;
    namespace summary=d::scheduled_default_point_guard_summary;
    namespace wrapper=d::scheduled_default_point_fft_runtime;
    Fixture f(chip,q,true,1,false);const auto& g=f.g;
    d::scheduled_default_point_family::Geometry fg{g.physical_base,g.node_spacing,g.capture_first,g.capture_end,
        g.template_cells,g.outputs,g.origin_first,g.step,g.fragment_cells};
    const archive::Layout layout{g.physical_base,g.node_spacing,g.capture_first,g.capture_end,g.template_cells,
        g.origin_first*g.step,g.template_cells+(g.outputs-1)*g.step,g.origin_first,g.outputs,g.step};
    auto owned=std::make_unique<archive::Archive>(layout,archive::Archive::reservation(layout));
    const auto expected=prepare(f,false);const auto bytes=rebuild::reservation(fg);
    const auto work=rebuild::run(fg,bytes,*owned,[&](auto k,auto,auto){return f.cell(k);},
        [&](auto a,auto b,auto emit){for(auto n=a;n<b;++n)emit(n,f.pcm[n]);});
    check(owned->sealed()&&work.fragments>1&&work.input_rows&&work.peak_bytes<=bytes,"rebuilt archive incomplete or uncharged");
    for(std::size_t k=0;k<g.template_cells;++k){const auto* a=owned->cell(k);const auto* b=expected->cell(k);
        check(a&&b&&a->chord.left==b->chord.left&&a->chord.right==b->chord.right&&a->chord.observed==b->chord.observed&&
            a->chord.supported==b->chord.supported&&a->source_node==b->source_node&&a->outer_zero==b->outer_zero&&
            a->outer_gain_left==b->outer_gain_left&&a->outer_gain_right==b->outer_gain_right,"archive chosen cell identity changed");}
    for(auto k=layout.input_first;k<layout.input_first+layout.input_cells;++k){const auto a=owned->moment(k),b=expected->moment(k);
        check(a.row&&b.row&&a.count==b.count,"archive rebuilt missing/count rows");close(a.energy,b.energy,"archive rebuilt energy changed");
        for(unsigned z=0;z<8;++z){close(a.row->at(z).real(),b.row->at(z).real(),"archive rebuilt moment real changed");
            close(a.row->at(z).imag(),b.row->at(z).imag(),"archive rebuilt moment imaginary changed");}}
    auto native=p::run(g,p::reservation(g),*owned,[&](auto n){return f.pcm[n];},
        [&](auto j,auto a,auto b){return f.classify(j,a,b);},[&](auto j,auto n,bool need){return f.row(j,n,need);},[&](auto k){return f.coefficients(k);});
    auto direct=e::run(g,e::reservation(g),[&](auto k,auto,auto){return normalized_cell(f,k);},
        [&](auto a,auto b,auto emit){for(auto n=a;n<b;++n)emit(n,f.pcm[n]);},[&](auto j,auto a,auto b){return f.classify(j,a,b);},
        [&](auto j,auto n,bool need){return f.row(j,n,need);},[&](auto k){return f.coefficients(k);});
    check(native.valid&&direct.valid,"completed summary source engine failed");
    wrapper::Entry entry;entry.key.capture=g.capture_end;entry.key.index=4;entry.key.family=3;entry.key.node_spacing=g.node_spacing;
    entry.reserved=wrapper::Entry::reservation(g.outputs);
    entry.guard_summaries=std::make_unique<summary::Store<wrapper::Key>>(entry.key,g.outputs,g.outputs,summary::Store<wrapper::Key>::reservation(g.outputs));
    check(entry.working_bytes()==entry.reserved+entry.guard_summaries->working_bytes(),"completed summaries uncharged or double-counted");
    for(std::size_t j=0;j<g.outputs;++j){event_close(native.events[j],direct.events[j],g);const auto completed=summary::summarize(native.events[j],g);
        check(completed.valid&&!entry.guard_summaries->insert(entry.key,j,completed,g.capture_end,g.capture_end-1),"future interval became a completed summary");
        check(entry.guard_summaries->insert(entry.key,j,completed,g.capture_end,g.capture_end),"current due summary not retained");
        for(const auto threshold:{0.,5.,27.,91.,1000.}){const auto a=summary::scores(*entry.guard_summaries->find(entry.key,j,g.capture_end),threshold);
            const auto b=e::scores(native.events[j],g,threshold);check(a.valid==b.valid&&a.bit==b.bit&&a.outer_presence_active==b.outer_presence_active,"actual threshold branch changed");
            for(unsigned bit=0;bit<2;++bit)close(a.score[bit],b.score[bit],"completed summary rescoring changed statistic");}}
    const auto saved=*entry.guard_summaries->find(entry.key,0,g.capture_end);owned.reset();native={};direct={};
    check(summary::scores(saved,27).valid&&entry.guard_summaries->find(entry.key,0,g.capture_end),"summary borrowed retired archive/result");
    auto other=entry.key;++other.index;check(!entry.guard_summaries->find(other,0,g.capture_end),"summary crossed private symbol identity");
    ++other.processing;check(!entry.guard_summaries->find(other,0,g.capture_end),"summary crossed Original fence identity");
    check(!entry.guard_summaries->find(entry.key,0,g.capture_end-1),"unpublished interval became usable");
}
void rebuild_failures() {
    namespace rebuild=d::scheduled_default_point_guard_archive_rebuild;
    Fixture f(17,4,false,1,false);const auto& g=f.g;
    d::scheduled_default_point_family::Geometry fg{g.physical_base,g.node_spacing,g.capture_first,g.capture_end,g.template_cells,g.outputs,g.origin_first,g.step,37};
    const archive::Layout l{g.physical_base,g.node_spacing,g.capture_first,g.capture_end,g.template_cells,g.origin_first*g.step,
        g.template_cells+(g.outputs-1)*g.step,g.origin_first,g.outputs,g.step};
    const auto fails=[&](auto run){bool failed=false;try{run();}catch(const datapump::Error&){failed=true;}check(failed,"invalid rebuild was silently accepted");};
    {archive::Archive a(l,archive::Archive::reservation(l));fails([&]{rebuild::run(fg,rebuild::reservation(fg)-1,a,
        [&](auto k,auto,auto){return f.cell(k);},[&](auto lo,auto hi,auto emit){for(auto n=lo;n<hi;++n)emit(n,f.pcm[n]);});});check(!a.sealed(),"undercharged archive published");}
    {archive::Archive a(l,archive::Archive::reservation(l));fails([&]{rebuild::run(fg,rebuild::reservation(fg),a,
        [&](auto k,auto,auto){return f.cell(k);},[&](auto lo,auto hi,auto emit){for(auto n=lo;n+1<hi;++n)emit(n,f.pcm[n]);});});check(!a.sealed()&&!a.cell(0),"missing capture row published archive");}
    {archive::Archive a(l,archive::Archive::reservation(l));std::stop_source stop;fails([&]{rebuild::run(fg,rebuild::reservation(fg),a,
        [&](auto k,auto,auto){if(k==40)stop.request_stop();return f.cell(k);},[&](auto lo,auto hi,auto emit){for(auto n=lo;n<hi;++n)emit(n,f.pcm[n]);},stop.get_token());});
        check(!a.sealed()&&!a.cell(0),"cancelled archive published partial cells");}
}
int main(){try{rebuilt_archive_and_completed_summaries(17,4);rebuilt_archive_and_completed_summaries(5,8);
    rebuild_failures();std::cout<<"actual-due guard prepass archive/summary source cases passed\n";return 0;}
    catch(const std::exception& x){std::cerr<<x.what()<<'\n';return 1;}}
