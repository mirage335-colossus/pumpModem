#pragma once
#include "datapump/types.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>

namespace datapump::modem::detail {
// Two complementary complex all-pass crossovers split the occupied payload
// band into low, middle and high ranges. The three signals sum exactly to the
// input before modulation. State and modulation phases follow receiver time.
class FrequencySelectivePerturbation {
    using Complex=std::complex<double>;
    static constexpr double tau=2*std::numbers::pi;
    struct Crossover {
        double a=0;
        Complex rotation{},previous_input{},previous_output{};
        Complex low(Complex input) {
            const auto output=a*input+rotation*(previous_input-a*previous_output);
            previous_input=input;previous_output=output;
            return (input+output)*.5;
        }
    };
    std::array<Crossover,2> filters_{};
    std::array<std::array<Complex,2>,3> phase_{};
    std::array<Complex,2> step_{};
    std::array<double,2> depth_{};
    std::uint64_t count_=0;
    static std::uint64_t mix(std::uint64_t x) {
        x+=0x9e3779b97f4a7c15ULL;x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;
        x=(x^(x>>27))*0x94d049bb133111ebULL;return x^(x>>31);
    }
public:
    // A finite zero-input drain after the final delayed DATA sample. The
    // filter is IIR, so this bounds relative pole-state decay rather than
    // declaring an absolute acoustic silence threshold. Keep the same policy
    // in sampled simulation, bulk PCM and physical Audio test playback.
    static std::uint64_t decay_samples(double sample_rate,double bandwidth_hz) {
        const auto cutoff=std::min(bandwidth_hz/6,sample_rate*.24);
        const auto k=std::tan(std::numbers::pi*cutoff/sample_rate);
        const auto pole=std::abs((k-1)/(k+1));
        if(!(pole>=0 && pole<1))throw Error("frequency-selective filter has no finite decay at this sample clock");
        const auto count=pole>0?std::ceil(std::log(1e-6L)/std::log(static_cast<long double>(pole))):0.L;
        if(!std::isfinite(count) || count>=std::ldexp(1.L,64))
            throw Error("frequency-selective filter tail exceeds sample coordinate");
        return static_cast<std::uint64_t>(count);
    }
    std::array<std::array<double,2>,3> initial_phases{};
    std::array<double,2> rates_hz{};
    std::array<double,3> band_centers_hz{};
    double band_width_hz=0;
    FrequencySelectivePerturbation(double sample_rate,double carrier_hz,double bandwidth_hz,
                                   double doppler_hz,double slow,double fast,std::uint64_t seed)
        :depth_{slow,fast},rates_hz{.1*doppler_hz,doppler_hz},band_width_hz(bandwidth_hz/3) {
        const auto cutoff=std::min(bandwidth_hz/6,sample_rate*.24);
        const auto k=std::tan(std::numbers::pi*cutoff/sample_rate);
        const auto a=(k-1)/(k+1);
        band_centers_hz={carrier_hz-bandwidth_hz/3,carrier_hz,carrier_hz+bandwidth_hz/3};
        for(std::size_t i=0;i<2;++i) {
            filters_[i].a=a;
            filters_[i].rotation=std::polar(1.,tau*band_centers_hz[i?2:0]/sample_rate);
            step_[i]=std::polar(1.,tau*rates_hz[i]/sample_rate);
        }
        for(std::size_t band=0;band<3;++band)for(std::size_t component=0;component<2;++component) {
            const auto bits=mix(seed^mix(2*band+component+0x9072d5ULL));
            initial_phases[band][component]=tau*(static_cast<double>(bits>>11)+.5)/9007199254740992.;
            phase_[band][component]=std::polar(1.,initial_phases[band][component]);
        }
    }
    Complex process(Complex input) {
        const auto low=filters_[0].low(input),high=filters_[1].low(input);
        const std::array<Complex,3> band{low,input-low-high,high};
        Complex output{};
        for(std::size_t i=0;i<3;++i) {
            // Additive excursions specify each component relative to nominal.
            const auto gain=1+depth_[0]*phase_[i][0].imag()+depth_[1]*phase_[i][1].imag();
            output+=gain*band[i];
            for(std::size_t j=0;j<2;++j)phase_[i][j]*=step_[j];
        }
        if((++count_&4095U)==0)for(auto& phases:phase_)for(auto& p:phases)p/=std::abs(p);
        return output;
    }
};
}
