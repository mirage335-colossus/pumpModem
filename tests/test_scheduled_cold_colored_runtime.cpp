#include "scheduled_cold_colored_runtime.hpp"
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace d=datapump::modem::detail;
namespace c=d::scheduled_acquisition_certificate;
namespace g=d::scheduled_cold_colored;
namespace t=d::scheduled_acquisition_tile;
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
void contains(c::Interval v,double x,const char* m){check(v.lo<=x && x<=v.hi,m);}
int main(){try{
    constexpr std::size_t N=408,C=34;const std::array<std::size_t,3> starts{0,7,17};
    std::vector<float> pcm(N+starts.back());std::vector<std::complex<double>> oscillator(pcm.size());
    std::vector<std::array<std::complex<double>,2>> rows(N);std::vector<std::uint8_t> mask(N,1);
    std::vector<std::uint64_t> owners(N);std::vector<d::CorrelationProjection> prefix(pcm.size()+1);
    double energy=0;
    for(std::size_t i=0;i<pcm.size();++i){pcm[i]=i%17==0?1.f:static_cast<float>(std::ldexp(std::sin(.7*i),-30));
        oscillator[i]=std::polar(1.,.731*i);const double x=pcm[i],a=oscillator[i].real(),b=oscillator[i].imag();
        auto p=prefix[i];p.xc+=x*a;p.xs+=x*b;p.cc+=a*a;p.ss+=b*b;p.cs+=a*b;p.energy+=x*x;prefix[i+1]=p;
        energy+=x*x;}
    const auto union_energy=c::up(energy/c::down(1-c::gamma(pcm.size(),std::numeric_limits<double>::epsilon()/2)));
    for(std::size_t i=0;i<N;++i){owners[i]=i/C;const auto ramp=std::min(1.,std::pow(double(i+1)/90,3));
        rows[i]={ramp*std::polar(.7,.211*i),ramp*std::polar(.8,-.17*i)};
        if(i>=55 && i<69)mask[i]=0;if(i>=136 && i<170)rows[i]={};}
    g::RuntimeDirect runtime(starts.size(),C,union_energy,1e-6,2,512);
    for(std::size_t first=0;first<N;){const auto end=std::min(N,first+47),length=end-first+starts.back();
        runtime.add({std::span<const float>(pcm).subspan(first,length),std::span<const std::complex<double>>(oscillator).subspan(first,length),{},first},
            {std::span<const std::array<std::complex<double>,2>>(rows).subspan(first,end-first),
             std::span<const std::uint8_t>(mask).subspan(first,end-first),first},starts,
             std::span<const std::uint64_t>(owners).subspan(first,end-first));first=end;}
    const auto result=runtime.finish();check(runtime.sample_pairs()>0,"runtime did no direct work");
    for(std::size_t j=0;j<starts.size();++j){std::array<d::CorrelationChipEvidence,2> original{};std::array<d::CorrelationFit,2> whole{};
        for(std::size_t i=0;i<N;++i)if(mask[i]){const auto at=i+starts[j];const auto p=prefix[at+1]-prefix[at];
            for(unsigned bit=0;bit<2;++bit){d::CorrelationFit value;value.add(p,rows[i][bit],1);
                original[bit].add_fit(value,owners[i]);whole[bit].add(p,rows[i][bit],1);}}
        for(unsigned bit=0;bit<2;++bit){const auto tail=d::CorrelationChipEvidence::projection(original[bit].cell);
            const auto& bound=result[j][bit];check(bound.valid && bound.nested && bound.owner_set_certified,"runtime prefix/ramp rank proof unknown");
            check(bound.count==whole[bit].count && bound.rank==original[bit].projected_rank+tail.second,"runtime changed original count/rank");
            contains(bound.projected,original[bit].projected_energy+tail.first,"original prefix-difference projected energy escaped");}}
    std::cout<<"scheduled cold colored runtime checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
