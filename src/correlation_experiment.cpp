#include "datapump/correlation_experiment.hpp"
#include "datapump/types.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <random>

namespace datapump::simulation {
namespace {
constexpr long double maximum_seconds = 1e18L;
constexpr std::uint64_t maximum_segments = 1000000000000ULL;
constexpr std::size_t maximum_trials = 1000000;
constexpr long double maximum_signal_energy = 1e24L;
constexpr long double tau = 2 * std::numbers::pi_v<long double>;

void require(bool condition, const char* message) {
    if(!condition)throw Error(message);
}
void validate_coherence(double seconds,double diffusion,double frequency) {
    require(std::isfinite(seconds) && seconds>0 && seconds<=maximum_seconds,
            "correlation duration must be positive and at most 1e18 seconds");
    require(std::isfinite(diffusion) && diffusion>=0 && diffusion<=180,
            "correlation phase noise must be 0..180 degrees/sqrt(second)");
    require(std::isfinite(frequency),"correlation residual frequency must be finite");
}
long double coherence(double seconds,double diffusion,double frequency) {
    const auto sigma=static_cast<long double>(diffusion)*std::numbers::pi_v<long double>/180;
    const auto duration=static_cast<long double>(seconds);
    require(std::abs(static_cast<long double>(frequency))<=1e15L/tau/duration,
            "correlation residual phase exceeds the 1e15-radian precision limit");
    const auto x=.5L*sigma*sigma*duration;
    const auto y=-tau*static_cast<long double>(frequency)*duration;
    const std::complex<long double> z{x,y};
    if(std::abs(z)<.01L) {
        // (z-1+exp(-z))/z^2 = sum[n>=2] (-z)^(n-2)/n!.
        // This also yields exactly one for the zero-noise, zero-offset limit.
        std::complex<long double> term{.5L,0},sum=term;
        for(unsigned n=3;n<=18;++n) {term*=-z/static_cast<long double>(n);sum+=term;}
        return std::clamp(2*sum.real(),0.L,1.L);
    }
    // The reciprocal form avoids z^2 overflow even on implementations where
    // long double has the same range as double. exp(-x) safely underflows.
    const auto inverse=1.L/z;
    const auto value=2*((1.L+(std::exp(-z)-1.L)*inverse)*inverse).real();
    return std::clamp(value,0.L,1.L);
}
struct Moments {
    std::size_t count=0;
    long double mean=0,m2=0;
    void add(long double value) {
        ++count;
        const auto difference=value-mean;
        mean+=difference/static_cast<long double>(count);
        m2+=difference*(value-mean);
    }
};
void wilson(CorrelationExperimentResult& result) {
    constexpr long double z=1.9599639845400542355L;
    const auto n=static_cast<long double>(result.trials);
    const auto p=static_cast<long double>(result.detected_correct)/n;
    const auto denominator=1+z*z/n;
    const auto center=(p+z*z/(2*n))/denominator;
    const auto radius=z*std::sqrt(p*(1-p)/n+z*z/(4*n*n))/denominator;
    result.correct_probability=static_cast<double>(p);
    result.correct_probability_low=result.detected_correct?static_cast<double>(std::max(0.L,center-radius)):0;
    result.correct_probability_high=result.detected_correct==result.trials?1:
        static_cast<double>(std::min(1.L,center+radius));
}
}

double expected_correlation_coherence(double seconds,double diffusion,double frequency) {
    validate_coherence(seconds,diffusion,frequency);
    return static_cast<double>(coherence(seconds,diffusion,frequency));
}

CorrelationExperimentResult correlation_experiment(const CorrelationExperimentParameters& parameters) {
    validate_coherence(parameters.symbol_seconds,parameters.phase_noise_degrees_per_sqrt_second,
                       parameters.residual_frequency_hz);
    require(std::isfinite(parameters.segment_seconds) && parameters.segment_seconds>0 &&
            parameters.segment_seconds<=maximum_seconds,
            "correlation segment duration must be positive and at most 1e18 seconds");
    require(std::isfinite(parameters.cn0_db_hz) && std::abs(parameters.cn0_db_hz)<=300,
            "correlation C/N0 must be -300..300 dB-Hz");
    require(std::isfinite(parameters.search_hypotheses) && parameters.search_hypotheses>=2,
            "correlation search must include at least two hypotheses");
    require(std::isfinite(parameters.template_correlation) && parameters.template_correlation>=0 &&
            parameters.template_correlation<=1,
            "correlation between templates must be 0..1");
    require(std::isfinite(parameters.false_alarm_probability) && parameters.false_alarm_probability>0 &&
            parameters.false_alarm_probability<1,
            "correlation false-alarm probability must be between zero and one");
    require(parameters.trials>0 && parameters.trials<=maximum_trials,
            "correlation experiment requires 1..1000000 trials");
    const auto count=std::max(1.L,std::ceil(static_cast<long double>(parameters.symbol_seconds)/parameters.segment_seconds));
    require(count<=maximum_segments,"correlation experiment exceeds 1000000000000 segments");
    CorrelationExperimentResult result;
    result.segments=static_cast<std::uint64_t>(count);
    result.segment_seconds=static_cast<double>(static_cast<long double>(parameters.symbol_seconds)/count);
    const auto retained=coherence(result.segment_seconds,parameters.phase_noise_degrees_per_sqrt_second,
                                  parameters.residual_frequency_hz);
    const auto energy=std::pow(10.L,static_cast<long double>(parameters.cn0_db_hz)/10)*
        parameters.symbol_seconds*retained;
    require(std::isfinite(energy) && energy<=maximum_signal_energy,
            "correlation matched signal energy exceeds 1e24");
    result.expected_coherence=static_cast<double>(retained);
    result.expected_signal_energy=static_cast<double>(energy);
    result.phase_mean_energy_approximation=parameters.phase_noise_degrees_per_sqrt_second>0;
    // Gamma(K,1) obeys P[X >= K+sqrt(2*K*x)+x] <= exp(-x).
    // Splitting alpha across M alternatives is valid without independence.
    const auto x=std::log(static_cast<long double>(parameters.search_hypotheses))-
        std::log(static_cast<long double>(parameters.false_alarm_probability));
    const auto threshold=count+std::sqrt(2*count*x)+x;
    result.threshold=static_cast<double>(threshold);
    result.template_correlation=parameters.template_correlation;
    result.trials=parameters.trials;

    std::mt19937_64 random(parameters.seed);
    std::normal_distribution<long double> normal{0,std::sqrt(.5L)};
    std::gamma_distribution<long double> noise{count,1};
    // Rotational invariance lets all fixed noncentral energy occupy one
    // complex dimension; K-1 central dimensions are one Gamma draw. This is
    // exactly the K-segment statistic for deterministic segment energies.
    std::gamma_distribution<long double> remainder{std::max(1.L,count-1),1};
    const auto amplitude=std::sqrt(energy);
    const auto correlation=static_cast<long double>(parameters.template_correlation);
    const auto independent_variance=(1-correlation)*(1+correlation);
    const auto independent_amplitude=std::sqrt(independent_variance);
    const auto alternative_for=[&](long double primary) {
        if(correlation==0)return noise(random);
        if(correlation==1)return primary;
        // Conditional on the primary vector, isotropy rotates it into one
        // dimension without changing its correlated alternative's norm.
        const auto real=correlation*std::sqrt(primary)+independent_amplitude*normal(random);
        const auto imaginary=independent_amplitude*normal(random);
        return real*real+imaginary*imaginary+
            (count>1?independent_variance*remainder(random):0);
    };
    Moments moments;
    for(std::size_t trial=0;trial<parameters.trials;++trial) {
        const auto in_phase=amplitude+normal(random),quadrature=normal(random);
        const auto signal=in_phase*in_phase+quadrature*quadrature+(count>1?remainder(random):0);
        const auto alternative=alternative_for(signal);
        result.detected_correct+=signal>=threshold && signal>alternative;
        const auto noise_zero=noise(random),noise_one=alternative_for(noise_zero);
        result.noise_pair_above+=std::max(noise_zero,noise_one)>=threshold;
        moments.add(signal);
    }
    result.signal_statistic_mean=static_cast<double>(moments.mean);
    result.signal_statistic_variance=static_cast<double>(moments.count>1?moments.m2/(moments.count-1):0);
    wilson(result);
    return result;
}

} // namespace datapump::simulation
