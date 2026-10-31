#include "datapump/helper_frequency_recovery.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace datapump::helper {
namespace {
bool finite(double x){return std::isfinite(x);}
bool nonnegative(double x){return finite(x)&&x>=0;}
struct Point {
    double t=0,first=0,end=0,residual=0,thermal_variance=0,process_variance=0,prior_radius=0;
};
struct Fit {
    bool valid=false,consistent=false;
    double value=0,thermal=0,process=0,stochastic=0,model=0,radius=0,score=0;
};
Fit fit(const std::vector<Point>& points,double multiplier){
    Fit out;double precision=0,rhs=0;
    for(const auto& p:points){const double v=p.thermal_variance+p.process_variance;
        if(!(v>0)||!finite(v))return out;
        const double w=1/v;
        if(!finite(w)||!finite(precision+w)||!finite(rhs+w*p.residual))return out;
        precision+=w;rhs+=w*p.residual;}
    if(!(precision>0))return out;
    out.value=rhs/precision;
    double tv=0,pv=0;
    for(const auto& p:points){const double v=p.thermal_variance+p.process_variance,a=(1/v)/precision;
        tv+=a*a*p.thermal_variance;pv+=a*a*p.process_variance;
        // The old model is shared across these candidates. Its allowance is
        // transported with an L1 influence bound, never divided by sqrt(N).
        out.model+=std::abs(a)*p.prior_radius;}
    for(const auto& p:points){const double v=p.thermal_variance+p.process_variance;
        const double residual=std::max(0.,std::abs(p.residual-out.value)-p.prior_radius-out.model);
        out.score=std::max(out.score,residual/(multiplier*std::sqrt(v)+1e-12));}
    out.thermal=std::sqrt(tv);out.process=std::sqrt(pv);out.stochastic=std::sqrt(tv+pv);
    out.radius=multiplier*out.stochastic+out.model;
    out.valid=finite(out.value)&&finite(out.radius)&&finite(out.score);out.consistent=out.valid&&out.score<=1;
    return out;
}
}

struct HelperFrequencyRecovery::State {
    explicit State(Config c):config(c){}
    Config config;bool have_reference=false,reference_lost=false,seen=false,have_baseline=false,work_limited=false;
    double reference=0,last_seen=0,baseline_begin=0;std::size_t baseline_count=0,accepted=0,rejected=0;
    Point baseline;std::vector<Point> candidates;Result latest;
};
HelperFrequencyRecovery::HelperFrequencyRecovery(Config c):state_(std::make_unique<State>(c)){
    if(!finite(c.sigma_multiplier)||c.sigma_multiplier<=0||!finite(c.minimum_step_hz)||c.minimum_step_hz<=0||
       !finite(c.maximum_step_hz)||c.maximum_step_hz<=c.minimum_step_hz||!finite(c.maximum_gap_seconds)||c.maximum_gap_seconds<=0||
       !finite(c.maximum_candidate_span_seconds)||c.maximum_candidate_span_seconds<=0||!finite(c.maximum_prediction_age_seconds)||c.maximum_prediction_age_seconds<=0||
       !finite(c.minimum_baseline_span_seconds)||c.minimum_baseline_span_seconds<0||
       !finite(c.minimum_confirmation_span_seconds)||c.minimum_confirmation_span_seconds<0||c.minimum_baseline_observations<2||
       c.provisional_observations<2||c.confirmation_observations<c.provisional_observations||c.maximum_observations<c.confirmation_observations||c.maximum_observations>32)
        throw std::invalid_argument("invalid Helper frequency recovery bounds");
}
HelperFrequencyRecovery::~HelperFrequencyRecovery()=default;
HelperFrequencyRecovery::HelperFrequencyRecovery(HelperFrequencyRecovery&&) noexcept=default;
HelperFrequencyRecovery& HelperFrequencyRecovery::operator=(HelperFrequencyRecovery&&) noexcept=default;
void HelperFrequencyRecovery::reset() noexcept{const auto c=state_->config;*state_=State(c);}
HelperFrequencyRecovery::Result HelperFrequencyRecovery::result() const{return state_->latest;}

HelperFrequencyRecovery::Result HelperFrequencyRecovery::observe(const Input& x){
    auto& s=*state_;const auto& c=s.config;Result out;
    const auto metadata=[&](){out.reference_preserved=s.have_reference&&!s.reference_lost;out.reference_changed=s.reference_lost;
        out.reference_input_seconds=s.reference;out.time_seconds=x.time_seconds;out.accepted_observations=s.accepted;
        out.rejected_observations=s.rejected;out.candidate_observations=s.candidates.size();out.work_limited=s.work_limited;};
    const auto reject=[&](bool break_sequence){++s.rejected;if(break_sequence){s.candidates.clear();s.baseline_count=0;s.have_baseline=false;}
        metadata();s.latest=out;return out;};
    if(!finite(x.time_seconds)||(s.seen&&x.time_seconds<=s.last_seen))return reject(false);
    s.seen=true;s.last_seen=x.time_seconds;
    const bool reference_known=x.reference_status_known||x.reference_preserved;
    if(reference_known&&!x.reference_preserved){if(s.have_reference)s.reference_lost=true;return reject(true);}
    if(!x.valid||!x.prediction_valid)return reject(true);
    if(!reference_known||!x.reference_preserved||!finite(x.reference_input_seconds))return reject(true);
    if(!s.have_reference){s.have_reference=true;s.reference=x.reference_input_seconds;}
    else if(x.reference_input_seconds!=s.reference){s.reference_lost=true;return reject(true);}
    if(s.reference_lost||s.work_limited)return reject(true);
    if(!finite(x.first_sample_seconds)||!finite(x.end_sample_seconds)||x.first_sample_seconds>x.time_seconds||x.end_sample_seconds<x.time_seconds||
       x.end_sample_seconds-x.first_sample_seconds<.1-1e-8||x.end_sample_seconds-x.first_sample_seconds>1+1e-8||!finite(x.high_frequency_hz)||!finite(x.low_frequency_hz)||
       !finite(x.predicted_high_hz)||!finite(x.predicted_low_hz)||!finite(x.prediction_last_observation_seconds)||
       x.prediction_last_observation_seconds>=x.first_sample_seconds||x.time_seconds-x.prediction_last_observation_seconds>c.maximum_prediction_age_seconds||
       !nonnegative(x.common_frequency_sigma_hz)||!nonnegative(x.prediction_high_sigma_hz)||!nonnegative(x.prediction_low_sigma_hz)||
       !nonnegative(x.prediction_high_model_half_width_hz)||!nonnegative(x.prediction_low_model_half_width_hz)||!nonnegative(x.current_frequency_model_half_width_hz))return reject(true);
    if(x.frequency_dates_valid&&(!finite(x.high_frequency_time_seconds)||!finite(x.low_frequency_time_seconds)||
       x.high_frequency_time_seconds<x.first_sample_seconds||x.high_frequency_time_seconds>x.end_sample_seconds||
       x.low_frequency_time_seconds<x.first_sample_seconds||x.low_frequency_time_seconds>x.end_sample_seconds))return reject(true);
    const auto& q=x.thermal_covariance;
    for(const auto& row:q)for(double v:row)if(!finite(v))return reject(true);
    if(!(q[0][0]>0)||!(q[1][1]>0)||std::abs(q[0][1]-q[1][0])>1e-9*std::sqrt(q[0][0]*q[1][1]))return reject(true);
    const double determinant=q[0][0]*q[1][1]-q[0][1]*q[1][0],common=x.common_frequency_sigma_hz*x.common_frequency_sigma_hz;
    if(!finite(determinant)||!(determinant>0)||!finite(common))return reject(true);
    std::array<std::array<double,2>,2> process{{{common,common},{common,common}}};
    if(x.process_covariance_valid){process=x.process_covariance;
        for(const auto& row:process)for(double v:row)if(!finite(v))return reject(true);
        if(process[0][0]<0||process[1][1]<0||std::abs(process[0][1]-process[1][0])>
           1e-9*std::max({std::sqrt(process[0][0]*process[1][1]),std::abs(process[0][1]),1e-300}))return reject(true);
        const double product=process[0][0]*process[1][1],cross=process[0][1]*process[1][0];
        if(!finite(product)||!finite(cross)||product-cross < -1e-10*std::max({product,std::abs(cross),1e-300}))return reject(true);
    }
    const double a=q[0][0]+process[0][0],b=.5*(q[0][1]+q[1][0]+process[0][1]+process[1][0]),d=q[1][1]+process[1][1];
    const double denominator=a+d-2*b;
    if(!(denominator>0)||!finite(denominator))return reject(true);
    const double wh=(d-b)/denominator,wl=1-wh;
    const double rh=x.high_frequency_hz-x.predicted_high_hz,rl=x.low_frequency_hz-x.predicted_low_hz;
    const double difference_variance=q[0][0]+q[1][1]-q[0][1]-q[1][0]+process[0][0]+process[1][1]-process[0][1]-process[1][0];
    const double difference_radius=c.sigma_multiplier*(std::sqrt(std::max(0.,difference_variance))+x.prediction_high_sigma_hz+x.prediction_low_sigma_hz)+
        x.prediction_high_model_half_width_hz+x.prediction_low_model_half_width_hz+2*x.current_frequency_model_half_width_hz;
    if(!finite(rh)||!finite(rl)||!finite(difference_radius)||std::abs(rh-rl)>difference_radius)return reject(true);
    Point p;p.t=x.time_seconds;p.first=x.first_sample_seconds;p.end=x.end_sample_seconds;p.residual=wh*rh+wl*rl;
    p.thermal_variance=wh*wh*q[0][0]+2*wh*wl*q[0][1]+wl*wl*q[1][1];
    p.process_variance=wh*wh*process[0][0]+wh*wl*(process[0][1]+process[1][0])+wl*wl*process[1][1];
    p.prior_radius=c.sigma_multiplier*(std::abs(wh)*x.prediction_high_sigma_hz+std::abs(wl)*x.prediction_low_sigma_hz)+
        std::abs(wh)*x.prediction_high_model_half_width_hz+std::abs(wl)*x.prediction_low_model_half_width_hz+
        (std::abs(wh)+std::abs(wl))*x.current_frequency_model_half_width_hz;
    const double point_radius=c.sigma_multiplier*std::sqrt(p.thermal_variance+p.process_variance)+p.prior_radius;
    if(!finite(p.residual)||!nonnegative(p.thermal_variance)||p.thermal_variance<=0||!finite(p.prior_radius)||!finite(point_radius)||
       std::abs(p.residual)>c.maximum_step_hz+point_radius)return reject(true);
    if(std::abs(p.residual)<=point_radius){
        ++s.accepted;out.input_accepted=true;
        out.compatible=true;
        if(s.have_baseline&&p.first-s.baseline.end<=c.maximum_gap_seconds&&p.first>=s.baseline.end)++s.baseline_count;
        else {s.baseline_count=1;s.baseline_begin=p.t;}
        s.baseline=p;s.have_baseline=true;s.candidates.clear();metadata();s.latest=out;return out;
    }
    if(!s.have_baseline||s.baseline_count<c.minimum_baseline_observations||s.baseline.t-s.baseline_begin<c.minimum_baseline_span_seconds||p.first<s.baseline.end||p.first-s.baseline.end>c.maximum_candidate_span_seconds)return reject(true);
    if(!s.candidates.empty()&&(p.first<s.candidates.back().end||p.first-s.candidates.back().end>c.maximum_gap_seconds||
       p.t-s.candidates.front().t>c.maximum_candidate_span_seconds||p.residual*s.candidates.front().residual<=0)){
        // A failed sequence cannot silently borrow its old compatible point
        // across an unbounded gap or a changed-sign transient.
        return reject(true);
    }
    if(s.candidates.size()>=c.maximum_observations){s.work_limited=true;return reject(false);}
    s.candidates.push_back(p);const auto f=fit(s.candidates,c.sigma_multiplier);
    if(!f.valid||!f.consistent)return reject(true);
    ++s.accepted;out.input_accepted=true;
    out.additive_correction_hz=f.value;out.thermal_sigma_hz=f.thermal;out.process_sigma_hz=f.process;out.stochastic_sigma_hz=f.stochastic;
    out.model_half_width_hz=f.model;out.conditional_half_width_hz=f.radius;out.fit_score=f.score;
    out.last_compatible_aperture_end_seconds=s.baseline.end;out.first_contrary_aperture_start_seconds=s.candidates.front().first;
    out.change_lower_seconds=s.baseline.first;out.change_upper_seconds=s.candidates.front().end;
    out.first_candidate_time_seconds=s.candidates.front().t;out.last_candidate_time_seconds=s.candidates.back().t;
    const double baseline_radius=c.sigma_multiplier*std::sqrt(s.baseline.thermal_variance+s.baseline.process_variance)+s.baseline.prior_radius;
    const bool localized_jump=std::abs(f.value-s.baseline.residual)>c.minimum_step_hz+f.radius+baseline_radius;
    const bool nonzero=std::abs(f.value)>c.minimum_step_hz+f.radius&&std::abs(f.value)+f.radius<=c.maximum_step_hz;
    out.provisional=localized_jump&&nonzero&&s.candidates.size()>=c.provisional_observations;
    out.epoch_requested=out.provisional&&s.candidates.size()>=c.confirmation_observations&&
        s.candidates.back().t-s.candidates.front().t>=c.minimum_confirmation_span_seconds;
    metadata();s.latest=out;return out;
}
}
