#include "datapump/helper_boundary_tracker.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace datapump::helper {
namespace {
constexpr double tau=2*std::numbers::pi;
double envelope(double x){x=std::clamp(x,0.,1.);return x*x*x*(10+x*(-15+6*x));}
bool finite(std::complex<double> z){return std::isfinite(z.real())&&std::isfinite(z.imag());}
}
HelperBoundaryTracker::HelperBoundaryTracker(Config c):config_(c){
    if(!std::isfinite(c.sample_rate)||c.sample_rate<=0||
       !std::isfinite(c.search_half_width_seconds)||c.search_half_width_seconds<=0||
       !std::isfinite(c.grid_seconds)||c.grid_seconds<=0||
       !std::isfinite(c.ramp_seconds)||c.ramp_seconds<=0||
       c.retained_boundaries==0||c.retained_boundaries>maximum_boundaries)
        throw std::invalid_argument("invalid Helper boundary tracker config");
    const double ratio=c.search_half_width_seconds/c.grid_seconds;
    const double half=std::round(ratio);
    if(!std::isfinite(ratio)||half<1||half>(maximum_candidates-1)/2||
       std::abs(ratio-half)>1e-9)
        throw std::invalid_argument("Helper boundary search requires bounded integral grid");
    grid_count_=static_cast<std::size_t>(2*half+1);
}
HelperBoundaryTracker::Pilot HelperBoundaryTracker::settled_pilot(
    std::span<const std::complex<float>> x,double first,double fs,double f,double ps,double f_sigma){
    Pilot p;
    if(x.size()<8||x.size()>24000||!std::isfinite(first)||!std::isfinite(fs)||fs<=0||
       !std::isfinite(f)||std::abs(f)>=fs/2||!std::isfinite(ps)||ps<0||
       !std::isfinite(f_sigma)||f_sigma<0)return p;
    p.anchor_sample=first+static_cast<double>(x.size()-1)/2;
    p.frequency_hz=f;p.phase_sigma_radians=ps;p.frequency_sigma_hz=f_sigma;
    std::complex<double> z{};double weight=0;
    const auto step=std::polar(1.,-tau*f/fs);
    auto mixer=std::polar(1.,tau*f*static_cast<double>(x.size()-1)/(2*fs));
    for(std::size_t i=0;i<x.size();++i){
        if(!finite(x[i]))return {};
        const double w=.5-.5*std::cos(tau*static_cast<double>(i)/static_cast<double>(x.size()-1));
        z+=w*std::complex<double>(x[i])*mixer;weight+=w;mixer*=step;
    }
    p.amplitude_at_anchor=z/weight;p.valid=finite(p.amplitude_at_anchor)&&std::abs(p.amplitude_at_anchor)>0;
    return p;
}
HelperBoundaryTracker::Result HelperBoundaryTracker::observe(const Input& in){
    Result out;const double fs=config_.sample_rate,step=config_.grid_seconds*fs;
    const double half=step*static_cast<double>(grid_count_/2),ramp=config_.ramp_seconds*fs/in.audio_scale;
    if(!std::isfinite(in.first_sample)||!std::isfinite(in.predicted_boundary_sample)||
       !std::isfinite(in.audio_scale)||std::abs(in.audio_scale-1)>.001||
       !std::isfinite(in.latest_observation_end_sample)||in.samples.empty()||in.samples.size()>maximum_edge_samples||
       in.first_sample>in.predicted_boundary_sample-half-ramp||
       in.first_sample+static_cast<double>(in.samples.size())<in.predicted_boundary_sample+half+ramp||
       (have_last_&&(in.predicted_boundary_sample<=last_boundary_||in.first_sample<last_edge_end_)))return out;
    const double edge_end=in.first_sample+static_cast<double>(in.samples.size());
    if(!std::isfinite(edge_end)||in.latest_observation_end_sample<edge_end||
       in.before.observation_end_sample>in.first_sample||in.after.observation_first_sample<edge_end)return out;
    for(const auto sample:in.samples)if(!finite(sample))return out;
    // Prediction uncertainty only downweights a row; it does not shrink the
    // retained prior or claim that shared templates are independent evidence.
    double maximum_phase_variance=0;
    for(const auto* hop:{&in.before,&in.after}){
      if(!std::isfinite(hop->observation_first_sample)||!std::isfinite(hop->observation_end_sample)||
         hop->observation_first_sample>=hop->observation_end_sample||
         hop->observation_end_sample>in.latest_observation_end_sample)return out;
      for(const auto& p:hop->pilots){
        if(!p.valid||!std::isfinite(p.anchor_sample)||!std::isfinite(p.frequency_hz)||
           std::abs(p.frequency_hz)>=fs/2||!finite(p.amplitude_at_anchor)||
           !std::isfinite(std::norm(p.amplitude_at_anchor))||
           p.anchor_sample<hop->observation_first_sample||p.anchor_sample>=hop->observation_end_sample||
           !std::isfinite(p.phase_sigma_radians)||p.phase_sigma_radians<0||
           !std::isfinite(p.frequency_sigma_hz)||p.frequency_sigma_hz<0)return out;
        const double transport=std::abs(in.predicted_boundary_sample-p.anchor_sample)/fs;
        const double sigma=p.phase_sigma_radians+tau*transport*p.frequency_sigma_hz;
        if(!std::isfinite(sigma))return out;
        maximum_phase_variance=std::max(maximum_phase_variance,sigma*sigma);
      }
    }
    if(maximum_phase_variance>1)return out;
    const double weight=1/(1+maximum_phase_variance);
    std::array<std::complex<double>,maximum_edge_samples> before{},after{};
    for(std::size_t side=0;side<2;++side){const auto& hop=side?in.after:in.before;auto& model=side?after:before;
        for(const auto& p:hop.pilots){
            const double phase=tau*p.frequency_hz*(in.first_sample-p.anchor_sample)/fs;
            if(!std::isfinite(phase))return out;
            auto value=p.amplitude_at_anchor*std::polar(1.,phase);
            const auto increment=std::polar(1.,tau*p.frequency_hz/fs);
            for(std::size_t i=0;i<in.samples.size();++i){model[i]+=value;value*=increment;}}
    }
    std::array<double,maximum_candidates> row{};
    for(std::size_t k=0;k<grid_count_;++k){const double boundary=in.predicted_boundary_sample+(static_cast<double>(k)-static_cast<double>(grid_count_/2))*step;
        double score=0;
        for(std::size_t i=0;i<in.samples.size();++i){
            const double d=in.first_sample+static_cast<double>(i)-boundary;
            const auto model=d<0?before[i]*envelope(-d/ramp):after[i]*envelope(d/ramp);
            score+=2*std::real(std::complex<double>(in.samples[i])*std::conj(model))-std::norm(model);
        }
        row[k]=weight*score;
        if(!std::isfinite(row[k]))return out;
    }
    // Every row uses the same unknown schedule epoch offset. Rows can share
    // their settled templates but not their edge samples. We use their sum
    // solely for a bounded point estimate, never as an information matrix.
    for(std::size_t k=0;k<grid_count_;++k){
        const double previous=count_==config_.retained_boundaries?scores_[head_][k]:0;
        if(!std::isfinite(total_[k]-previous+row[k]))return out;
    }
    for(std::size_t k=0;k<grid_count_;++k){
        if(count_==config_.retained_boundaries)total_[k]-=scores_[head_][k];
        scores_[head_][k]=row[k];total_[k]+=row[k];
    }
    head_=(head_+1)%config_.retained_boundaries;count_=std::min(count_+1,config_.retained_boundaries);
    have_last_=true;last_boundary_=in.predicted_boundary_sample;last_edge_end_=edge_end;
    const auto best=static_cast<std::size_t>(std::max_element(total_.begin(),total_.begin()+static_cast<std::ptrdiff_t>(grid_count_))-total_.begin());
    out.available=true;out.epoch_offset_samples=(static_cast<double>(best)-static_cast<double>(grid_count_/2))*step;
    out.boundary_sample=in.predicted_boundary_sample+out.epoch_offset_samples;
    out.prior_first_sample=in.predicted_boundary_sample-half;out.prior_end_sample=in.predicted_boundary_sample+half;
    out.latest_observation_end_sample=in.latest_observation_end_sample;out.boundaries=count_;out.candidates=grid_count_;
    out.score_improvement=total_[best]-total_[grid_count_/2];out.sample_products=grid_count_*in.samples.size();
    return out;
}
}
