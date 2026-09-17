#include "datapump/correlation_experiment.hpp"
#include "datapump/types.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <stdexcept>

using namespace datapump;
namespace {
namespace experiment=datapump::simulation;
void check(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
void near(double actual,double expected,double tolerance,const char* message) {
    check(std::isfinite(actual) && std::abs(actual-expected)<=tolerance,message);
}
template<class Action>void rejects(Action action,const char* message) {
    try {action();}catch(const Error&){return;}
    throw std::runtime_error(message);
}
struct Direct {
    double probability=0,mean=0,variance=0,noise_probability=0;
};
// Independent reference: explicitly generate every complex segment and its
// correlated wrong-template projection. This is intentionally bounded to the
// small K used here; the production experiment must never iterate over K.
Direct direct_segments(unsigned segments,double energy,double correlation,double threshold) {
    constexpr unsigned trials=100000;
    std::mt19937_64 random(713);
    std::normal_distribution<double> normal(0,std::sqrt(.5));
    const auto amplitude=std::sqrt(energy/segments),independent=std::sqrt(1-correlation*correlation);
    const auto noise=[&] {return std::complex<double>{normal(random),normal(random)};};
    double sum=0,squares=0;
    unsigned accepted=0,noise_pair=0;
    for(unsigned trial=0;trial<trials;++trial) {
        double primary=0,alternative=0,null_primary=0,null_alternative=0;
        for(unsigned segment=0;segment<segments;++segment) {
            const auto signal=std::complex<double>{amplitude,0}+noise();
            primary+=std::norm(signal);
            alternative+=std::norm(correlation*signal+independent*noise());
            const auto null=noise();
            null_primary+=std::norm(null);
            null_alternative+=std::norm(correlation*null+independent*noise());
        }
        accepted+=primary>=threshold && primary>alternative;
        noise_pair+=std::max(null_primary,null_alternative)>=threshold;
        sum+=primary;squares+=primary*primary;
    }
    const auto mean=sum/trials;
    return {static_cast<double>(accepted)/trials,mean,squares/trials-mean*mean,
            static_cast<double>(noise_pair)/trials};
}
void exact_statistic_reference() {
    for(const auto segments:{1U,4U})for(const auto correlation:{0.,.6}) {
        experiment::CorrelationExperimentParameters parameters;
        parameters.cn0_db_hz=10*std::log10(2.5/4);
        parameters.symbol_seconds=4;parameters.segment_seconds=4./segments;
        parameters.phase_noise_degrees_per_sqrt_second=0;
        parameters.template_correlation=correlation;
        parameters.false_alarm_probability=.2;parameters.trials=100000;parameters.seed=42;
        const auto result=experiment::correlation_experiment(parameters);
        check(result.segments==segments && !result.phase_mean_energy_approximation,
              "deterministic ideal geometry was changed or labeled phase approximate");
        near(result.expected_signal_energy,2.5,1e-12,"C/N0 duration energy normalization");
        const auto direct=direct_segments(segments,2.5,correlation,result.threshold);
        near(result.signal_statistic_mean,segments+2.5,.06,"noncentral Gamma statistic mean");
        near(result.signal_statistic_variance,segments+5.,.2,"noncentral Gamma statistic variance");
        near(result.signal_statistic_mean,direct.mean,.07,"reduced statistic differs from direct segment mean");
        near(result.signal_statistic_variance,direct.variance,.25,"reduced statistic differs from direct segment variance");
        near(result.correct_probability,direct.probability,.009,"reduced and direct detection probabilities disagree");
        near(static_cast<double>(result.noise_pair_above)/result.trials,direct.noise_probability,.004,
             "reduced correlated null statistics disagree with direct segments");
        check(result.correct_probability_low<=result.correct_probability &&
              result.correct_probability_high>=result.correct_probability &&
              result.correct_probability_high-result.correct_probability_low<.007,
              "Wilson interval must bracket the estimate at the actual trial count");
    }
}
void gamma_false_alarm_bound() {
    experiment::CorrelationExperimentParameters parameters;
    parameters.cn0_db_hz=-300;parameters.symbol_seconds=4;parameters.segment_seconds=1;
    parameters.phase_noise_degrees_per_sqrt_second=0;
    parameters.false_alarm_probability=.2;parameters.trials=200000;
    const auto result=experiment::correlation_experiment(parameters);
    // Exact integer-shape survival for one independent Gamma(4,1).
    const auto q=result.threshold;
    const auto survival=std::exp(-q)*(1+q+q*q/2+q*q*q/6);
    const auto pair=1-(1-survival)*(1-survival);
    near(static_cast<double>(result.noise_pair_above)/result.trials,pair,.002,
         "noise-only probability does not match Gamma survival");
    check(pair<=parameters.false_alarm_probability,"Gamma threshold violated its two-hypothesis union bound");
    const auto original=result.threshold;
    parameters.search_hypotheses=1e9;
    const auto larger=experiment::correlation_experiment(parameters);
    check(larger.threshold>original && larger.detected_correct<=result.detected_correct,
          "a larger searched bank must increase evidence cost");
    parameters.false_alarm_probability=1e-10;
    const auto stringent=experiment::correlation_experiment(parameters);
    check(stringent.threshold>larger.threshold,"a smaller false-alarm budget must increase evidence cost");
}
double numerical_coherence(double seconds,double diffusion,double frequency) {
    // Independent quadrature of 2 integral_0^1 (1-u)exp(-x*u)cos(y*u) du.
    constexpr unsigned intervals=20000;
    const auto sigma=diffusion*std::numbers::pi/180;
    const auto x=sigma*sigma*seconds/2,y=2*std::numbers::pi*frequency*seconds;
    double sum=0;
    for(unsigned index=0;index<intervals;++index) {
        const auto u=(index+.5)/intervals;
        sum+=2*(1-u)*std::exp(-x*u)*std::cos(y*u)/intervals;
    }
    return sum;
}
void coherence_integrals() {
    const auto coherence=experiment::expected_correlation_coherence;
    near(coherence(1e18,0,0),1,0,"ideal arbitrarily long segment lost coherence");
    for(const auto frequency:{0.,.00013,-.00013,.001}) {
        const auto seconds=700.;
        const auto angle=std::numbers::pi*frequency*seconds;
        const auto sinc=angle==0?1:std::sin(angle)/angle;
        near(coherence(seconds,0,frequency),sinc*sinc,1e-13,"pure CFO coherence must equal sinc squared");
        near(coherence(seconds,.5,frequency),numerical_coherence(seconds,.5,frequency),5e-8,
             "joint Wiener phase and CFO energy integral");
    }
    const auto diffusion=.5*std::numbers::pi/180;
    const auto small=diffusion*diffusion*.001/2;
    near(coherence(.001,.5,0),1-small/3+small*small/12,1e-14,
         "small joint integral lost precision");
    const auto long_seconds=1e12;
    near(coherence(long_seconds,.5,0)*long_seconds,4/(diffusion*diffusion),.003,
         "long coherent mean energy must reach the Wiener plateau");
    rejects([&]{(void)coherence(1e18,180,std::numeric_limits<double>::max());},
            "unrepresentable residual phase was accepted");
    check(std::isfinite(coherence(1e18,180,1e-5)),"finite extreme coherence geometry became nonfinite");
    experiment::CorrelationExperimentParameters parameters;
    parameters.trials=10;
    const auto result=experiment::correlation_experiment(parameters);
    check(result.phase_mean_energy_approximation,"Wiener mean-energy approximation must be disclosed");
    const auto combined=coherence(1000,5,.0007);
    const auto multiplied=coherence(1000,5,0)*coherence(1000,0,.0007);
    check(std::abs(combined-multiplied)>.05,"joint integral silently multiplied separate coherence losses");
}
void geometry_bounds_and_determinism() {
    experiment::CorrelationExperimentParameters parameters;
    parameters.symbol_seconds=10;parameters.segment_seconds=3;parameters.trials=300;
    const auto equal=experiment::correlation_experiment(parameters);
    check(equal.segments==4 && equal.segment_seconds==2.5,"equal segments must cover all symbol time");
    parameters.symbol_seconds=1e18;parameters.segment_seconds=1e6;
    parameters.cn0_db_hz=-90;
    const auto huge=experiment::correlation_experiment(parameters);
    const auto repeated=experiment::correlation_experiment(parameters);
    check(huge.segments==1000000000000ULL && huge.segment_seconds*huge.segments==parameters.symbol_seconds,
          "large segment count dropped represented time");
    check(std::isfinite(huge.threshold) && std::isfinite(huge.signal_statistic_mean) &&
          std::isfinite(huge.signal_statistic_variance),"large reduced statistics became nonfinite");
    check(huge.detected_correct==repeated.detected_correct && huge.noise_pair_above==repeated.noise_pair_above &&
          huge.signal_statistic_mean==repeated.signal_statistic_mean &&
          huge.signal_statistic_variance==repeated.signal_statistic_variance,
          "seeded experiment is not deterministic");
    parameters.symbol_seconds=100;parameters.segment_seconds=1e6;parameters.cn0_db_hz=0;
    parameters.phase_noise_degrees_per_sqrt_second=0;parameters.template_correlation=1;
    const auto identical=experiment::correlation_experiment(parameters);
    check(identical.segments==1 && identical.detected_correct==0 && identical.correct_probability_low==0 &&
          identical.correct_probability_high>0 && identical.template_correlation==1,
          "identical templates must never be distinguishable as correct bits");
    parameters.template_correlation=0;
    const auto distinct=experiment::correlation_experiment(parameters);
    check(distinct.detected_correct>identical.detected_correct,"orthogonal reference failed to distinguish strong signal");
    parameters.trials=1;
    const auto single=experiment::correlation_experiment(parameters);
    check(single.signal_statistic_variance==0 && single.correct_probability_low>=0 && single.correct_probability_high<=1,
          "one-trial estimate must retain a finite interval and variance");
}
void invalid_inputs() {
    const auto rejected=[](auto change) {
        experiment::CorrelationExperimentParameters parameters;change(parameters);
        rejects([&]{(void)experiment::correlation_experiment(parameters);},"invalid correlation experiment accepted");
    };
    rejected([](auto& p){p.symbol_seconds=0;});
    rejected([](auto& p){p.symbol_seconds=1e19;});
    rejected([](auto& p){p.segment_seconds=0;});
    rejected([](auto& p){p.segment_seconds=1e-20;});
    rejected([](auto& p){p.cn0_db_hz=-301;});
    rejected([](auto& p){p.cn0_db_hz=300;}); // The derived energy exceeds its explicit limit.
    rejected([](auto& p){p.phase_noise_degrees_per_sqrt_second=-1;});
    rejected([](auto& p){p.phase_noise_degrees_per_sqrt_second=181;});
    rejected([](auto& p){p.residual_frequency_hz=std::numeric_limits<double>::infinity();});
    rejected([](auto& p){p.residual_frequency_hz=std::numeric_limits<double>::max();});
    rejected([](auto& p){p.template_correlation=-.1;});
    rejected([](auto& p){p.template_correlation=1.1;});
    rejected([](auto& p){p.template_correlation=std::numeric_limits<double>::quiet_NaN();});
    rejected([](auto& p){p.search_hypotheses=1;});
    rejected([](auto& p){p.search_hypotheses=std::numeric_limits<double>::infinity();});
    rejected([](auto& p){p.false_alarm_probability=0;});
    rejected([](auto& p){p.false_alarm_probability=1;});
    rejected([](auto& p){p.trials=0;});
    rejected([](auto& p){p.trials=1000001;});
    rejects([]{(void)experiment::expected_correlation_coherence(-1,0,0);},"negative coherence duration accepted");
}
}
int main() {
    try {
        exact_statistic_reference();gamma_false_alarm_bound();coherence_integrals();
        geometry_bounds_and_determinism();invalid_inputs();
        std::cout<<"bounded correlation experiment tests passed\n";return 0;
    } catch(const std::exception& error) {
        std::cerr<<"correlation experiment tests failed: "<<error.what()<<'\n';return 1;
    }
}
