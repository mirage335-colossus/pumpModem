// Read-only probe of the actual shared selective stage and Gaussian FIR.
#include "channel_perturbation.hpp"
#include "triad_gaussian_fading.hpp"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numbers>

int main() {
    using datapump::modem::detail::FrequencySelectivePerturbation;
    std::cout << std::setprecision(17) << "{\"filter\":{\"sample_rate\":32,\"bandwidth\":6,\"doppler\":0.5,\"slow\":0.25,\"fast\":0.65,\"seed\":123,\"initial_phases\":[";
    FrequencySelectivePerturbation stage(32,0,6,.5,.25,.65,123);
    for (unsigned b=0;b<3;++b) {
        if(b) std::cout << ',';
        std::cout << '[' << stage.initial_phases[b][0] << ',' << stage.initial_phases[b][1] << ']';
    }
    std::cout << "],\"output\":[";
    for (unsigned n=0;n<256;++n) {
        const std::complex<double> x{std::sin(.37*n)+.2*std::cos(.11*n),std::cos(.23*n)};
        const auto y=stage.process(x);
        if(n)std::cout << ',';
        std::cout << '[' << y.real() << ',' << y.imag() << ']';
    }
    std::cout << "]},\"kernels\":[";
    bool comma=false;
    for (const auto [width,fs]: {std::pair{.001,.25},std::pair{.01,2.},
            std::pair{.1,16.},std::pair{1.,16.},std::pair{10.,128.},std::pair{30.,512.}}) {
        if(comma)std::cout << ',';comma=true;
        datapump::triad::v3::detail::GaussianRayFading fading(width,1/fs,123);
        std::cout << "{\"width_hz\":" << width << ",\"sample_rate\":" << fs << ",\"coefficients\":[";
        bool inner=false;
        for (double h:fading.coefficients()) {if(inner)std::cout << ',';inner=true;std::cout << h;}
        std::cout << "]}";
    }
    std::cout << "]}\n";
}
