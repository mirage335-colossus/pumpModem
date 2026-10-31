#include "datapump/helper_dual_pilot_phase.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace datapump::helper {
namespace {
constexpr double tau=2*std::numbers::pi;
bool nonnegative(double x) {return std::isfinite(x)&&x>=0;}
double wrap(double x) {return std::remainder(x,tau);}
}

HelperDualPilotPhase::HelperDualPilotPhase(Config c):config_(c) {
    if(!std::isfinite(c.sigma_multiplier)||c.sigma_multiplier<=0||
       !std::isfinite(c.consistency_sigma_multiplier)||c.consistency_sigma_multiplier<=0||
       !std::isfinite(c.maximum_prediction_age_seconds)||c.maximum_prediction_age_seconds<=0||
       !std::isfinite(c.maximum_difference_support_radians)||c.maximum_difference_support_radians<=0||
       c.maximum_difference_support_radians>=std::numbers::pi||
       !std::isfinite(c.maximum_combined_half_width_radians)||c.maximum_combined_half_width_radians<=0||
       c.maximum_combined_half_width_radians>=std::numbers::pi||
       !std::isfinite(c.minimum_fractional_radius_improvement)||c.minimum_fractional_radius_improvement<0||
       c.minimum_fractional_radius_improvement>=1)
        throw std::invalid_argument("invalid Helper dual-pilot phase configuration");
}

void HelperDualPilotPhase::reset() noexcept {
    have_reference_=have_seen_=false;reference_=last_seen_=0;
}

HelperDualPilotPhaseResult HelperDualPilotPhase::observe(const HelperDualPilotPhaseInput& x,
                                                       const HelperDualPilotPhasePrediction& p) {
    HelperDualPilotPhaseResult out;
    out.time_seconds=x.time_seconds;
    out.reference_input_seconds=have_reference_?reference_:p.reference_input_seconds;
    out.reference_preserved=have_reference_;
    if(!std::isfinite(x.time_seconds))return out;
    if(have_seen_&&x.time_seconds<=last_seen_) {
        out.failure=HelperDualPilotPhaseFailure::nonmonotone_date;return out;
    }
    have_seen_=true;last_seen_=x.time_seconds;
    if(!x.valid||!std::isfinite(x.high_phase_radians)||!std::isfinite(x.low_phase_radians)||
       !nonnegative(x.common_phase_sigma_radians)||!nonnegative(x.high_model_half_width_radians)||
       !nonnegative(x.low_model_half_width_radians))return out;
    if(!p.valid||!p.previous_observations_only||!p.reference_preserved||p.alternatives!=1||
       !std::isfinite(p.time_seconds)||std::abs(p.time_seconds-x.time_seconds)>1e-9||
       !std::isfinite(p.latest_observation_seconds)||p.latest_observation_seconds>=x.time_seconds||
       x.time_seconds-p.latest_observation_seconds>config_.maximum_prediction_age_seconds||
       !std::isfinite(p.reference_input_seconds)||p.reference_input_seconds>p.latest_observation_seconds||
       !std::isfinite(p.high_minus_low_radians)||!nonnegative(p.thermal_sigma_radians)||
       !nonnegative(p.process_sigma_radians)||p.process_sigma_radians>std::numbers::pi||
       !nonnegative(p.model_half_width_radians)) {
        out.failure=HelperDualPilotPhaseFailure::unavailable_prediction;return out;
    }
    if(have_reference_&&std::abs(reference_-p.reference_input_seconds)>1e-9) {
        out.failure=HelperDualPilotPhaseFailure::reference_mismatch;return out;
    }
    const double vh=x.thermal_covariance[0][0],vl=x.thermal_covariance[1][1];
    const double ch=x.thermal_covariance[0][1],cl=x.thermal_covariance[1][0];
    const double vp=p.thermal_sigma_radians*p.thermal_sigma_radians;
    const double hp=p.current_prediction_covariance[0],lp=p.current_prediction_covariance[1];
    const double common=x.common_phase_sigma_radians*x.common_phase_sigma_radians;
    if(!nonnegative(vh)||!nonnegative(vl)||!nonnegative(vp)||!nonnegative(common)||
       vh>std::numbers::pi*std::numbers::pi||vl>std::numbers::pi*std::numbers::pi||
       vp>std::numbers::pi*std::numbers::pi||common>std::numbers::pi*std::numbers::pi||
       !std::isfinite(ch)||!std::isfinite(cl)||!std::isfinite(hp)||!std::isfinite(lp)) {
        out.failure=HelperDualPilotPhaseFailure::covariance;return out;
    }
    const double scale=std::max({vh,vl,vp,1e-30});
    const double tolerance=128*std::numeric_limits<double>::epsilon();
    // Normalize before principal-minor checks, avoiding under/overflow of a
    // valid small covariance. Semidefinite, including zero-noise, is allowed.
    const double a=vh/scale,b=vl/scale,c=vp/scale,d=(ch+cl)/(2*scale),e=hp/scale,f=lp/scale;
    const double determinant=a*b*c+2*d*e*f-a*f*f-b*e*e-c*d*d;
    if(std::abs(ch-cl)>tolerance*scale||a*b-d*d<-tolerance||a*c-e*e<-tolerance||
       b*c-f*f<-tolerance||determinant<-tolerance||!std::isfinite(determinant)) {
        out.failure=HelperDualPilotPhaseFailure::covariance;return out;
    }
    const double cov=(ch+cl)/2;
    const double residual_variance=std::max(0.,vh+vl+vp-2*cov-2*hp+2*lp);
    const double residual_bound=x.high_model_half_width_radians+x.low_model_half_width_radians+p.model_half_width_radians;
    out.difference_support_radians=config_.consistency_sigma_multiplier*
        (std::sqrt(residual_variance)+p.process_sigma_radians)+residual_bound;
    // Reduce separately so even finite, unnormalized input phases cannot
    // overflow while forming their difference.
    const double high_phase=wrap(x.high_phase_radians),low_phase=wrap(x.low_phase_radians);
    out.difference_residual_radians=wrap(high_phase-low_phase-wrap(p.high_minus_low_radians));
    if(!std::isfinite(out.difference_support_radians)||
       out.difference_support_radians>=config_.maximum_difference_support_radians) {
        out.failure=HelperDualPilotPhaseFailure::cycle_ambiguity;return out;
    }
    if(std::abs(out.difference_residual_radians)>out.difference_support_radians) {
        out.failure=HelperDualPilotPhaseFailure::inconsistent_difference;return out;
    }
    const double aligned_low_variance=vl+vp+2*lp;
    const double aligned_covariance=cov+hp;
    const double difference_variance=vh+aligned_low_variance-2*aligned_covariance;
    double low_weight=difference_variance>0?(vh-aligned_covariance)/difference_variance:0;
    low_weight=std::clamp(low_weight,0.,1.);
    const double high_weight=1-low_weight;
    const double combined_variance=std::max(0.,high_weight*high_weight*vh+
        low_weight*low_weight*aligned_low_variance+2*high_weight*low_weight*aligned_covariance);
    const double combined_bound=high_weight*x.high_model_half_width_radians+
        low_weight*(x.low_model_half_width_radians+p.model_half_width_radians);
    out.high_only_half_width_radians=config_.sigma_multiplier*(std::sqrt(vh)+x.common_phase_sigma_radians)+x.high_model_half_width_radians;
    out.thermal_sigma_radians=std::sqrt(combined_variance);
    out.common_sigma_radians=x.common_phase_sigma_radians;
    out.prediction_process_sigma_radians=low_weight*p.process_sigma_radians;
    out.model_half_width_radians=combined_bound;
    out.conditional_half_width_radians=config_.sigma_multiplier*(std::sqrt(combined_variance)+
        x.common_phase_sigma_radians+out.prediction_process_sigma_radians)+combined_bound;
    if(!std::isfinite(out.conditional_half_width_radians)||
       out.conditional_half_width_radians>=config_.maximum_combined_half_width_radians) {
        out.failure=HelperDualPilotPhaseFailure::uncertainty_too_large;return out;
    }
    if(low_weight<=0||out.conditional_half_width_radians>=out.high_only_half_width_radians*
        (1-config_.minimum_fractional_radius_improvement)) {
        out.failure=HelperDualPilotPhaseFailure::no_benefit;return out;
    }
    // The residual is the short circular path from aligned low to high. Its
    // sign is chosen so weight=0 preserves high and weight=1 uses aligned low.
    out.high_gauge_phase_radians=wrap(high_phase-low_weight*out.difference_residual_radians);
    out.low_weight=low_weight;out.valid=out.combined=true;out.failure=HelperDualPilotPhaseFailure::none;
    if(!have_reference_){have_reference_=true;reference_=p.reference_input_seconds;}
    out.reference_input_seconds=reference_;out.reference_preserved=true;
    return out;
}

} // namespace datapump::helper
