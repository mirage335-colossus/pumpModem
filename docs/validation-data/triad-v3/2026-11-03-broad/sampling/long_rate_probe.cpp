#include "channel_perturbation.hpp"
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numbers>

// Adapted from the accepted selective-correction response-v2 probe. This calls
// the actual continuous sampled FIR, including its initial zero history. The
// independent reference uses ideal physical delays, not sampled coefficients.
using C=std::complex<double>;
using Filter=datapump::modem::detail::DispersiveSelectivePerturbation;
constexpr double pi=std::numbers::pi, tau=2*pi, depth=.65;
constexpr std::uint64_t seed=0x17a3b5c9ULL;
static std::uint64_t mix(std::uint64_t x){
    x+=0x9e3779b97f4a7c15ULL;x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;
    x=(x^(x>>27))*0x94d049bb133111ebULL;return x^(x>>31);
}
static double initial_phase(int band,int component){
    const auto bits=mix(seed^mix(2*band+component+0x9072d5ULL));
    return tau*(static_cast<double>(bits>>11)+.5)/9007199254740992.;
}
static C ideal_transfer(double f,double spacing,int band){
    C h{};for(int m=0;m<3;++m)
        h+=std::polar(1./3,tau*(band-1)*m/3-tau*f*m/(3*spacing));
    return h;
}
static void array3(const std::array<double,3>& a){
    std::cout<<'['<<a[0]<<','<<a[1]<<','<<a[2]<<']';
}
static void run(double fs,double rate,double doppler,double cfo,int ray){
    const double spacing=1.25*rate/6,offset=cfo+ray*doppler;
    const double slow=.1*doppler,fast=doppler;
    const auto tail=Filter::tail_samples(fs,spacing);
    // Tail plus 1,000 seconds is discarded; the retained observation contains
    // exactly 100 slow / 1,000 fast modulation cycles at every sample rate.
    const auto skip=tail+static_cast<std::uint64_t>(1000*fs);
    const auto count=static_cast<std::uint64_t>(std::llround(100/slow*fs));
    const auto total=skip+count;
    std::array<std::unique_ptr<Filter>,3> isolated;
    for(auto& p:isolated)p=std::make_unique<Filter>(fs,0,spacing,doppler,depth,depth,seed);
    Filter combined(fs,0,spacing,doppler,depth,depth,seed);
    std::array<std::array<C,3>,3> transfer{};
    std::array<std::array<double,2>,3> sine{},cosine{};
    for(int b=0;b<3;++b)for(int j=0;j<2;++j){
        sine[b][j]=std::sin(initial_phase(b,j));cosine[b][j]=std::cos(initial_phase(b,j));
    }
    for(int c=0;c<3;++c)for(int b=0;b<3;++b)
        transfer[c][b]=ideal_transfer(offset+(c-1)*spacing,spacing,b);
    std::array<double,3> complex_error{},amplitude_error{},phase_error{},phase_count{},
        amplitude_rms{},phase_rms{},energy{},ideal_energy{};
    std::array<C,3> mean{};
    std::array<std::array<C,4>,3> sideband{},ideal_sideband{};
    double super_error=0,super_energy=0,sum_error=0,sum_ideal_energy=0,
        adjacent_complex=0,adjacent_amplitude=0,adjacent_phase=0,adjacent_error=0;
    for(std::uint64_t k=0;k<total;++k){
        const double t=k/fs;
        std::array<C,3> x{},actual{},ideal{};
        C input_sum{},isolated_sum{};
        for(int c=0;c<3;++c){
            x[c]=std::polar(1.,tau*(offset+(c-1)*spacing)*t);
            const C y=isolated[c]->process(x[c]);actual[c]=y/x[c];
            input_sum+=x[c];isolated_sum+=y;
        }
        const C actual_sum=combined.process(input_sum);
        if(k<skip)continue;
        const C es=std::polar(1.,tau*slow*t),ef=std::polar(1.,tau*fast*t);
        const std::array<C,4> demod={std::conj(es),es,std::conj(ef),ef};
        std::array<double,3> excursion{};
        for(int b=0;b<3;++b)
            excursion[b]=depth*(sine[b][0]*es.real()+cosine[b][0]*es.imag()
                +sine[b][1]*ef.real()+cosine[b][1]*ef.imag());
        C ideal_sum{};
        for(int c=0;c<3;++c){
            ideal[c]=1.;for(int b=0;b<3;++b)ideal[c]+=excursion[b]*transfer[c][b];
            mean[c]+=actual[c];
            complex_error[c]+=std::norm(actual[c]-ideal[c]);
            amplitude_error[c]+=std::pow(std::abs(actual[c])-std::abs(ideal[c]),2);
            if(std::abs(ideal[c])>1e-8 && std::abs(actual[c])>1e-8){
                phase_error[c]+=std::pow(std::remainder(std::arg(actual[c]/ideal[c]),tau),2);
                phase_count[c]+=1;
            }
            amplitude_rms[c]+=std::pow(std::abs(actual[c])-1.,2);
            phase_rms[c]+=std::pow(std::arg(actual[c]),2);
            energy[c]+=std::norm(actual[c]);ideal_energy[c]+=std::norm(ideal[c]);
            ideal_sum+=x[c]*ideal[c];
            for(int j=0;j<4;++j){sideband[c][j]+=actual[c]*demod[j];ideal_sideband[c][j]+=ideal[c]*demod[j];}
        }
        for(int c=0;c<2;++c){
            const C difference=actual[c]-actual[c+1];
            adjacent_complex+=std::norm(difference)/2;
            adjacent_amplitude+=std::pow(std::abs(actual[c])-std::abs(actual[c+1]),2)/2;
            adjacent_phase+=std::pow(std::remainder(std::arg(actual[c]/actual[c+1]),tau),2)/2;
            adjacent_error+=std::norm(difference-(ideal[c]-ideal[c+1]))/2;
        }
        super_error+=std::norm(actual_sum-isolated_sum);super_energy+=std::norm(actual_sum);
        sum_error+=std::norm(actual_sum-ideal_sum);sum_ideal_energy+=std::norm(ideal_sum);
    }
    std::array<double,3> amp{},phase{},ce{},ae{},pe{},edb{},dc{};
    for(int c=0;c<3;++c){
        amp[c]=std::sqrt(amplitude_rms[c]/count);phase[c]=std::sqrt(phase_rms[c]/count);
        ce[c]=std::sqrt(complex_error[c]/count);ae[c]=std::sqrt(amplitude_error[c]/count);
        pe[c]=std::sqrt(phase_error[c]/phase_count[c]);edb[c]=10*std::log10(energy[c]/ideal_energy[c]);
        dc[c]=std::abs(mean[c]/double(count)-C(1.));
    }
    std::cout<<std::setprecision(17)<<"{\"rate_hz\":"<<rate<<",\"spacing_hz\":"<<spacing
        <<",\"doppler_hz\":"<<doppler<<",\"cfo_hz\":"<<cfo<<",\"ray\":"<<ray
        <<",\"tone_triplet_offset_hz\":"<<offset<<",\"fs_hz\":"<<fs<<",\"seed\":"<<seed
        <<",\"depth\":"<<depth<<",\"tail_samples\":"<<tail<<",\"discard_samples\":"<<skip
        <<",\"measured_samples\":"<<count<<",\"measured_seconds\":"<<count/fs
        <<",\"slow_cycles\":100,\"fast_cycles\":1000,\"phase_error_min_amplitude\":1e-8"
        <<",\"individual_amplitude_rms\":";array3(amp);
    std::cout<<",\"individual_phase_rms_rad\":";array3(phase);
    std::cout<<",\"finite_vs_ideal_complex_rms\":";array3(ce);
    std::cout<<",\"finite_vs_ideal_amplitude_rms\":";array3(ae);
    std::cout<<",\"finite_vs_ideal_phase_rms_rad\":";array3(pe);
    std::cout<<",\"individual_energy_error_db\":";array3(edb);
    std::cout<<",\"dc_response_error\":";array3(dc);
    std::cout<<",\"adjacent_complex_response_difference_rms\":"<<std::sqrt(adjacent_complex/count)
        <<",\"adjacent_amplitude_difference_rms\":"<<std::sqrt(adjacent_amplitude/count)
        <<",\"adjacent_phase_difference_rms_rad\":"<<std::sqrt(adjacent_phase/count)
        <<",\"finite_vs_ideal_adjacent_difference_rms\":"<<std::sqrt(adjacent_error/count)
        <<",\"simultaneous_vs_isolated_relative_error\":"<<std::sqrt(super_error/super_energy)
        <<",\"simultaneous_vs_ideal_relative_error\":"<<std::sqrt(sum_error/sum_ideal_energy)
        <<",\"simultaneous_energy_error_db\":"<<10*std::log10(super_energy/sum_ideal_energy)
        <<",\"sideband_frequencies_hz\":["<<slow<<','<<-slow<<','<<fast<<','<<-fast<<']'
        <<",\"sideband_amplitude_actual\":[";
    for(int c=0;c<3;++c){if(c)std::cout<<',';std::cout<<'[';for(int j=0;j<4;++j){if(j)std::cout<<',';std::cout<<2*std::abs(sideband[c][j])/count;}std::cout<<']';}
    std::cout<<"],\"sideband_amplitude_ideal\":[";
    for(int c=0;c<3;++c){if(c)std::cout<<',';std::cout<<'[';for(int j=0;j<4;++j){if(j)std::cout<<',';std::cout<<2*std::abs(ideal_sideband[c][j])/count;}std::cout<<']';}
    std::cout<<"]}\n";
}
int main(){
    for(double rate:{.001,.01})for(double doppler:{.001,.05}){
        const double base_fs=doppler==.001?.25:.5;
        for(double multiple:{1.,2.,4.})for(double cfo:{-.00005,.00005})for(int ray:{0,1})
            run(base_fs*multiple,rate,doppler,cfo,ray);
    }
}
