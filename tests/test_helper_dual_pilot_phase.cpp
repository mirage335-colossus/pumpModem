#include "datapump/helper_dual_pilot_phase.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <stdexcept>

namespace {
namespace h=datapump::helper;
constexpr double tau=2*std::numbers::pi;
void require(bool b,const char* why) {if(!b)throw std::runtime_error(why);}
double wrap(double p) {return std::remainder(p,tau);}
h::HelperDualPilotPhaseInput input(double t) {
    h::HelperDualPilotPhaseInput x;x.valid=true;x.time_seconds=t;
    x.high_phase_radians=3.12;x.low_phase_radians=-2.98;
    x.thermal_covariance={{{.07*.07,0},{0,.1*.1}}};return x;
}
h::HelperDualPilotPhasePrediction prediction(double t) {
    h::HelperDualPilotPhasePrediction p;p.valid=p.previous_observations_only=p.reference_preserved=true;
    p.alternatives=1;p.time_seconds=t;p.latest_observation_seconds=t-.4;p.reference_input_seconds=1.2;
    p.high_minus_low_radians=wrap(3.12+2.98);p.thermal_sigma_radians=.01;return p;
}

void boundaries() {
    h::HelperDualPilotPhase blend({});
    auto x=input(10);auto p=prediction(10);
    auto r=blend.observe(x,p);require(r.valid&&r.combined&&r.reference_preserved,"clean wrapped phases not combined");
    require(std::abs(wrap(r.high_gauge_phase_radians-x.high_phase_radians))<1e-12,"clean wrapped alignment shifted phase");
    require(r.low_weight>.3&&r.low_weight<.34,"incorrect independent pilot weight");
    require(r.conditional_half_width_radians<r.high_only_half_width_radians,"reported radius did not improve");
    require(!blend.observe(x,p).valid,"duplicate observation accepted");
    x=input(10.4);p=prediction(10.4);p.valid=false;
    r=blend.observe(x,p);require(!r.valid&&r.reference_preserved,"gap discarded established reference");
    x=input(10.8);p=prediction(10.8);p.reference_input_seconds=4;
    r=blend.observe(x,p);require(!r.valid&&r.failure==h::HelperDualPilotPhaseFailure::reference_mismatch,"new reference silently accepted after gap");
    x=input(11.2);p=prediction(11.2);
    require(blend.observe(x,p).valid,"same reference failed recovery");
    x=input(11.6);p=prediction(11.6);p.latest_observation_seconds=x.time_seconds;
    require(!blend.observe(x,p).valid,"current observation reused as independent clock evidence");
    x=input(12);p=prediction(12);p.latest_observation_seconds=9;
    require(!blend.observe(x,p).valid,"stale clock phase prediction accepted");
    x=input(12.4);p=prediction(12.4);p.alternatives=2;
    require(!blend.observe(x,p).valid,"unresolved clock branch accepted");
    x=input(12.8);p=prediction(12.8);p.high_minus_low_radians+=1.2;
    r=blend.observe(x,p);require(!r.valid&&r.failure==h::HelperDualPilotPhaseFailure::inconsistent_difference,"gross differential phase change accepted");
    x=input(13.2);p=prediction(13.2);p.thermal_sigma_radians=.5;
    require(!blend.observe(x,p).valid,"cycle-ambiguous phase prediction accepted");
    x=input(13.6);p=prediction(13.6);x.thermal_covariance[0][1]=.1;x.thermal_covariance[1][0]=.1;
    require(!blend.observe(x,p).valid,"indefinite current covariance accepted");
    x=input(14);p=prediction(14);p.current_prediction_covariance[0]=.1;
    require(!blend.observe(x,p).valid,"indefinite joint covariance accepted");
    x=input(14.4);p=prediction(14.4);x.high_phase_radians=std::numeric_limits<double>::infinity();
    require(!blend.observe(x,p).valid,"infinite phase accepted");
    x=input(14.8);p=prediction(14.8);x.thermal_covariance[0][0]=1e308;
    require(!blend.observe(x,p).valid,"extreme covariance accepted");
    x=input(15.2);p=prediction(15.2);x.valid=false;
    require(!blend.observe(x,p).valid,"missing pilot supplied fresh phase");
    x=input(15.6);p=prediction(15.6);x.high_phase_radians=1e308;x.low_phase_radians=-1e308;
    const auto huge=blend.observe(x,p);
    require(!huge.valid||std::isfinite(huge.high_gauge_phase_radians),"finite extreme phases produced an invalid valid phase");
    blend.reset();x=input(20);p=prediction(20);p.reference_input_seconds=18;
    require(blend.observe(x,p).valid,"explicit new reception reset failed");
}

void monte_carlo(bool correlated,double differential_constant,double differential_bound) {
    h::HelperDualPilotPhase blend({});
    std::mt19937_64 rng(correlated?921:391);std::normal_distribution<double> normal;
    std::uniform_real_distribution<double> phase(-std::numbers::pi,std::numbers::pi);
    double high2=0,combined2=0,normalized2=0,largest=0;unsigned admitted=0,missed=0;
    constexpr unsigned trials=4096;
    for(unsigned i=0;i<trials;++i) {
        const double t=10+.4*i,theta=phase(rng),eh=.07*normal(rng),el=.1*normal(rng);
        const double ep=(correlated?.2*eh:0)+.01*normal(rng);
        const double variation=differential_bound*std::sin(1.6180339887498948*i);
        auto x=input(t);auto p=prediction(t);
        x.high_phase_radians=wrap(theta+eh);
        x.low_phase_radians=wrap(theta-differential_constant-variation+el);
        p.high_minus_low_radians=wrap(differential_constant+ep);
        p.thermal_sigma_radians=std::sqrt(.01*.01+(correlated?.04*.07*.07:0));
        p.current_prediction_covariance[0]=correlated?.2*.07*.07:0;
        p.model_half_width_radians=differential_bound;
        const auto r=blend.observe(x,p);
        if(!r.valid)continue;
        const double e=wrap(r.high_gauge_phase_radians-theta);
        ++admitted;high2+=eh*eh;combined2+=e*e;largest=std::max(largest,std::abs(e));
        normalized2+=e*e/(r.thermal_sigma_radians*r.thermal_sigma_radians);
        if(std::abs(e)>r.conditional_half_width_radians)++missed;
    }
    require(admitted>trials*.99,"too many clean independently hopped pilots declined");
    require(combined2<high2*.85,"dual-pilot phase did not improve matched RMS");
    // Finite seeded Gaussian check, not a rare-tail calibration. Bounded
    // differential phase is accounted in the radius, not falsely called noise.
    require(missed<30,"conditional radius missed too many matched samples");
    std::cout<<"helper_dual_phase correlated="<<correlated<<" unknown_difference_rad="<<differential_constant
        <<" differential_bound_rad="<<differential_bound<<" admitted="<<admitted<<'/'<<trials
        <<" high_rms_deg="<<std::sqrt(high2/admitted)*180/std::numbers::pi
        <<" combined_rms_deg="<<std::sqrt(combined2/admitted)*180/std::numbers::pi
        <<" maximum_error_deg="<<largest*180/std::numbers::pi
        <<" conditional_radius_misses="<<missed<<" normalized_error_rms="<<std::sqrt(normalized2/admitted)<<'\n';
}

void common_process_and_model_bound() {
    h::HelperDualPilotPhase blend({});auto x=input(10);auto p=prediction(10);
    x.common_phase_sigma_radians=.08;
    const auto r=blend.observe(x,p);
    require(r.valid&&r.common_sigma_radians==.08,"averaging incorrectly removed common phase uncertainty");
    require(r.conditional_half_width_radians>=3*.08,"reported uncertainty below shared process contribution");
    h::HelperDualPilotPhase process({});x=input(10);p=prediction(10);p.process_sigma_radians=.01;
    const auto process_result=process.observe(x,p);
    require(process_result.valid&&process_result.prediction_process_sigma_radians>0,
            "predictor process uncertainty disappeared");
    require(process_result.conditional_half_width_radians>=3*(process_result.thermal_sigma_radians+
            process_result.prediction_process_sigma_radians),"predictor process not added conservatively");
    h::HelperDualPilotPhase bounded({});x=input(10);p=prediction(10);p.model_half_width_radians=.5;
    const auto no=bounded.observe(x,p);
    require(!no.valid&&no.failure==h::HelperDualPilotPhaseFailure::no_benefit,"model bound reported optimistic combination gain");
}

void covariance_all_terms() {
    h::HelperDualPilotPhase blend({});std::mt19937_64 rng(751);
    std::normal_distribution<double> normal;double normalized2=0;unsigned count=0;
    for(unsigned i=0;i<4096;++i) {
        const double g1=normal(rng),g2=normal(rng),g3=normal(rng);
        const double high_error=.07*g1,low_error=.03*g1+.095*g2;
        const double prediction_error=.012*g1+.008*g2+.008*g3;
        auto x=input(10+.4*i);auto p=prediction(x.time_seconds);
        x.high_phase_radians=wrap(2.9+high_error);x.low_phase_radians=wrap(2.+low_error);
        x.thermal_covariance={{{.0049,.0021},{.0021,.009925}}};
        p.high_minus_low_radians=wrap(.9+prediction_error);p.thermal_sigma_radians=std::sqrt(.000272);
        p.current_prediction_covariance={.00084,.00112};
        const auto r=blend.observe(x,p);require(r.valid,"valid fully correlated covariance was declined");
        const double e=wrap(r.high_gauge_phase_radians-2.9);
        normalized2+=e*e/(r.thermal_sigma_radians*r.thermal_sigma_radians);++count;
    }
    const double normalized=std::sqrt(normalized2/count);
    require(normalized>.94&&normalized<1.06,"full correlation propagation disagreed with matched Monte Carlo");
}
}

int main() {
    try {
        boundaries();monte_carlo(false,.8,0);monte_carlo(false,-2.9,.04);
        monte_carlo(true,2.9,0);common_process_and_model_bound();covariance_all_terms();
        std::cout<<"Helper dual-pilot phase tests passed\n";return 0;
    } catch(const std::exception& e) {std::cerr<<"Helper dual-pilot phase failure: "<<e.what()<<'\n';return 1;}
}
