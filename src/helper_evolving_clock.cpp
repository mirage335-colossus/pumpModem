#include "datapump/helper_evolving_clock.hpp"
#include <algorithm>
#include <cmath>

namespace datapump::helper {
namespace { bool nonnegative(double x){return std::isfinite(x)&&x>=0;} }
HelperEvolvingFrequencyResult refine_helper_evolving_frequency(
    const HelperEvolvingFrequencyInput& x,const HelperEvolvingFrequencyConfig& c){
    HelperEvolvingFrequencyResult out;
    if(!x.valid||!x.reference_preserved||!x.previous_only||
       !std::isfinite(x.time_seconds)||!std::isfinite(x.previous_observation_seconds)||
       !std::isfinite(x.forecast_hz)||!std::isfinite(x.observed_hz)||
       !nonnegative(x.forecast_thermal_sigma_hz)||!nonnegative(x.forecast_process_sigma_hz)||
       !nonnegative(x.forecast_model_half_width_hz)||!nonnegative(x.forecast_conditional_half_width_hz)||
       !nonnegative(x.observed_thermal_sigma_hz)||!nonnegative(x.observed_process_sigma_hz)||
       !nonnegative(x.observed_model_half_width_hz)||!nonnegative(c.maximum_prediction_age_seconds)||
       !nonnegative(c.additional_acceleration_bound_hz_per_second2)||
       !nonnegative(c.consistency_sigma_multiplier)||!nonnegative(c.uncertainty_sigma_multiplier)||
       !nonnegative(c.maximum_observation_weight)||c.maximum_observation_weight>1)return out;
    const double age=x.time_seconds-x.previous_observation_seconds;
    if(!(age>0)||age>c.maximum_prediction_age_seconds)return out;
    const double evolution=.5*c.additional_acceleration_bound_hz_per_second2*age*age;
    // Retain any all/recent enclosure beyond the declared stochastic/model
    // forecast; neither the two overlapping fits nor prediction and new
    // process errors are treated as independent.
    const double pm=std::max(x.forecast_model_half_width_hz,
        x.forecast_conditional_half_width_hz-c.uncertainty_sigma_multiplier*
        (x.forecast_thermal_sigma_hz+x.forecast_process_sigma_hz))+evolution;
    const double sigma=x.forecast_thermal_sigma_hz+x.forecast_process_sigma_hz+
                       x.observed_thermal_sigma_hz+x.observed_process_sigma_hz;
    out.innovation_hz=x.observed_hz-x.forecast_hz;
    out.innovation_half_width_hz=c.consistency_sigma_multiplier*sigma+pm+x.observed_model_half_width_hz;
    if(!std::isfinite(out.innovation_half_width_hz)||std::abs(out.innovation_hz)>out.innovation_half_width_hz)return out;
    // The weight only selects a correction coordinate.  It is not used to
    // assert posterior independence or to re-ingest the forecast/current data.
    const double p=x.forecast_thermal_sigma_hz+x.forecast_process_sigma_hz;
    const double z=x.observed_thermal_sigma_hz+x.observed_process_sigma_hz;
    const double scale=std::max(p,z);
    const double pn=scale>0?p/scale:0,zn=scale>0?z/scale:0;
    const double w=pn*pn+zn*zn>0?std::min(c.maximum_observation_weight,pn*pn/(pn*pn+zn*zn)):0;
    out.observation_weight=w;out.frequency_hz=x.forecast_hz+w*out.innovation_hz;
    out.thermal_sigma_hz=(1-w)*x.forecast_thermal_sigma_hz+w*x.observed_thermal_sigma_hz;
    out.process_sigma_hz=(1-w)*x.forecast_process_sigma_hz+w*x.observed_process_sigma_hz;
    out.model_half_width_hz=(1-w)*pm+w*x.observed_model_half_width_hz;
    out.conditional_half_width_hz=c.uncertainty_sigma_multiplier*(out.thermal_sigma_hz+out.process_sigma_hz)+out.model_half_width_hz;
    out.valid=std::isfinite(out.frequency_hz)&&std::isfinite(out.conditional_half_width_hz);
    out.fallback_required=!out.valid;
    return out;
}
}
