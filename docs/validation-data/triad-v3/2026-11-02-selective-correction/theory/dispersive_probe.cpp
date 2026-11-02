// Read-only checks of the actual corrected FIR and finite Gaussian teacher.
#include "channel_perturbation.hpp"
#include "triad_gaussian_fading.hpp"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <utility>

int main() {
    using datapump::modem::detail::DispersiveSelectivePerturbation;
    constexpr unsigned input_count=24;
    DispersiveSelectivePerturbation stage(16,.7,3,.7,.25,.65,123);
    const auto total=input_count+stage.tail_samples(16,3);
    std::cout<<std::setprecision(17)<<"{\"filter\":{\"sample_rate\":16,\"center_hz\":0.7,\"spacing_hz\":3,\"doppler_hz\":0.7,\"slow\":0.25,\"fast\":0.65,\"input_count\":"<<input_count
        <<",\"tail_samples\":"<<stage.tail_samples(16,3)<<",\"initial_phases\":[";
    for(unsigned b=0;b<3;++b){if(b)std::cout<<',';std::cout<<'['<<stage.initial_phases[b][0]<<','<<stage.initial_phases[b][1]<<']';}
    std::cout<<"],\"output\":[";
    for(std::uint64_t n=0;n<total;++n){
        const std::complex<double> x=n<input_count?std::complex<double>{std::sin(.37*n)+.2*std::cos(.11*n),std::cos(.23*n)}:std::complex<double>{};
        const auto y=stage.process(x);if(n)std::cout<<',';std::cout<<'['<<y.real()<<','<<y.imag()<<']';
    }
    std::cout<<"]},\"kernels\":[";bool comma=false;
    for(const auto [width,fs]:{std::pair{.1,16.},std::pair{1.,16.},std::pair{10.,128.}}){
        if(comma)std::cout<<',';comma=true;
        datapump::triad::v3::detail::GaussianRayFading fading(width,1/fs,123);
        std::cout<<"{\"width_hz\":"<<width<<",\"sample_rate\":"<<fs<<",\"coefficients\":[";
        bool inner=false;for(double h:fading.coefficients()){if(inner)std::cout<<',';inner=true;std::cout<<h;}std::cout<<"]}";
    }
    std::cout<<"]}\n";
}
