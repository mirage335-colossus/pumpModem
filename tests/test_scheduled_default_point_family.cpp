#include "scheduled_default_point_family.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <stop_token>
#include <vector>
namespace d=datapump::modem::detail;
namespace family=d::scheduled_default_point_family;
namespace observation=d::scheduled_default_point_observation;
namespace events=d::scheduled_default_linear_events;
using Complex=std::complex<double>;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void close(double a,double b,double tolerance,const char* message){
    check(std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=tolerance*std::max({1.,std::abs(a),std::abs(b)}),message);
}
struct Fixture {
    family::Geometry g;
    std::vector<d::CorrelationProjection> pcm;
    std::vector<observation::Selection> selections;
    Fixture(std::size_t first,std::size_t outputs) {
        g={-.3125L,8.5L,3,0,83,outputs,first,2,13};
        g.capture_end=static_cast<std::uint64_t>(std::ceil(g.physical_base+(g.template_cells+(first+outputs-1)*g.step)*g.node_spacing))-7;
        pcm.resize(g.capture_end);
        for(std::uint64_t n=0;n<g.capture_end;++n) {
            const auto x=.37*std::sin(.13*n)+.29*std::cos(.041*n)+.07*std::sin(.007*n*n);
            const auto o=std::polar(1.,.731*n);
            pcm[n]={x*o.real(),x*o.imag(),o.real()*o.real(),o.imag()*o.imag(),o.real()*o.imag(),x*x};
        }
        selections.resize(outputs);
        for(std::size_t j=0;j<outputs;++j) {
            auto&s=selections[j];s.valid=true;
            for(auto n=g.capture_first;n<g.capture_end;++n)if(selected(first+j,n)) {
                if(!s.runs.empty()&&s.runs.back().end==n)++s.runs.back().end;else s.runs.push_back({n,n+1});
                ++s.count;
            }
        }
    }
    long double node(std::uint64_t n)const{return (static_cast<long double>(n)-g.physical_base)/g.node_spacing;}
    bool selected(std::size_t j,std::uint64_t n)const {
        if(n<g.capture_first||n>=g.capture_end)return false;
        const auto u=node(n)-j*g.step;
        // Three dwells, exact holes/crops and owner-independent zero-q rows.
        return u>=0&&u<g.template_cells&&((u<23)||(u>=31&&u<56)||(u>=65))&&n%37!=0;
    }
    events::Cell cell(std::size_t k)const {
        events::Cell c;c.chord.supported=k!=22&&k!=31&&k!=55&&k!=65;
        c.chord.observed=true;c.chord.residual={0,0};
        if((k>=23&&k<31)||(k>=56&&k<65)||k==7) {
            // Global private zero remains an observation when Selection says
            // so; in particular k7 tests E/N rather than inferred q support.
            c.chord.left={};c.chord.right={};
            if(k!=7)c.chord.observed=false;
            return c;
        }
        const auto value=[](double x,unsigned b){return Complex{.4*std::cos(.13*x+.3*b)+.05,.27*std::sin(.17*x+.4*b)-.06};};
        for(unsigned b=0;b<2;++b) {
            const auto gain=[](double x){return std::clamp(std::min(x/3.,(83-x)/4.),0.,1.);};
            c.chord.left[b]=gain(k)*value(k,b);c.chord.right[b]=gain(k+1)*value(k+1,b);
        }
        return c;
    }
    family::OriginalRow original(std::size_t j,std::uint64_t n)const {
        family::OriginalRow r;r.observed=selected(j,n);if(!r.observed)return r;
        const auto u=static_cast<double>(node(n)-j*g.step);
        r.q={Complex{.23*std::sin(.171*u),.39*std::cos(.19*u)},Complex{.31*std::cos(.213*u),.29*std::sin(.111*u)}};
        return r;
    }
    std::array<d::CorrelationFit,2> direct(std::size_t j)const {
        std::array<d::CorrelationFit,2> fits;
        for(auto n=g.capture_first;n<g.capture_end;++n)if(selected(j,n)) {
            const auto u=node(n)-j*g.step;const auto k=static_cast<std::size_t>(std::floor(u));
            const auto c=cell(k);auto q=original(j,n).q;
            if(c.chord.supported)for(unsigned b=0;b<2;++b) {
                const auto t=static_cast<double>(u-k);q[b]=(1-t)*c.chord.left[b]+t*c.chord.right[b];
            }
            for(unsigned b=0;b<2;++b)fits[b].add(pcm[n],q[b],1);
        }
        return fits;
    }
    auto cells()const{return [&](std::size_t k,long double,long double){return cell(k);};}
    auto visit()const{return [&](std::uint64_t first,std::uint64_t end,auto emit){for(auto n=first;n<end;++n)emit(n,pcm[n]);};}
    auto fallback()const{return [&](std::size_t j,std::uint64_t n){return original(j,n);};}
};
template<class T>void actual() {
    Fixture f(3,33);const auto budget=family::reservation<T>(f.g);
    const auto result=family::run<T>(f.g,budget,f.cells(),f.visit(),f.selections,f.fallback());
    const auto tolerance=std::is_same_v<T,float>?5e-5:2e-10;
    for(std::size_t j=0;j<f.g.outputs;++j) {
        check(result.valid[j],"native whole fit invalid");const auto expected=f.direct(f.g.origin_first+j);
        for(unsigned b=0;b<2;++b) {
            const auto&a=result.whole[j][b];const auto&e=expected[b];
            check(a.count==e.count&&a.count==f.selections[j].count,"exact observation count changed");
            close(a.energy,e.energy,2e-13,"raw observation energy changed");
            close(a.xc,e.xc,tolerance,"xc changed");close(a.xs,e.xs,tolerance,"xs changed");
            close(a.cc,e.cc,tolerance,"cc changed");close(a.ss,e.ss,tolerance,"ss changed");close(a.cs,e.cs,tolerance,"cs changed");
            close(a.score(),e.score(),tolerance*4,"point score changed");
        }
    }
    check(result.work.transforms.partitions>1&&result.work.transforms.inverse_ffts==16,"multi-fragment FFT not shared");
    check(result.work.mask_rows>0&&result.work.original_rows>0,"fringe corrections were not exercised");
    check(result.work.zero_cells>0&&result.work.selected_rows>0,"zero-q observations missing");
    check(result.work.peak_bytes<=budget,"workspace escaped reservation");
    // Shifted cohort is the SAME immutable global template, not a reset.
    auto sub=f.g;sub.origin_first+=9;sub.outputs=7;
    const auto selected=std::span<const observation::Selection>(f.selections).subspan(9,7);
    const auto split=family::run<T>(sub,family::reservation<T>(sub),f.cells(),f.visit(),selected,f.fallback());
    for(std::size_t j=0;j<7;++j)for(unsigned b=0;b<2;++b) {
        const auto&a=split.whole[j][b];const auto&e=result.whole[j+9][b];
        check(a.count==e.count,"global cohort count changed");close(a.xc,e.xc,tolerance,"global cohort xc changed");
        close(a.cc,e.cc,tolerance,"global cohort Gram changed");close(a.energy,e.energy,2e-13,"global cohort energy changed");
    }
}
template<class F>void fails(F&& f,const char* message){bool failed=false;try{f();}catch(const datapump::Error&){failed=true;}check(failed,message);}
int main(){try {
    actual<double>();actual<float>();Fixture f(0,5);const auto budget=family::reservation<double>(f.g);
    fails([&]{family::run<double>(f.g,1,f.cells(),f.visit(),f.selections,f.fallback());},"workspace limit ignored");
    std::stop_source source;source.request_stop();
    fails([&]{family::run<double>(f.g,budget,f.cells(),f.visit(),f.selections,f.fallback(),{},source.get_token());},"cancellation ignored");
    fails([&]{family::run<double>(f.g,budget,f.cells(),f.visit(),f.selections,f.fallback(),{0,0});},"corrective work limit dropped rows");
    auto wrong=f.selections;++wrong[0].count;
    fails([&]{family::run<double>(f.g,budget,f.cells(),f.visit(),wrong,f.fallback());},"bad selection count accepted");
    const auto missing=[&](std::uint64_t first,std::uint64_t end,auto emit){for(auto n=first;n<end;++n)if(n!=first)emit(n,f.pcm[n]);};
    fails([&]{family::run<double>(f.g,budget,f.cells(),missing,f.selections,f.fallback());},"missing PCM accepted");
    d::CorrelationFit singular{1,2,1,1,1,10,100};check(!family::usable(singular),"singular center became zero-score rejection");
    d::CorrelationFit overflow{1e100,0,1e150,1e150,0,1e78,100};
    check(!family::usable(overflow),"overflowing quadratic became finite score");
    std::cout<<"Default point family same-chord multi-dwell FFT passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
