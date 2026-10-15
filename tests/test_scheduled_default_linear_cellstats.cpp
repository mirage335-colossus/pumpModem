#include "scheduled_default_linear_cellstats.hpp"
#include <array>
#include <cfenv>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace d=datapump::modem::detail;
namespace s=d::scheduled_default_linear_cellstats;
namespace g=d::scheduled_linear_grid;
namespace f=d::scheduled_linear_grid_fft;
namespace c=d::scheduled_acquisition_certificate;
using Complex=std::complex<double>;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Action> void rejects(Action&& action,const char* message) {
    bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}check(rejected,message);
}
void near(double a,double b,const char* message) {
    check(std::isfinite(a) && std::isfinite(b) && std::abs(a-b)<=2e-11*(1+std::abs(a)+std::abs(b)),message);
}
void compare(const d::CorrelationFit& a,const d::CorrelationFit& b) {
    near(a.xc,b.xc,"linear dot real mismatch");near(a.xs,b.xs,"linear dot imag mismatch");
    near(a.cc,b.cc,"linear Gram cc mismatch");near(a.ss,b.ss,"linear Gram ss mismatch");
    near(a.cs,b.cs,"linear Gram cs mismatch");near(a.energy,b.energy,"linear energy mismatch");
    check(a.count==b.count,"linear physical count mismatch");
}
d::CorrelationProjection projection(float pcm,Complex oscillator) {
    const auto x=static_cast<double>(pcm),a=oscillator.real(),b=oscillator.imag();
    return {x*a,x*b,a*a,b*b,a*b,x*x};
}
g::Cell cell(std::size_t index) {
    g::Cell result;result.observed=result.supported=true;
    const auto n=static_cast<double>(index);
    result.left={Complex{.25+n/256.,-.375+n/512.},Complex{-.5+n/1024.,.125+n/256.}};
    result.right={Complex{-.125+n/512.,.5-n/1024.},Complex{.375-n/256.,-.25+n/1024.}};
    // Infinite residual is intentional: this center helper supplies no
    // Original approximation/certificate and must not claim one from Cell.
    return result;
}
void exact_center_match_and_independent_row_oracle() {
    constexpr std::size_t cells=48;constexpr long double base=-.375L,spacing=3.75L;
    const auto budget=s::Moments::reservation(cells)+4096;
    s::Moments lean(cells,budget);g::Moments reference(cells,true,1<<20);
    std::array<std::array<d::CorrelationFit,2>,cells> row_oracle{};
    // Gaps, zero gain and observed=false all share one physical/grid identity.
    for(std::uint64_t p=0;p<175;++p) {
        const auto u=(static_cast<long double>(p)-base)/spacing;
        const auto at=static_cast<std::size_t>(std::floor(u));
        const auto t=static_cast<double>(u-at),a=1-t,b=t;
        const auto oscillator=std::polar(.8+.001*(p%7),.71*static_cast<double>(p)+(p/17)*.13);
        const auto pcm=static_cast<float>((static_cast<int>(p%17)-8)/16.);
        const auto pr=projection(pcm,oscillator);
        const auto gain=p%19==0?0.:(p%3==0?.75:1.);
        const bool observed=p%11!=0;
        lean.add(p,u,pr,gain,observed);reference.add(p,u,pr,gain,observed);
        if(!observed || gain<=0)continue;
        const auto chord=cell(at);
        for(unsigned bit=0;bit<2;++bit) {
            // Independent per-row qpoly, actual SAME physical carrier. This
            // uses direct basis products rather than transported Gram planes.
            const auto q=gain*(a*chord.left[bit]+b*chord.right[bit]);
            const auto z=q*oscillator;auto& fit=row_oracle[at][bit];
            fit.xc+=static_cast<double>(pcm)*z.real();fit.xs+=static_cast<double>(pcm)*z.imag();
            fit.cc+=z.real()*z.real();fit.ss+=z.imag()*z.imag();fit.cs+=z.real()*z.imag();
            fit.energy+=pr.energy;++fit.count;
        }
    }
    lean.finish();reference.finish();lean.finish();
    check(lean.working_bytes()<=budget,"lean workspace exceeds declared budget");
    for(std::size_t at=0;at<cells;++at) {
        for(unsigned plane=0;plane<8;++plane) {
            near(lean.rows()[at][plane].real(),reference.rows()[at][plane].real(),"lean moment real mismatch");
            near(lean.rows()[at][plane].imag(),reference.rows()[at][plane].imag(),"lean moment imag mismatch");
        }
        check(lean.energy()[at]==reference.energy()[at] && lean.count()[at]==reference.count()[at],"lean preparation changed energy/count");
        const auto stats=s::contract(lean.rows()[at],cell(at),static_cast<double>(lean.energy()[at]),lean.count()[at]);
        check(stats.valid,"finite linear cell unsupported");
        for(unsigned bit=0;bit<2;++bit)compare(stats.fits[bit],row_oracle[at][bit]);
    }
}
void restricted_physical_subrange_uses_its_own_moments() {
    s::Moments selected(1,s::Moments::reservation(1)+4096);
    std::array<d::CorrelationFit,2> expected{};const auto chord=cell(2);
    // Fractional cell aperture h=9.5 with only a strict physical subrange kept.
    for(std::uint64_t p=102;p<107;++p) {
        const auto u=(static_cast<long double>(p)-100.25L)/9.5L;
        const auto t=static_cast<double>(u),a=1-t,b=t;
        const auto oscillator=std::polar(.9,.53*static_cast<double>(p));
        const auto pr=projection(.375F,oscillator);selected.add(p,u,pr,1);
        for(unsigned bit=0;bit<2;++bit)expected[bit].add(pr,a*chord.left[bit]+b*chord.right[bit],1);
    }
    selected.finish();const auto stats=s::contract(selected.rows()[0],chord,static_cast<double>(selected.energy()[0]),selected.count()[0]);
    check(stats.valid && stats.fits[0].count==5,"restricted physical count changed");
    for(unsigned bit=0;bit<2;++bit)compare(stats.fits[bit],expected[bit]);
}
void gain_overshoot_private_zero_and_native_rank() {
    s::Moments moments(1,s::Moments::reservation(1)+4096);
    const auto gain=c::up(1.);check(gain<=d::scheduled_cold_colored::smooth_gain_upper(),"bad gain cap fixture");
    const auto pr=projection(.5F,{1,.25});moments.add(10,.25L,pr,gain);moments.finish();
    check(moments.rows()[0][0].real()==.75*(pr.xc*gain) &&
        moments.rows()[0][0].real()>.75*pr.xc,"gain overshoot was clamped");
    auto chord=cell(0);const auto singleton=s::contract(moments.rows()[0],chord,.25,1);
    check(singleton.valid && singleton.needs_refinement && singleton.rank[0]==s::NativeRank::refine,"singleton acquired rank2");
    chord.left=chord.right={};const auto zero=s::contract(moments.rows()[0],chord,.25,1);
    check(zero.valid && !zero.needs_refinement && zero.rank[0]==s::NativeRank::zero_fields &&
        zero.fits[0].count==1 && zero.fits[0].energy==.25,"private zero discarded physical noise/count");
    d::CorrelationFit rank_two{0,0,2,3,.25,1,4};
    check(s::native_rank(rank_two)==s::NativeRank::two,"full-rank diagnostic changed");
    rank_two.cs=3;check(s::native_rank(rank_two)==s::NativeRank::refine,"indefinite Gram acquired rank2");
    rank_two.cc=rank_two.ss=std::numeric_limits<double>::max();
    check(s::native_rank(rank_two)==s::NativeRank::refine,"overflowing determinant acquired rank2");
    rank_two={1.5e113,.5e113,1.25e150,.25e150,.25e150,1e77,2};
    check(s::native_rank(rank_two)==s::NativeRank::refine,"overflowing projection quadratic acquired rank2");
    f::InputRow indefinite{};indefinite[2]={0,4};indefinite[5]={2,0};
    chord.left=chord.right={Complex{1,0},Complex{1,0}};
    const auto raw=s::contract(indefinite,chord,1,3);
    check(raw.valid && raw.needs_refinement && raw.fits[0].cc==1 && raw.fits[0].ss==1 &&
        raw.fits[0].cs==2,"indefinite native Gram was repaired or accepted as rank2");
}
void finite_support_and_builder_contracts() {
    const f::InputRow empty{};auto chord=cell(0);
    check(s::contract(empty,chord,0,0).valid,"empty observed cell rejected");
    auto bad=empty;bad[0]={1,0};check(!s::contract(bad,chord,0,0).valid,"nonempty moments with zero count admitted");
    chord.observed=false;
    check(s::contract(bad,chord,0,0).valid,"explicit quiet support not excluded");
    check(!s::contract(bad,chord,1,1).valid,"quiet cell discarded supplied energy/count");
    chord.supported=false;check(!s::contract(empty,chord,0,0).valid,"unsupported cell treated as quiet");
    chord=cell(0);bad=empty;bad[6]={0,1};check(!s::contract(bad,chord,0,1).valid,"complex trace plane admitted");
    bad=empty;bad[0]={std::numeric_limits<double>::infinity(),0};check(!s::contract(bad,chord,0,1).valid,"nonfinite moment admitted");
    check(!s::contract(empty,chord,-1,1).valid,"negative energy admitted");
    check(!s::contract(empty,chord,0,s::maximum_exact_count+1).valid,"inexact count admitted");
    chord.left[0]={std::numeric_limits<double>::max(),1};
    check(!s::contract(empty,chord,0,1).valid,"overflowing endpoint products admitted");
    rejects([]{s::Moments m(0,1<<20);},"empty tile allocated");
    rejects([]{s::Moments m(2,1);},"underbudget tile allocated");
    const auto p=projection(.25F,{.5,.5});
    {s::Moments m(2,s::Moments::reservation(2)+4096);rejects([&]{(void)m.rows();},"unsealed rows exposed");
        m.add(10,.5L,p,1);rejects([&]{m.add(10,.75L,p,1);},"duplicate physical row admitted");}
    {s::Moments m(2,s::Moments::reservation(2)+4096);m.add(10,1.25L,p,1);
        rejects([&]{m.add(11,.75L,p,1);},"grid rewind admitted");}
    {s::Moments m(1,s::Moments::reservation(1)+4096);rejects([&]{m.add(1,1.L,p,1);},"outside tile admitted");
        rejects([&]{m.add(1,.5L,p,c::up(d::scheduled_cold_colored::smooth_gain_upper()));},"excess gain admitted");}
    {s::Moments m(1,s::Moments::reservation(1)+4096);auto invalid=p;invalid.cc=5;
        rejects([&]{m.add(1,.5L,invalid,1);},"unbounded physical projection admitted");}
    {s::Moments m(1,s::Moments::reservation(1)+4096);m.add(1,.25L,p,1,false);m.add(2,.5L,p,0);m.finish();
        check(m.count()[0]==0 && m.energy()[0]==0 && m.rows()[0]==empty,"missing/zero-gain rows contributed");
        rejects([&]{m.add(3,.75L,p,1);},"sealed moments mutated");}
    const auto mode=std::fegetround();check(std::fesetround(FE_DOWNWARD)==0,"cannot set rounding fixture");
    const auto rejected=!s::contract(empty,cell(0),0,0).valid;bool constructor_rejected=false;
    try{s::Moments m(1,1<<20);}catch(const std::exception&){constructor_rejected=true;}
    const auto restored=std::fesetround(mode);
    check(restored==0 && rejected && constructor_rejected,"nonnearest environment admitted");
}
int main() {
    if(!c::environment<double>()){std::cerr<<"qualified binary64 environment unavailable\n";return 77;}
    try {
        exact_center_match_and_independent_row_oracle();restricted_physical_subrange_uses_its_own_moments();
        gain_overshoot_private_zero_and_native_rank();finite_support_and_builder_contracts();
        std::cout<<"default linear cellstats: passed\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
