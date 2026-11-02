#include "channel_perturbation.hpp"
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iostream>
#include <memory>
#include <numbers>
#include <vector>

using C=std::complex<double>;
using datapump::modem::detail::DispersiveSelectivePerturbation;
constexpr double pi=std::numbers::pi;
constexpr std::uint64_t seed_a=0x17a3b5c9ULL,seed_b=0xdecafbad12345678ULL;

struct Stats { C mean{};double amp=0,phase=0,amp_pair=0,phase_pair=0,complex_pair=0,ideal_error=0; };
static std::uint64_t mix(std::uint64_t x){x+=0x9e3779b97f4a7c15ULL;x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;x=(x^(x>>27))*0x94d049bb133111ebULL;return x^(x>>31);}
static double initial_phase(std::uint64_t seed,int band,int component){auto bits=mix(seed^mix(2*band+component+0x9072d5ULL));return 2*pi*(static_cast<double>(bits>>11)+.5)/9007199254740992.;}
static C ideal_transfer(double f,double spacing,int band){
    C h{};for(int m=0;m<3;++m)h+=std::polar(1./3,2*pi*(band-1)*m/3-2*pi*f*m/(3*spacing));return h;
}
static std::array<double,4> sidebands(const std::vector<C>& x,double fs,double start){
    std::array<double,4> a{};const std::array<double,4> fq={.005,-.005,.05,-.05};
    for(int j=0;j<4;++j){C z{};for(std::size_t k=0;k<x.size();++k)z+=x[k]*std::polar(1.,-2*pi*fq[j]*(start+k/fs));a[j]=2*std::abs(z)/x.size();}return a;
}
static Stats run(double fs,double spacing,double offset,double depth,std::uint64_t seed){
    const double measured=20000.,d=.05,slow=.005,fast=.05,bw=3*spacing;
    const double discard=std::ceil(DispersiveSelectivePerturbation::tail_samples(fs,spacing)/fs)+1000.;
    const double duration=discard+measured;
    const auto n=static_cast<std::uint64_t>(duration*fs),skip=static_cast<std::uint64_t>(discard*fs);
    std::array<std::vector<C>,3> gains;for(auto& g:gains)g.reserve(n-skip);
    auto make=[&](){return DispersiveSelectivePerturbation(fs,0,spacing,d,depth,depth,seed);};
    std::array<std::unique_ptr<DispersiveSelectivePerturbation>,3> one;
    for(auto& p:one)p=std::make_unique<DispersiveSelectivePerturbation>(make());
    for(int c=0;c<3;++c)for(std::uint64_t k=0;k<n;++k){double t=k/fs,f=offset+(c-1)*spacing;C x=std::polar(1.,2*pi*f*t),y=one[c]->process(x);if(k>=skip)gains[c].push_back(y/x);}
    auto simultaneous=make();double super_err=0,super_energy=0,ideal_err=0,ideal_energy=0;
    for(std::uint64_t k=0;k<n;++k){double t=k/fs;C xsum{};std::array<C,3> xs{};
        for(int c=0;c<3;++c){double f=offset+(c-1)*spacing;xs[c]=std::polar(1.,2*pi*f*t);xsum+=xs[c];}
        C actual=simultaneous.process(xsum);
        if(k>=skip){
            C isolated_sum{},ideal_sum{};
            for(int c=0;c<3;++c){double f=offset+(c-1)*spacing;C x=xs[c];
                isolated_sum+=gains[c][k-skip]*x;
                C gi=1.;for(int b=0;b<3;++b){double es=depth*std::sin(initial_phase(seed,b,0)+2*pi*slow*t);double ef=depth*std::sin(initial_phase(seed,b,1)+2*pi*fast*t);gi+=(es+ef)*ideal_transfer(f,spacing,b);}ideal_sum+=x*gi;
            }
            super_err+=std::norm(actual-isolated_sum);super_energy+=std::norm(actual);
            ideal_err+=std::norm(actual-ideal_sum);ideal_energy+=std::norm(ideal_sum);
        }
    }
    Stats out{};std::array<C,3> mean{};const auto count=gains[0].size();
    for(int c=0;c<3;++c){for(C z:gains[c])mean[c]+=z;mean[c]/=double(count);}
    double amp=0,phase=0,ap=0,pp=0,cp=0,ie=0;std::array<std::vector<C>,3> ideal_g;
    for(auto&v:ideal_g)v.reserve(count);
    for(std::size_t k=0;k<count;++k){double t=discard+k/fs;
        for(int c=0;c<3;++c){double f=offset+(c-1)*spacing;C gi=1.;for(int b=0;b<3;++b){double ex=depth*std::sin(initial_phase(seed,b,0)+2*pi*slow*t)+depth*std::sin(initial_phase(seed,b,1)+2*pi*fast*t);gi+=ex*ideal_transfer(f,spacing,b);}ideal_g[c].push_back(gi);ie+=std::norm(gains[c][k]-gi);}
        const C d01=gains[0][k]-gains[1][k],d12=gains[1][k]-gains[2][k];cp+=(std::norm(d01)+std::norm(d12))/2;
        ap+=(std::pow(std::abs(gains[0][k])-std::abs(gains[1][k]),2)+std::pow(std::abs(gains[1][k])-std::abs(gains[2][k]),2))/2;
        pp+=(std::pow(std::remainder(std::arg(gains[0][k]/gains[1][k]),2*pi),2)+std::pow(std::remainder(std::arg(gains[1][k]/gains[2][k]),2*pi),2))/2;
        for(int c=0;c<3;++c){amp+=std::pow(std::abs(gains[c][k])-std::abs(mean[c]),2);phase+=std::pow(std::remainder(std::arg(gains[c][k]/mean[c]),2*pi),2);}
    }
    out.amp=std::sqrt(amp/(3*count));out.phase=std::sqrt(phase/(3*count));out.amp_pair=std::sqrt(ap/count);out.phase_pair=std::sqrt(pp/count);out.complex_pair=std::sqrt(cp/count);out.ideal_error=std::sqrt(ie/(3*count));
    const auto sb=sidebands(gains[1],fs,discard),is=sidebands(ideal_g[1],fs,discard);
    std::cout<<"{\"model\":\"corrected-dispersive-v2\",\"fs_hz\":"<<fs<<",\"spacing_hz\":"<<spacing<<",\"bandwidth_hz\":"<<bw<<",\"offset_hz\":"<<offset<<",\"depth\":"<<depth<<",\"seed\":"<<seed<<",\"duration_s\":"<<duration<<",\"discard_s\":"<<discard<<",\"measured_seconds\":"<<measured<<",\"samples\":"<<n<<",\"lag_samples_1\":"<<fs/(3*spacing)<<",\"lag_samples_2\":"<<2*fs/(3*spacing)<<",\"lag_seconds_2\":"<<2/(3*spacing)<<",\"tail_samples\":"<<DispersiveSelectivePerturbation::tail_samples(fs,spacing)<<",\"individual_amplitude_rms\":"<<out.amp<<",\"individual_phase_rms_rad\":"<<out.phase<<",\"adjacent_complex_response_difference_rms\":"<<out.complex_pair<<",\"adjacent_amplitude_difference_rms\":"<<out.amp_pair<<",\"adjacent_phase_difference_rms_rad\":"<<out.phase_pair<<",\"finite_vs_ideal_complex_rms\":"<<out.ideal_error<<",\"middle_tone_sideband_amp_plusminus_0p005_0p05\":["<<sb[0]<<","<<sb[1]<<","<<sb[2]<<","<<sb[3]<<"],\"ideal_middle_tone_sideband_amp_plusminus_0p005_0p05\":["<<is[0]<<","<<is[1]<<","<<is[2]<<","<<is[3]<<"],\"simultaneous_vs_isolated_relative_error\":"<<std::sqrt(super_err/std::max(super_energy,1e-300))<<",\"simultaneous_vs_ideal_relative_error\":"<<std::sqrt(ideal_err/std::max(ideal_energy,1e-300))<<"}\n";
    return out;
}
int main(){
    for(auto seed:{seed_a,seed_b})for(double depth:{.25,.65})for(double fs:{.25,.5,1.,2.})for(double offset:{0.,.025,.05})run(fs,.00075,offset,depth,seed);
    for(double depth:{.25,.65})for(double offset:{0.,.025,.05})run(1.,.000075,offset,depth,seed_a);
    for(double spacing:{.0075,.075})for(double depth:{.25,.65})run(1.,spacing,0.,depth,seed_a);
}
