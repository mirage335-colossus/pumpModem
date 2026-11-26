// Finite mathematical probe of the legacy model at 8c98439406a124639ccce58edcff8588f6f9a35b.
// All-pass recurrences and arithmetic order extracted from channel_perturbation.hpp;
// SHA-256 710da481e4613abed3e771eccf63659157e6bf2bc478bfea06c5032324cf044c.
// This is a fixed diagnostic, not an operating channel or receiver.
#include "historical_math.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numbers>

int main() {
    using namespace historical_math;
    constexpr double sample_rate=32,bandwidth_hz=6,doppler_hz=.5;
    const std::array<double,2> depth{.25,.65},rates{.1*doppler_hz,doppler_hz};
    const auto phases=initial_phases(123);
    std::array<std::array<Complex,2>,3> phase{};
    std::array<Complex,2> step{},rotation{},previous_input{},previous_output{};
    const auto cutoff=std::min(bandwidth_hz/6,sample_rate*.24);
    const auto k=std::tan(std::numbers::pi*cutoff/sample_rate);
    const auto a=(k-1)/(k+1);
    for(std::size_t i=0;i<2;++i) {
        rotation[i]=std::polar(1.,tau*(i?bandwidth_hz/3:-bandwidth_hz/3)/sample_rate);
        step[i]=std::polar(1.,tau*rates[i]/sample_rate);
    }
    for(std::size_t b=0;b<3;++b)for(std::size_t c=0;c<2;++c)
        phase[b][c]=std::polar(1.,phases[b][c]);
    std::cout << std::setprecision(17) << "{\"filter\":{\"sample_rate\":32,\"bandwidth\":6,\"doppler\":0.5,\"slow\":0.25,\"fast\":0.65,\"seed\":123,\"initial_phases\":[";
    for (unsigned b=0;b<3;++b) {
        if(b) std::cout << ',';
        std::cout << '[' << phases[b][0] << ',' << phases[b][1] << ']';
    }
    std::cout << "],\"output\":[";
    for (unsigned n=0;n<256;++n) {
        const std::complex<double> x{std::sin(.37*n)+.2*std::cos(.11*n),std::cos(.23*n)};
        std::array<Complex,2> low{};
        for(std::size_t i=0;i<2;++i) {
            const auto output=a*x+rotation[i]*(previous_input[i]-a*previous_output[i]);
            previous_input[i]=x;previous_output[i]=output;
            low[i]=(x+output)*.5;
        }
        const std::array<Complex,3> band{low[0],x-low[0]-low[1],low[1]};
        Complex y{};
        for(std::size_t i=0;i<3;++i) {
            const auto gain=1+depth[0]*phase[i][0].imag()+depth[1]*phase[i][1].imag();
            y+=gain*band[i];
            for(std::size_t j=0;j<2;++j)phase[i][j]*=step[j];
        }
        if(n)std::cout << ',';
        std::cout << '[' << y.real() << ',' << y.imag() << ']';
    }
    std::cout << "]},\"kernels\":[";
    bool comma=false;
    for (const auto [width,fs]: {std::pair{.001,.25},std::pair{.01,2.},
            std::pair{.1,16.},std::pair{1.,16.},std::pair{10.,128.},std::pair{30.,512.}}) {
        if(comma)std::cout << ',';comma=true;
        const auto coefficients=gaussian_coefficients(width,1/fs);
        std::cout << "{\"width_hz\":" << width << ",\"sample_rate\":" << fs << ",\"coefficients\":[";
        bool inner=false;
        for (double h:coefficients) {if(inner)std::cout << ',';inner=true;std::cout << h;}
        std::cout << "]}";
    }
    std::cout << "]}\n";
}
