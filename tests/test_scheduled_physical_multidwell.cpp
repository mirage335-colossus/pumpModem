#include "../src/scheduled_parallel_preparation.hpp"
#include "../src/scheduled_moment_synthesis.hpp"
#include <array>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>

namespace d=datapump::modem::detail;
namespace t=d::scheduled_acquisition_tile;
using C=std::complex<double>;
#define CHECK(x) do{if(!(x))throw std::runtime_error("check failed: " #x);}while(false)
void env(const char* text){
#ifdef _WIN32
    CHECK(_putenv_s("DATAPUMP_SCHED_LAZY_FRAGMENT",text?text:"")==0);
#else
    CHECK(text?setenv("DATAPUMP_SCHED_LAZY_FRAGMENT",text,1)==0:unsetenv("DATAPUMP_SCHED_LAZY_FRAGMENT")==0);
#endif
}
struct RestoreEnv {std::optional<std::string> old;RestoreEnv(){if(auto p=std::getenv("DATAPUMP_SCHED_LAZY_FRAGMENT"))old=p;}
    ~RestoreEnv(){if(old)env(old->c_str());else env(nullptr);}};
bool near(double a,double b,double tolerance){return std::abs(a-b)<=tolerance*(1+std::max(std::abs(a),std::abs(b)));}
void compare(const d::CorrelationFit& a,const d::CorrelationFit& b,double tolerance){
    CHECK(a.count==b.count);CHECK(near(a.energy,b.energy,1e-11));CHECK(near(a.xc,b.xc,tolerance));
    CHECK(near(a.xs,b.xs,tolerance));CHECK(near(a.cc,b.cc,tolerance));CHECK(near(a.ss,b.ss,tolerance));CHECK(near(a.cs,b.cs,tolerance));
}
void selectors(){RestoreEnv restore;env(nullptr);
    CHECK(d::scheduled_parallel_preparation::shape(8192).maximum_fragment==2048);
    for(const auto n:std::array<std::size_t,4>{65536,131072,262144,524288}){const auto text=std::to_string(n);env(text.c_str());
        const auto shape=d::scheduled_parallel_preparation::shape(8192);CHECK(shape.maximum_fragment==n);
        CHECK(shape.transform_size()==2*n);CHECK(shape.transform_size()<=1048576);
        CHECK(d::scheduled_parallel_preparation::scratch_bytes(8192,1,n)>=48ULL*shape.transform_size()*sizeof(C));}
    env("253952");CHECK(d::scheduled_parallel_preparation::shape(8192).transform_size()==262144);
    env("1048576");bool failed=false;try{(void)d::scheduled_parallel_preparation::shape(8192);}catch(const datapump::Error&){failed=true;}CHECK(failed);
    CHECK((t::Shape{8192,524288}.transform_size()==1048576));
}
// One bounded physical template spans>26 nominal2448sample dwells. Source
// ownership is paused across every two-null-hop gap, but carrier time is not.
// A partial final fragment, mask holes and observed zero rows are retained.
template<class T>void physical_runs(bool native_accumulation=false){
    constexpr std::size_t L=65536,N=L-17,span=33,hop=2448;const t::Shape shape{span,L};
    const std::array<std::size_t,3> starts{0,7,32};constexpr std::size_t budget=128*1024*1024;
    std::vector<float> pcm(L+span-1);std::vector<C> oscillator(pcm.size());std::vector<std::uint8_t> observed(pcm.size(),1),mask(N);
    std::vector<std::array<C,2>> rows(N);const auto tau=2*std::numbers::pi_v<long double>;
    std::size_t source=0,runs=0;bool active=false;
    for(std::size_t i=0;i<pcm.size();++i){pcm[i]=static_cast<float>(.3*std::sin(.373*i)+.13*std::cos(.017*i));
        // Capture-date carrier keeps advancing through all nulls. An original
        // recurrence norm deviation is intentionally retained in real Gram.
        oscillator[i]=std::polar(1.+1e-12*i,static_cast<double>(std::remainder((static_cast<long double>(i)+(1ULL<<50))*tau*1500/6120,tau)));
        if(i%97==0)observed[i]=0;
        if(i>=N)continue;mask[i]=(i/hop)%3==0;if(mask[i]&&!active)++runs;active=mask[i];
        if(!mask[i])continue;const auto edge=i%hop;const double gain=edge<16?std::pow(std::sin(std::numbers::pi*(edge+.5)/32),2):
            edge>=hop-16?std::pow(std::sin(std::numbers::pi*(hop-edge-.5)/32),2):1.;
        for(unsigned bit=0;bit<2;++bit){auto q=std::polar(.2+.17*std::abs(std::sin((bit?.271:.143)*source)),(bit?.831:.577)*source);
            rows[i][bit]=gain*q;}if(i%113==0)rows[i]={C{},C{}};++source;
    }
    CHECK(runs>8);CHECK(source>10000);CHECK(source<N/2);
    t::Plan<T> plan(shape);CHECK(plan.size==131072);CHECK(plan.native.maximum==plan.size);
    t::PreparedInput<T> input(plan,{pcm,oscillator,observed,0},budget);
    t::PreparedFragment<T> fragment(plan,{rows,mask,0},budget);CHECK(fragment.runs.size()==runs);
    CHECK(input.spectra[0].size()==plan.size);CHECK(fragment.spectra[5].size()==plan.size);
    t::Accumulator<T> accumulator(plan,0,starts,budget,{},false,native_accumulation);accumulator.add(input,fragment);const auto result=accumulator.finish();
    CHECK(result.size()==starts.size());for(const auto& value:result)CHECK(value.certificate.valid);CHECK(accumulator.work().fragments==1);CHECK(accumulator.work().prefix_queries==runs*starts.size());
    CHECK(plan.working_bytes()+input.working_bytes()+fragment.working_bytes()+accumulator.working_bytes()<budget);
    for(std::size_t j=0;j<starts.size();++j)for(unsigned bit=0;bit<2;++bit){d::CorrelationFit expected;
        for(std::size_t i=0;i<N;++i){const auto at=starts[j]+i;if(!mask[i]||!observed[at])continue;const double x=pcm[at];const auto o=oscillator[at];
            expected.add({x*o.real(),x*o.imag(),o.real()*o.real(),o.imag()*o.imag(),o.real()*o.imag(),x*x},rows[i][bit],1);}
        CHECK(expected.count>10000);compare(result[j].fits[bit],expected,std::is_same_v<T,float>?3e-4:2e-9);
        CHECK(result[j].certificate.energy_lower<=expected.energy+1e-8);CHECK(result[j].certificate.energy_upper>=expected.energy-1e-8);}
}
// Exercise the independent synthesis extent, not just native FFT allocation.
// Per-bin ascending products/Neumaier order remains unchanged beyondold16k.
void moment_extent(){namespace m=d::scheduled_moment_synthesis;constexpr std::size_t N=65537;
    std::vector<std::complex<float>> a(N),b(N),output(N);std::vector<C> before(N);
    for(std::size_t i=0;i<N;++i){a[i]={float(.3*std::sin(.137*i)),float(.2*std::cos(.173*i))};b[i]={float(.1*std::cos(.211*i)),float(.2*std::sin(.193*i))};}
    const std::array<std::span<const std::complex<float>>,2> planes{a,b};const std::array<std::complex<float>,2> coefficients{{{1,0},{0,1}}};
    const auto execution=m::synthesize<float>(planes,coefficients,output,before);CHECK(execution.arithmetic_eligible);
    for(std::size_t i=0;i<N;++i){const auto expected=std::complex<float>{a[i].real()-b[i].imag(),a[i].imag()+b[i].real()};
        CHECK(output[i]==expected);CHECK(near(before[i].real(),double(a[i].real())-double(b[i].imag()),1e-14));
        CHECK(near(before[i].imag(),double(a[i].imag())+double(b[i].real()),1e-14));}
}
int main(){try{selectors();physical_runs<float>();physical_runs<float>(true);physical_runs<double>();moment_extent();
    std::cout<<"bounded physical multi-dwell native FFT tests PASS\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
