#include "scheduled_polynomial_chip_moments.hpp"
#include "datapump/pattern_pulse.hpp"
#include <array>
#include <cfenv>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace d=datapump::modem::detail;
namespace p=d::scheduled_polynomial_chip;
namespace c=d::scheduled_acquisition_certificate;
namespace g=d::scheduled_cold_colored;
using Complex=std::complex<double>;using Wide=std::complex<long double>;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void contains(c::Interval interval,double value,const char* message) {
    check(interval.bounded() && interval.lo<=value && value<=interval.hi,message);
}
template<class F>void rejects(F run,const char* message) {
    bool caught=false;try{run();}catch(const datapump::Error&){caught=true;}check(caught,message);
}
struct Fixture {
    std::array<float,34> pcm{};
    std::array<Complex,34> oscillator{};
    std::array<std::array<Complex,2>,34> q{};
    Fixture() {
        const auto& basis=p::basis();
        for(std::size_t n=0;n<34;++n) {
            pcm[n]=static_cast<float>(.4*std::sin(.7*n)+.15*std::cos(.31*n));
            // Deliberately nonunit, asymmetric and independently reset halfway.
            oscillator[n]=n<17?Complex{.6+.01*n,.4-.013*n}:Complex{-.3+.021*n,.7+.004*n};
            const auto t=basis.t[n];
            q[n][0]=Complex{.2,-.1}+t*(Complex{.15,.08}+t*(Complex{-.07,.02}+t*Complex{.03,-.05}));
            q[n][1]=Complex{-.12,.21}+t*(Complex{.01,-.17}+t*(Complex{.09,.04}+t*Complex{-.06,.01}));
        }
    }
};
std::array<c::ReferenceErrors,2> exact_reference() {
    return {{{0,0,0,c::infinity,true},{0,0,0,c::infinity,true}}};
}
d::CorrelationFit original(const Fixture& f,unsigned bit) {
    d::CorrelationFit result;
    for(std::size_t n=0;n<34;++n) {
        const auto x=static_cast<double>(f.pcm[n]);const auto o=f.oscillator[n];
        const d::CorrelationProjection projection{x*o.real(),x*o.imag(),o.real()*o.real(),
            o.imag()*o.imag(),o.real()*o.imag(),x*x};
        result.add(projection,f.q[n][bit],1);
    }
    return result;
}
void fields(const g::FitIntervals& intervals,const d::CorrelationFit& value) {
    check(intervals.valid && intervals.count==34 && intervals.count_lower==34 && intervals.count_upper==34,"full cell count changed");
    contains(intervals.xc,value.xc,"Original xc escaped polynomial interval");
    contains(intervals.xs,value.xs,"Original xs escaped polynomial interval");
    contains(intervals.cc,value.cc,"Original cc escaped polynomial interval");
    contains(intervals.ss,value.ss,"Original ss escaped polynomial interval");
    contains(intervals.cs,value.cs,"Original cs escaped polynomial interval");
    contains(intervals.energy,value.energy,"Original energy escaped polynomial interval");
}
void exact_cubic_and_basis() {
    Fixture f;const auto source=p::build_source(f.q);const auto moments=p::build_moments(f.pcm,f.oscillator);
    const auto result=p::contract(source,moments,exact_reference());check(result.supported,"ordinary cubic unsupported");
    check(source.working_bytes()==sizeof(p::SourceCell),"SourceCell has hidden dynamic storage");
    check(source.residual_square[0]<1e-20 && source.residual_square[1]<1e-20,"cubic interpolation residual too large");
    check(std::abs(moments.trace[1].value)>1e-3,"odd carrier-norm moment was silently zeroed");
    for(unsigned bit=0;bit<2;++bit)fields(result.intervals[bit],original(f,bit));
    for(std::size_t n=0;n<34;++n)for(std::size_t i=0;i<=3;++i)for(std::size_t j=0;j<=3;++j) {
        const auto t=static_cast<long double>(p::basis().t[n]);long double power=1;
        for(std::size_t k=0;k<i+j;++k)power*=t;
        const auto interval=p::basis().powers[n][i+j];
        const auto allowance=8*std::numeric_limits<long double>::epsilon();
        check(power>=interval.lo-allowance && power<=interval.hi+allowance,"exact-real power escaped common basis table");
    }
}
void arbitrary_limited_q_and_original_errors() {
    Fixture f;
    for(std::size_t n=0;n<34;++n) {
        for(unsigned bit=0;bit<2;++bit) {
            Complex raw{};
            datapump::modem::pattern_pulse_each(10*34+n+.000715,34*32,34,[&](std::uint64_t chip,double weight) {
                const auto coefficient=std::polar(1.,(bit?-.71:.93)*chip);raw+=weight*coefficient;
            });
            const auto gain=n<7?double(n+1)/7:1.;
            f.q[n][bit]=gain*datapump::modem::pattern_limit_pcm(3.*raw)/3.;
        }
    }
    // Add noncubic finite perturbations; no pulse smoothness assumption may
    // disappear behind the fit or limiter acceptance.
    f.q[8][0]+=Complex{.07,-.04};f.q[19][1]+=Complex{-.05,.03};
    const auto source=p::build_source(f.q);const auto moments=p::build_moments(f.pcm,f.oscillator);
    check(source.residual_square[0]>1e-6 && source.residual_square[1]>1e-6,"noncubic residual was not retained");
    auto errors=exact_reference();for(auto& e:errors){e.dot=1e-4;e.gram=2e-4;e.energy=3e-4;}
    const auto result=p::contract(source,moments,errors);check(result.supported,"bounded noncubic source unsupported");
    for(unsigned bit=0;bit<2;++bit) {
        auto value=original(f,bit);fields(result.intervals[bit],value);
        value.xc+=.5*errors[bit].dot;value.xs-=.5*errors[bit].dot;
        value.cc+=.5*errors[bit].gram;value.ss-=.5*errors[bit].gram;value.cs+=.5*errors[bit].gram;
        value.energy+=.5*errors[bit].energy;fields(result.intervals[bit],value);
    }
    for(unsigned bit=0;bit<2;++bit) {
        long double residual=0,polynomial=0;
        for(std::size_t n=0;n<34;++n) {
            const auto t=static_cast<long double>(p::basis().t[n]);Wide q{};
            for(std::size_t k=4;k-->0;){const auto a=source.coefficients[bit][k];q=q*t+Wide{a.real(),a.imag()};}
            const auto actual=f.q[n][bit];residual+=std::norm(Wide{actual.real(),actual.imag()}-q);polynomial+=std::norm(q);
        }
        check(residual<=source.residual_square[bit]+1e-17L && polynomial<=source.polynomial_square[bit]+1e-17L,
            "source residual or polynomial norm certificate is inward");
    }
    errors[0].valid=false;check(!p::contract(source,moments,errors).supported,"unknown Original field contract accepted");
}
void zero_and_cancellation() {
    Fixture f;for(auto& row:f.q)row={};
    auto source=p::build_source(f.q);const auto moments=p::build_moments(f.pcm,f.oscillator);
    auto result=p::contract(source,moments,exact_reference());
    check(result.supported && result.fits[0].count==34 && result.fits[0].energy>0,"zero q removed physical noise/count");
    for(unsigned bit=0;bit<2;++bit) {
        fields(result.intervals[bit],original(f,bit));
        const auto projected=g::projection(result.intervals[bit]);check(projected.valid && projected.rank==0,"exact zero q did not retain rank0 proof");
    }
    // Tiny native q after cancellation, with unequal nonunit carrier magnitude.
    for(std::size_t n=0;n<34;++n){const auto t=p::basis().t[n];
        f.q[n][0]=Complex{1e-10,-2e-10}*(t*t*t-t);f.q[n][1]=Complex{-3e-10,1e-10}*(t*t-1);}
    source=p::build_source(f.q);result=p::contract(source,moments,exact_reference());
    check(result.supported,"small finite cancellation cell unsupported");
    for(unsigned bit=0;bit<2;++bit)fields(result.intervals[bit],original(f,bit));
    check(!result.intervals[0].zero_basis,"small polynomial silently claimed exact zero basis");
}
p::PhysicalKey key(std::uint64_t first) {return {1,2,3,4,first,5,1000000};}
void cache_identity_lru_and_release() {
    Fixture f;p::MomentCache cache(3,64*1024);
    check(cache.working_bytes()<=p::MomentCache::reservation(3),"cache reservation did not cover actual capacity");
    check(cache.capacity()==3,"cache capacity changed");
    cache.get_or_build(key(0),f.pcm,f.oscillator);cache.get_or_build(key(34),f.pcm,f.oscillator);cache.get_or_build(key(68),f.pcm,f.oscillator);
    check(cache.find(key(0))!=nullptr,"known physical moment missed");
    cache.insert(key(102),f.pcm,f.oscillator);
    check(cache.find(key(34))==nullptr && cache.find(key(68))!=nullptr,"O(1)LRU eviction chose wrong entry");
    check(cache.counters().builds==4 && cache.counters().evictions==1 && cache.counters().physical_sample_rows==136,
        "cache build/eviction/sample accounting incorrect");
    const auto before=cache.counters().builds;
    auto changed=key(0);changed.fence=7;check(!cache.find(changed),"different Original fence reused moment");
    changed=key(0);changed.screen_capture_end=1000001;check(!cache.find(changed),"different capture snapshot reused moment");
    changed=key(0);changed.frequency=9;check(!cache.find(changed),"different physical carrier reused moment");
    changed=key(0);changed.layout=9;check(!cache.find(changed),"different processing layout reused moment");
    check(cache.counters().builds==before,"miss lookup gathered/built physical input");
    const auto bytes=cache.working_bytes();cache.clear(true);
    check(cache.capacity()==0 && cache.working_bytes()<bytes && cache.counters().clears==1,"clear(release) retained cache allocations");
    check(cache.find(key(0))==nullptr,"released cache retained moment");
    rejects([&]{cache.insert(key(0),f.pcm,f.oscillator);},"disabled cache accepted allocation");
}
void hash_tombstones_and_cancellation() {
    Fixture f;p::MomentCache cache(127,256*1024);
    for(std::size_t query=0;query<1600;++query) {
        const auto address=(query*73)%257;const auto physical=key(address*34);
        for(std::size_t n=0;n<34;++n)f.pcm[n]=static_cast<float>(double((address+n)%17)/32);
        const auto& actual=cache.get_or_build(physical,f.pcm,f.oscillator);
        double energy=0;for(const auto x:f.pcm)energy+=double(x)*x;
        check(actual.energy.value==energy,"bounded hash/tombstone cache returned wrong physical window");
    }
    check(cache.counters().evictions>0 && cache.counters().rehashes>0,"hash tombstone rebuilding was not exercised");
    const auto builds=cache.counters().builds,evictions=cache.counters().evictions;
    std::stop_source stop;stop.request_stop();
    rejects([&]{cache.insert(key(99999),f.pcm,f.oscillator,stop.get_token());},"cancelled moment build published cache entry");
    check(cache.counters().builds==builds && cache.counters().evictions==evictions,"cancelled build mutated cache ownership");
    rejects([&]{(void)p::build_source(f.q,stop.get_token());},"source cancellation ignored");
    rejects([&]{(void)p::build_moments(f.pcm,f.oscillator,stop.get_token());},"physical cancellation ignored");
}
void unsupported_extents_environment_and_budget() {
    Fixture f;
    rejects([&]{(void)p::build_source(std::span<const std::array<Complex,2>>(f.q).first(33));},"partial source owner admitted");
    rejects([&]{(void)p::build_moments(std::span<const float>(f.pcm).first(33),f.oscillator);},"partial physical cell admitted");
    rejects([&]{p::MomentCache too_small(8192,1);},"short physical cache budget admitted");
    rejects([&]{p::MomentCache too_large(p::maximum_capacity+1);},"unbounded cache capacity admitted");
    p::MomentCache cache(1,64*1024);auto bad=key(0);bad.fence=0;
    rejects([&]{(void)cache.find(bad);},"missing physical provenance admitted");
    auto nan=f.q;nan[2][0]={std::numeric_limits<double>::quiet_NaN(),0};
    rejects([&]{(void)p::build_source(nan);},"nonfinite source admitted");
    const auto source=p::build_source(f.q);const auto moments=p::build_moments(f.pcm,f.oscillator);
    const auto rounding=std::fegetround();
    check(std::fesetround(FE_UPWARD)==0,"could not test unsupported rounding mode");
    const auto result=p::contract(source,moments,exact_reference());
    const auto restored=std::fesetround(rounding);
    check(restored==0 && !result.supported,"non-nearest arithmetic certified");
}
int main(){try {
    exact_cubic_and_basis();arbitrary_limited_q_and_original_errors();zero_and_cancellation();
    cache_identity_lru_and_release();hash_tombstones_and_cancellation();unsupported_extents_environment_and_budget();
    std::cout<<"polynomial C34 chip: six focused cases passed; SourceCell="<<sizeof(p::SourceCell)
        <<" Moments="<<sizeof(p::Moments)<<" cache8192="<<p::MomentCache::reservation(8192)<<'\n';
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
