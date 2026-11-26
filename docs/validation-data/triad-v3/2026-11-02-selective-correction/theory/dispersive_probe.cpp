// Finite mathematical probe of the corrected model at ea05d3a8346be7b06a2a53c0e4050e44a963b34b.
// Cubic delay/DFT formulas and arithmetic order from channel_perturbation.hpp;
// SHA-256 fae3b1b345e3dc19c8b85a5010e4434530bd0a34d595dd4de8cd55349d8d879a.
// This fixed diagnostic includes all FIR output, without a channel runtime.
#include "../../2026-11-02-selective/theory/historical_math.hpp"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <utility>

int main() {
    using namespace historical_math;
    constexpr unsigned input_count=24;
    constexpr double sample_rate=16,center_hz=.7,spacing=3,doppler_hz=.7;
    const std::array<double,2> depth{.25,.65},rates{.1*doppler_hz,doppler_hz};
    const long double lag=static_cast<long double>(sample_rate)/(3.L*spacing);
    const auto twice_lag=2*lag,twice_floor=std::floor(twice_lag);
    const auto tail=static_cast<std::uint64_t>(twice_floor)+(twice_lag==twice_floor?0:2);
    const auto total=input_count+tail;
    const auto phases=initial_phases(123);
    std::array<std::array<Complex,2>,3> phase{};
    std::array<Complex,2> step{};
    std::array<std::array<Complex,3>,3> dft{};
    std::array<std::array<Complex,4>,2> coefficients{};
    std::array<std::array<std::uint64_t,4>,2> lags{};
    const long double center_cycles=std::remainder(static_cast<long double>(center_hz),
        static_cast<long double>(sample_rate))/sample_rate;
    for(std::size_t m=1;m<=2;++m) {
        const long double desired=m*lag;
        const auto floor=static_cast<std::uint64_t>(std::floor(desired));
        const double fraction=static_cast<double>(desired-floor);
        for(int tap=-1;tap<=2;++tap) {
            const auto index=static_cast<std::size_t>(tap+1);
            const auto actual=tap<0?floor-1:floor+static_cast<std::uint64_t>(tap);
            lags[m-1][index]=actual;
            double coefficient=1;
            for(int other=-1;other<=2;++other)if(other!=tap)
                coefficient*=(fraction-other)/static_cast<double>(tap-other);
            coefficients[m-1][index]=coefficient*std::polar(1.,tau*static_cast<double>(
                std::remainder(center_cycles*actual,1.L)));
        }
    }
    for(std::size_t band=0;band<3;++band) {
        for(std::size_t m=0;m<3;++m)dft[band][m]=std::polar(1./3,
            tau*(static_cast<double>(band)-1)*static_cast<double>(m)/3);
        for(std::size_t component=0;component<2;++component)
            phase[band][component]=std::polar(1.,phases[band][component]);
    }
    for(std::size_t component=0;component<2;++component)
        step[component]=std::polar(1.,tau*rates[component]/sample_rate);
    std::vector<Complex> inputs(total);
    std::cout<<std::setprecision(17)<<"{\"filter\":{\"sample_rate\":16,\"center_hz\":0.7,\"spacing_hz\":3,\"doppler_hz\":0.7,\"slow\":0.25,\"fast\":0.65,\"input_count\":"<<input_count
        <<",\"tail_samples\":"<<tail<<",\"initial_phases\":[";
    for(unsigned b=0;b<3;++b){if(b)std::cout<<',';std::cout<<'['<<phases[b][0]<<','<<phases[b][1]<<']';}
    std::cout<<"],\"output\":[";
    for(std::uint64_t n=0;n<total;++n){
        const std::complex<double> x=n<input_count?std::complex<double>{std::sin(.37*n)+.2*std::cos(.11*n),std::cos(.23*n)}:std::complex<double>{};
        inputs[n]=x;
        std::array<Complex,3> delayed{x,Complex{},Complex{}};
        for(std::size_t m=0;m<2;++m)for(std::size_t tap=0;tap<4;++tap) {
            const auto coefficient=coefficients[m][tap];
            if(coefficient==Complex{})continue;
            const auto delay=lags[m][tap];
            delayed[m+1]+=coefficient*(n>=delay?inputs[n-delay]:Complex{});
        }
        Complex y=x;
        for(std::size_t band=0;band<3;++band) {
            const double excursion=depth[0]*phase[band][0].imag()+depth[1]*phase[band][1].imag();
            Complex branch{};
            for(std::size_t m=0;m<3;++m)branch+=dft[band][m]*delayed[m];
            y+=excursion*branch;
            for(std::size_t component=0;component<2;++component)phase[band][component]*=step[component];
        }
        if(n)std::cout<<',';std::cout<<'['<<y.real()<<','<<y.imag()<<']';
    }
    std::cout<<"]},\"kernels\":[";bool comma=false;
    for(const auto [width,fs]:{std::pair{.1,16.},std::pair{1.,16.},std::pair{10.,128.}}){
        if(comma)std::cout<<',';comma=true;
        const auto kernel=gaussian_coefficients(width,1/fs);
        std::cout<<"{\"width_hz\":"<<width<<",\"sample_rate\":"<<fs<<",\"coefficients\":[";
        bool inner=false;for(double h:kernel){if(inner)std::cout<<',';inner=true;std::cout<<h;}std::cout<<"]}";
    }
    std::cout<<"]}\n";
}
