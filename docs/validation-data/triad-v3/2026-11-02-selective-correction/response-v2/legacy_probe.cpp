#include "channel_perturbation.hpp"
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iostream>
#include <memory>
#include <numbers>
#include <vector>

using C = std::complex<double>;
using datapump::modem::detail::FrequencySelectivePerturbation;
constexpr double pi=std::numbers::pi;

struct Stats { C mean{}; double rms=0,phase_rms=0,mag_rms=0; };

Stats stats(const std::vector<C>& v) {
    Stats s;
    for(auto z:v)s.mean+=z;
    s.mean/=static_cast<double>(v.size());
    double e=0,m=0,p=0;
    for(auto z:v) { e+=std::norm(z-s.mean);m+=std::pow(std::abs(z)-std::abs(s.mean),2);
        p+=std::pow(std::remainder(std::arg(z/s.mean),2*pi),2); }
    s.rms=std::sqrt(e/v.size());s.mag_rms=std::sqrt(m/v.size());s.phase_rms=std::sqrt(p/v.size());
    return s;
}

void run(double spacing,double offset,double slow,double fast) {
    const double fs=1., bw=3*spacing, d=.05, duration=24000., transient=5000.;
    FrequencySelectivePerturbation model(fs,0,bw,d,slow,fast,0x17a3b5c9ULL);
    std::array<std::unique_ptr<FrequencySelectivePerturbation>,3> isolated;
    for(auto& x:isolated)x=std::make_unique<FrequencySelectivePerturbation>(fs,0,bw,d,slow,fast,0x17a3b5c9ULL);
    std::array<std::vector<C>,3> gains;
    const auto n=static_cast<std::uint64_t>(duration*fs);
    std::vector<C> summed_output;summed_output.reserve(n);
    for(auto& v:gains)v.reserve(n-static_cast<std::uint64_t>(transient*fs));
    for(std::uint64_t k=0;k<n;++k) {
        const double t=static_cast<double>(k)/fs;
        C sum{};
        for(int i=0;i<3;++i)sum+=std::polar(1.,2*pi*(offset+(i-1)*spacing)*t);
        summed_output.push_back(model.process(sum));
    }
    // Run the three labelled tones independently through identical seeded banks.
    for(int i=0;i<3;++i) {
        FrequencySelectivePerturbation one(fs,0,bw,d,slow,fast,0x17a3b5c9ULL);
        for(std::uint64_t k=0;k<n;++k) {
            const double t=static_cast<double>(k)/fs, f=offset+(i-1)*spacing;
            const C x=std::polar(1.,2*pi*f*t),y=one.process(x);
            if(t>=transient)gains[i].push_back(y/x);
        }
    }
    std::array<Stats,3> s{};
    for(int i=0;i<3;++i)s[i]=stats(gains[i]);
    std::array<std::array<double,4>,3> sidebands{};
    const std::array<double,4> sf={.005,-.005,.05,-.05};
    for(int i=0;i<3;++i)for(int j=0;j<4;++j) {
        C z{};for(std::size_t k=0;k<gains[i].size();++k)
            z+=gains[i][k]*std::polar(1.,-2*pi*sf[j]*(transient+static_cast<double>(k)/fs));
        sidebands[i][j]=2*std::abs(z)/gains[i].size();
    }
    double pair_rms=0,phase_pair=0;
    for(std::size_t k=0;k<gains[0].size();++k) {
        const C d01=gains[0][k]-gains[1][k],d12=gains[1][k]-gains[2][k];
        pair_rms+=(std::norm(d01)+std::norm(d12))/2;
        phase_pair+=(std::pow(std::remainder(std::arg(gains[0][k]/gains[1][k]),2*pi),2)+
                     std::pow(std::remainder(std::arg(gains[1][k]/gains[2][k]),2*pi),2))/2;
    }
    pair_rms=std::sqrt(pair_rms/gains[0].size());phase_pair=std::sqrt(phase_pair/gains[0].size());
    double superposition_error=0,signal_energy=0;
    for(std::uint64_t k=0;k<n;++k) {
        const double t=static_cast<double>(k)/fs;C outsum{};
        for(int i=0;i<3;++i) {
            const double f=offset+(i-1)*spacing;const C x=std::polar(1.,2*pi*f*t);
            outsum+=isolated[i]->process(x);
        }
        superposition_error+=std::norm(outsum-summed_output[k]);
        signal_energy+=std::norm(outsum);
    }
    const double superposition_relative=std::sqrt(superposition_error/std::max(signal_energy,1e-300));
    std::cout<<"{\"spacing_hz\":"<<spacing<<",\"bw_hz\":"<<bw<<",\"offset_hz\":"<<offset
      <<",\"depth_slow\":"<<slow<<",\"depth_fast\":"<<fast<<",\"duration_s\":"<<duration
      <<",\"transient_discard_s\":"<<transient<<",\"n_measure\":"<<gains[0].size()
      <<",\"tone_stats\":[";
    for(int i=0;i<3;++i)std::cout<<(i?",":"")<<"{\"mean_re\":"<<s[i].mean.real()<<",\"mean_im\":"<<s[i].mean.imag()
      <<",\"rms_complex\":"<<s[i].rms<<",\"rms_magnitude\":"<<s[i].mag_rms<<",\"rms_phase_rad\":"<<s[i].phase_rms
      <<",\"sideband_amplitudes_at_plusminus_0p005_and_0p05\":["<<sidebands[i][0]<<","<<sidebands[i][1]<<","<<sidebands[i][2]<<","<<sidebands[i][3]<<"]}";
    std::cout<<"],\"adjacent_response_difference_rms\":"<<pair_rms<<",\"adjacent_phase_difference_rms_rad\":"<<phase_pair
      <<",\"superposition_test_samples\":"<<n<<",\"superposition_relative_error\":"<<superposition_relative<<"}\n";
}
int main() {
    for(auto depth:{.25,.65})for(auto spacing:{.00075,.0075,.075})for(auto offset:{-.05,-.025,0.,.025,.05})
        run(spacing,offset,depth,depth);
}
