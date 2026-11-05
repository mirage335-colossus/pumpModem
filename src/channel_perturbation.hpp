#pragma once
#include "datapump/types.hpp"
#include "triad_random_process.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>
#include <memory>
#include <limits>
#include <vector>

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
    // in sampled simulation, bulk PCM and physical playback.
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

// A broadband linear time-varying dispersive stress operator. Three
// overlapping DFT delay branches form a complementary partition of *every*
// captured frequency, rather than only a bank around the unshifted source.
// No carrier identity is inspected: arbitrary overlapping spectra, Doppler
// sidebands and oscillator-shifted inputs pass through this same operator.
//
// With ideal delays d_m=m/(3*spacing),
// B_b(f)=sum_m exp(i*2pi*(b-1)*m/3)exp(-i*2pi*(f-center)*d_m)/3.
// Sum_b B_b=1 and sum_b |B_b|^2=1. The ideal response is periodic with
// period 3*spacing; this is an intentional synthetic frequency-correlation law.
// Strong selectivity at millihertz spacing entails hundreds of seconds of
// extra dispersive memory, separate from SpreadRay's millisecond excess delay.
// The sampled implementation approximates each fractional delay by a causal
// cubic Lagrange FIR, so the ideal periodicity is likewise approximate. Its
// actual response and sampling error must be measured. These FIR delays are
// L2 contractions: for delta in [0,1] and s=sin^2(omega/2), their squared gain
// is 1-(4/9)delta(1-delta)(2-delta)(1+delta)s^2[3+4delta(1-delta)s].
class DispersiveSelectivePerturbation {
    using Complex=std::complex<double>;
    static constexpr double tau=2*std::numbers::pi;
    static constexpr std::uint64_t maximum_history_samples=4'194'240;
    struct Delay {
        std::array<std::uint64_t,4> lag{};
        std::array<Complex,4> coefficient{};
    };
    std::array<Delay,2> delays_{};
    std::array<std::array<Complex,2>,3> phase_{};
    std::array<Complex,2> step_{};
    std::array<double,2> depth_{};
    std::array<std::array<Complex,3>,3> dft_{};
    std::array<std::array<std::unique_ptr<triad::v3::detail::FixedGaussianProcess>,2>,3> random_{};
    double sample_rate_ = 1;
    bool random_coefficients_ = false;
    std::vector<Complex> history_;
    std::size_t cursor_=0;
    std::uint64_t count_=0;
    static std::uint64_t mix(std::uint64_t x) {
        x+=0x9e3779b97f4a7c15ULL;x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;
        x=(x^(x>>27))*0x94d049bb133111ebULL;return x^(x>>31);
    }
    static long double lag_samples(double sample_rate,double spacing) {
        if(!(std::isfinite(sample_rate)&&sample_rate>0&&std::isfinite(spacing)&&spacing>0&&
                sample_rate>3*spacing))
            throw Error("dispersive selective model needs finite Fs > 3*correlation spacing > 0");
        const long double lag=static_cast<long double>(sample_rate)/(3.L*spacing);
        if(!std::isfinite(lag)||lag<1||2*lag+4>maximum_history_samples)
            throw Error("dispersive selective history exceeds its 64 MiB sample bound");
        return lag;
    }
public:
    std::array<std::array<double,2>,3> initial_phases{};
    std::array<double,2> rates_hz{};
    std::array<double,3> band_centers_hz{};
    std::array<double,3> branch_delays_seconds{};
    double band_width_hz=0,response_period_hz=0;
    // Four-point interpolation uses lags floor(d)+{-1,0,1,2}. The
    // largest nonzero lag bounds the exact finite zero-input drain.
    static std::uint64_t tail_samples(double sample_rate,double spacing) {
        const auto lag=2*lag_samples(sample_rate,spacing);
        const auto floor=std::floor(lag);
        return static_cast<std::uint64_t>(floor)+(lag==floor?0:2);
    }
    static std::uint64_t memory_samples(double sample_rate,double spacing) {
        return tail_samples(sample_rate,spacing)+1;
    }
    static constexpr std::size_t maximum_random_working_bytes() {
        return 6*sizeof(triad::v3::detail::FixedGaussianProcess);
    }
    std::size_t working_bytes() const {
        std::size_t bytes=sizeof(*this)+history_.capacity()*sizeof(Complex);
        for(const auto& band:random_)for(const auto& component:band)
            if(component)bytes+=sizeof(triad::v3::detail::FixedGaussianProcess);
        return bytes;
    }
    DispersiveSelectivePerturbation(double sample_rate,double center_hz,double spacing,
                                   double doppler_hz,double slow,double fast,std::uint64_t seed,
                                   bool random_coefficients=false)
        :depth_{slow,fast},sample_rate_(sample_rate),random_coefficients_(random_coefficients),
         rates_hz{.1*doppler_hz,doppler_hz},
         band_centers_hz{center_hz-spacing,center_hz,center_hz+spacing},
         branch_delays_seconds{0,1/(3*spacing),2/(3*spacing)},
         band_width_hz(spacing),response_period_hz(3*spacing) {
        const auto lag=lag_samples(sample_rate,spacing);
        if(!(std::isfinite(center_hz)&&std::isfinite(doppler_hz)&&doppler_hz>=0&&
             std::isfinite(slow)&&slow>=0&&std::isfinite(fast)&&fast>=0))
            throw Error("invalid dispersive selective modulation parameters");
        history_.resize(static_cast<std::size_t>(memory_samples(sample_rate,spacing)));
        const long double center_cycles=std::remainder(static_cast<long double>(center_hz),
            static_cast<long double>(sample_rate))/sample_rate;
        for(std::size_t m=1;m<=2;++m) {
            const long double desired=m*lag;
            const auto floor=static_cast<std::uint64_t>(std::floor(desired));
            const double fraction=static_cast<double>(desired-floor);
            auto& delay=delays_[m-1];
            for(int tap=-1;tap<=2;++tap) {
                const auto index=static_cast<std::size_t>(tap+1);
                const auto actual=tap<0?floor-1:floor+static_cast<std::uint64_t>(tap);
                delay.lag[index]=actual;
                double coefficient=1;
                for(int other=-1;other<=2;++other)if(other!=tap)
                    coefficient*=(fraction-other)/static_cast<double>(tap-other);
                // Rotate each ACTUAL integer-delay tap, so changing a
                // baseband reference transforms the complete sampled FIR.
                delay.coefficient[index]=coefficient*std::polar(1.,tau*static_cast<double>(
                    std::remainder(center_cycles*actual,1.L)));
            }
        }
        for(std::size_t band=0;band<3;++band) {
            for(std::size_t m=0;m<3;++m)dft_[band][m]=std::polar(1./3,
                tau*(static_cast<double>(band)-1)*static_cast<double>(m)/3);
            for(std::size_t component=0;component<2;++component) {
                const auto bits=mix(seed^mix(2*band+component+0x9072d5ULL));
                initial_phases[band][component]=tau*(static_cast<double>(bits>>11)+.5)/9007199254740992.;
                phase_[band][component]=std::polar(1.,initial_phases[band][component]);
                if(random_coefficients_ && rates_hz[component]>0 && depth_[component]>0)
                    random_[band][component]=std::make_unique<triad::v3::detail::FixedGaussianProcess>(
                        rates_hz[component],seed^mix(2*band+component+0x52414e444f4dULL));
            }
        }
        for(std::size_t component=0;component<2;++component)
            step_[component]=std::polar(1.,tau*rates_hz[component]/sample_rate);
    }
    Complex process(Complex input,long double physical_seconds=std::numeric_limits<long double>::quiet_NaN()) {
        history_[cursor_]=input;
        std::array<Complex,3> delayed{input,Complex{},Complex{}};
        for(std::size_t m=0;m<2;++m)for(std::size_t tap=0;tap<4;++tap) {
            const auto coefficient=delays_[m].coefficient[tap];
            if(coefficient==Complex{})continue;
            const auto lag=static_cast<std::size_t>(delays_[m].lag[tap]);
            const auto index=cursor_>=lag?cursor_-lag:history_.size()-(lag-cursor_);
            delayed[m+1]+=coefficient*history_[index];
        }
        // The unity path is exact; depths zero never introduce a numerical
        // filter or latency. All added paths and their complex phase remain.
        Complex output=input;
        if(random_coefficients_ && std::isnan(physical_seconds))
            physical_seconds=static_cast<long double>(count_)/sample_rate_;
        for(std::size_t band=0;band<3;++band) {
            double excursion=0;
            if(random_coefficients_) {
                for(std::size_t component=0;component<2;++component)
                    if(random_[band][component]) excursion+=depth_[component]*
                        random_[band][component]->bounded_value(physical_seconds);
            } else excursion=depth_[0]*phase_[band][0].imag()+depth_[1]*phase_[band][1].imag();
            Complex branch{};
            for(std::size_t m=0;m<3;++m)branch+=dft_[band][m]*delayed[m];
            output+=excursion*branch;
            for(std::size_t component=0;component<2;++component)phase_[band][component]*=step_[component];
        }
        if(++cursor_==history_.size())cursor_=0;
        if((++count_&4095U)==0)for(auto& phases:phase_)for(auto& p:phases)p/=std::abs(p);
        return output;
    }
};
}
