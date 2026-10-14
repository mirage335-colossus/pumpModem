#include "scheduled_cold_colored_proxy_runtime.hpp"
#include <iostream>
#include <stdexcept>
namespace d=datapump::modem::detail;
namespace c=d::scheduled_acquisition_certificate;
namespace g=d::scheduled_cold_colored;
namespace t=d::scheduled_acquisition_tile;
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
void contains(c::Interval v,double x,const char* m){check(v.lo<=x && x<=v.hi,m);}
int main(){try{
    constexpr std::size_t N=408,C=34;constexpr long double beta=.000716L,delta=.000268L;
    check(!g::fractional_membership_margin(34.L,delta),"integer chip edge admitted");
    check(!g::fractional_membership_margin(34.L+0x1p-12L,0x1p-12L),"nonstrict margin admitted");
    check(!g::fractional_membership_margin(34.L+.0001L,delta),"clock displacement exceeds margin");
    // Reproduce the executed piece association, not only a source-rate formula.
    // Every active row has the fixture's fractional lattice. Across the clock
    // family, check source ownership/window and integer useful-mask boundaries.
    for(const long double rate:{1.L-3e-10L,1.L,1.L+3e-10L})for(std::size_t n=0;n<87040;++n) {
        const auto native=static_cast<long double>(n)+beta;
        check(g::fractional_membership_margin(native,delta),"fixture fractional margin unknown");
        const auto piece=n/47*47;
        const auto original=(static_cast<long double>(piece)+beta+(rate-1)*800000.L)+(n-piece)*rate;
        check(std::abs(original-native)<delta,"executed piece escaped displacement");
        check(std::floor(original/C)==std::floor(native/C),"fractional clock changed chip owner");
        check((original>=0 && original<87040)==(native>=0 && native<87040),"fractional clock changed source window");
        const auto within_native=std::fmod(native,2448.L),within_original=std::fmod(original,2448.L);
        check((within_native>=123 && within_native<2325)==(within_original>=123 && within_original<2325),"fractional clock changed useful mask");
    }
    const std::array<std::size_t,3> starts{0,7,17};
    std::vector<float> pcm(N+starts.back());std::vector<std::complex<double>> oscillator(pcm.size()),actual_o(pcm.size());
    std::vector<std::array<std::complex<double>,2>> rows(N),actual_q(N);
    std::vector<std::uint8_t> mask(N,1);std::vector<g::ProxyRow> geometry(N);
    std::vector<d::CorrelationProjection> prefix(pcm.size()+1);double energy=0;
    for(std::size_t i=0;i<pcm.size();++i){pcm[i]=i%17==0?1.f:static_cast<float>(std::ldexp(std::sin(.7*i),-30));
        oscillator[i]=std::polar(1.,.731*i);actual_o[i]=oscillator[i]*std::complex<double>(1,2e-7);
        const double x=pcm[i],a=actual_o[i].real(),b=actual_o[i].imag();auto p=prefix[i];
        p.xc+=x*a;p.xs+=x*b;p.cc+=a*a;p.ss+=b*b;p.cs+=a*b;p.energy+=x*x;prefix[i+1]=p;energy+=x*x;}
    const auto union_energy=c::up(energy/c::down(1-c::gamma(pcm.size(),std::numeric_limits<double>::epsilon()/2)));
    for(std::size_t i=0;i<N;++i){const auto ramp=std::min(1.,std::pow(double(i+1)/180,5));
        rows[i]={ramp*std::polar(.7,.211*i),ramp*std::polar(.8,-.17*i)};
        for(unsigned bit=0;bit<2;++bit)actual_q[i][bit]=rows[i][bit]*std::complex<double>(1,1e-6*std::sin(.11*i));
        geometry[i]={i/C,0,c::mul(2e-6,std::max(c::norm(rows[i][0]),c::norm(rows[i][1])))};
        if(i>=55 && i<69){mask[i]=0;geometry[i]={};}}
    g::RuntimeProxy runtime(starts.size(),C,union_energy,1e-6,2,512,0);
    g::RuntimeProxy bounded(starts.size(),C,union_energy,1e-6,2,512,0,true);
    for(std::size_t first=0;first<N;){const auto end=std::min(N,first+47),length=end-first+starts.back();
        runtime.add({std::span<const float>(pcm).subspan(first,length),std::span<const std::complex<double>>(oscillator).subspan(first,length),{},first},
            {std::span<const std::array<std::complex<double>,2>>(rows).subspan(first,end-first),std::span<const std::uint8_t>(mask).subspan(first,end-first),first},
            starts,std::span<const g::ProxyRow>(geometry).subspan(first,end-first));
        bounded.add({std::span<const float>(pcm).subspan(first,length),std::span<const std::complex<double>>(oscillator).subspan(first,length),{},first},
            {std::span<const std::array<std::complex<double>,2>>(rows).subspan(first,end-first),std::span<const std::uint8_t>(mask).subspan(first,end-first),first},
            starts,std::span<const g::ProxyRow>(geometry).subspan(first,end-first));first=end;}
    const auto result=runtime.finish(), fast=bounded.finish();check(runtime.sample_pairs()>0,"proxy runtime did no direct work");
    for(std::size_t j=0;j<starts.size();++j){std::array<d::CorrelationChipEvidence,2> original{};std::array<d::CorrelationFit,2> whole{};
        for(std::size_t i=0;i<N;++i)if(mask[i]){const auto at=i+starts[j],owner=i/C;const auto p=prefix[at+1]-prefix[at];
            for(unsigned bit=0;bit<2;++bit){d::CorrelationFit value;value.add(p,actual_q[i][bit],1);original[bit].add_fit(value,owner);whole[bit].add(p,actual_q[i][bit],1);}}
        for(unsigned bit=0;bit<2;++bit){const auto tail=d::CorrelationChipEvidence::projection(original[bit].cell);const auto& bound=result[j][bit];
            check(bound.valid && bound.nested && bound.owner_set_certified,"fractional proxy/ramp rank proof unknown");
            check(bound.count==whole[bit].count && bound.rank==original[bit].projected_rank+tail.second,"fractional proxy changed original count/rank");
            contains(bound.projected,original[bit].projected_energy+tail.first,"original proxy/carrier prefix energy escaped");
            const auto& optimized=fast[j][bit];
            check(optimized.valid && optimized.nested && optimized.owner_set_certified && optimized.count==bound.count && optimized.rank==bound.rank,
                "bounded error planes changed original count/rank or lost a valid enclosure");
            contains(optimized.projected,original[bit].projected_energy+tail.first,"bounded error plane lost original projected energy");}}
    bool repeated=false;try{runtime.finish();}catch(const datapump::Error&){repeated=true;}check(repeated,"proxy last cell retained twice");
    std::cout<<"scheduled cold colored fractional proxy checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
