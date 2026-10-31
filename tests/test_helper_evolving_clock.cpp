#include "datapump/helper_evolving_clock.hpp"
#include <stdexcept>
#include <cmath>
#include <iostream>
#include <limits>
using namespace datapump::helper;
void require(bool value){if(!value)throw std::runtime_error("evolving frequency control failed");}
int main(){
    HelperEvolvingFrequencyInput x;
    x.valid=x.reference_preserved=x.previous_only=true;
    x.time_seconds=5.2;x.previous_observation_seconds=4.8;x.forecast_hz=2400.2;
    x.forecast_thermal_sigma_hz=.02;x.forecast_process_sigma_hz=.001;
    x.forecast_model_half_width_hz=.005;x.forecast_conditional_half_width_hz=.12;
    x.observed_hz=2400.3;x.observed_thermal_sigma_hz=.1;
    x.observed_process_sigma_hz=.002;x.observed_model_half_width_hz=.005;
    auto r=refine_helper_evolving_frequency(x);
    require(r.valid&&!r.fallback_required&&r.observation_weight>0&&r.observation_weight<.1);
    require(r.frequency_hz>x.forecast_hz&&r.frequency_hz<x.observed_hz);
    require(r.conditional_half_width_hz>=(1-r.observation_weight)*x.forecast_conditional_half_width_hz);
    auto shifted=x;shifted.forecast_hz+=1e6;shifted.observed_hz+=1e6;
    const auto s=refine_helper_evolving_frequency(shifted);
    require(s.valid&&std::abs((s.frequency_hz-r.frequency_hz)-1e6)<1e-9);
    auto bad=x;bad.observed_hz+=3;require(!refine_helper_evolving_frequency(bad).valid);
    bad=x;bad.time_seconds=8;require(!refine_helper_evolving_frequency(bad).valid);
    bad=x;bad.time_seconds=4.8;require(!refine_helper_evolving_frequency(bad).valid);
    bad=x;bad.previous_only=false;require(!refine_helper_evolving_frequency(bad).valid);
    bad=x;bad.reference_preserved=false;require(!refine_helper_evolving_frequency(bad).valid);
    bad=x;bad.observed_thermal_sigma_hz=std::numeric_limits<double>::quiet_NaN();
    require(!refine_helper_evolving_frequency(bad).valid);
    HelperEvolvingFrequencyConfig c;c.additional_acceleration_bound_hz_per_second2=.1;
    const auto t=refine_helper_evolving_frequency(x,c);require(t.valid&&t.conditional_half_width_hz>r.conditional_half_width_hz);
    std::cout<<"helper_evolving_clock bounded short checks passed\n";
}
