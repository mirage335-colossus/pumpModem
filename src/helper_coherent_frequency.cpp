#include "datapump/helper_coherent_frequency.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace datapump::helper {
namespace {
using C=std::complex<double>;
constexpr double pi=3.1415926535897932384626433832795,tau=2*pi;
bool nonnegative(double x){return std::isfinite(x)&&x>=0;}
template<class F> double maximize(F&& f,double lo,double hi) {
    constexpr double g=.6180339887498948482;
    double x=hi-g*(hi-lo),y=lo+g*(hi-lo),fx=f(x),fy=f(y);
    for(unsigned i=0;i<26;++i) {
        if(fx>fy){hi=y;y=x;fy=fx;x=hi-g*(hi-lo);fx=f(x);}
        else {lo=x;x=y;fx=fy;y=lo+g*(hi-lo);fy=f(y);}
    }
    return (lo+hi)/2;
}
double variance(const std::array<std::array<double,2>,2>& covariance,double dt) {
    return covariance[0][0]+2*dt*covariance[0][1]+dt*dt*covariance[1][1];
}
}

HelperCoherentFrequency::HelperCoherentFrequency(HelperCoherentFrequencyConfig config):config_(config) {
    const HelperMeasurement geometry(config.measurement);(void)geometry;
    const auto& c=config.measurement;
    if(c.sample_rate<1000||c.high_search_max_hz-c.high_search_min_hz>16 ||
       std::abs(c.high_search_min_hz-c.high_hz)>32 || std::abs(c.high_search_max_hz-c.high_hz)>32 ||
       !std::isfinite(config.audio_center_ppm)||std::abs(config.audio_center_ppm)>1000 ||
       c.audio_error_bound_ppm>1000 || !nonnegative(config.rate_search_half_hz_per_second)||
       config.rate_search_half_hz_per_second>1 ||
       !std::isfinite(config.minimum_observation_seconds)||config.minimum_observation_seconds<=0 ||
       !std::isfinite(config.observation_begin_seconds)||
       !std::isfinite(config.minimum_sampled_fraction)||config.minimum_sampled_fraction<=0||config.minimum_sampled_fraction>1 ||
       !nonnegative(config.maximum_unobserved_seconds)||!nonnegative(config.additional_model_error_hz)||
       config.max_dwells<3||config.max_dwells>256)
        throw std::invalid_argument("invalid coherent no-FHSS Helper model");
}
void HelperCoherentFrequency::reset(){apertures_.clear();shift_hz_=0;cache_.reset();}

bool HelperCoherentFrequency::observe(std::span<const std::complex<float>> samples,double shift,double start) {
    const auto& c=config_.measurement;
    const double duration=static_cast<double>(samples.size())/c.sample_rate;
    if(samples.size()<32||duration<.1||duration>.4||!std::isfinite(start)||!std::isfinite(shift)||shift<=0||
       start<config_.observation_begin_seconds||!std::isfinite(start+duration)||
       apertures_.size()>=config_.max_dwells||
       (!apertures_.empty()&&(start<apertures_.back().end||shift!=shift_hz_)))return false;
    double power=0;
    for(const auto z:samples){if(!std::isfinite(z.real())||!std::isfinite(z.imag()))return false;power+=std::norm(C(z));}
    const auto group=std::max<std::size_t>(1,static_cast<std::size_t>(c.sample_rate/200));
    const auto count=samples.size()/group;
    Aperture a;a.start=start;a.end=start+duration;a.step=static_cast<double>(group)/c.sample_rate;
    a.first_center=start+static_cast<double>(group-1)/(2*c.sample_rate);
    a.noise=std::max(power/static_cast<double>(samples.size())/static_cast<double>(group),1e-30);
    a.high.reserve(count);a.low.reserve(count);
    const double scale=1+config_.audio_center_ppm*1e-6;
    // Preserve the absolute capture date in the downmix phase. Resetting this
    // phase for each aperture would silently destroy the coherent information.
    C hp=std::polar(1.,-tau*c.high_hz*scale*start),lp=std::polar(1.,-tau*c.low_hz*scale*start);
    const auto hs=std::polar(1.,-tau*c.high_hz*scale/c.sample_rate);
    const auto ls=std::polar(1.,-tau*c.low_hz*scale/c.sample_rate);
    for(std::size_t j=0;j<count;++j){
        C high=0,low=0;
        for(std::size_t k=0;k<group;++k){const C z(samples[j*group+k]);high+=z*hp;low+=z*lp;hp*=hs;lp*=ls;}
        a.high.push_back(high/static_cast<double>(group));a.low.push_back(low/static_cast<double>(group));
    }
    shift_hz_=shift;apertures_.push_back(std::move(a));cache_.reset();return true;
}

bool HelperCoherentFrequency::support(double until,HelperCoherentFrequencySnapshot& out) const {
    out.dwell_count=apertures_.size();
    if(!std::isfinite(until)||until<config_.observation_begin_seconds)return false;
    out.observation_seconds=until-config_.observation_begin_seconds;
    if(apertures_.size()<3||out.observation_seconds<config_.minimum_observation_seconds||until<apertures_.back().end)return false;
    double end=config_.observation_begin_seconds,gap=0;
    for(const auto& a:apertures_){out.sampled_seconds+=a.end-a.start;gap=std::max(gap,a.start-end);end=a.end;}
    gap=std::max(gap,until-end);
    return out.sampled_seconds+1e-9>=config_.minimum_sampled_fraction*out.observation_seconds &&
        gap<=config_.maximum_unobserved_seconds+1e-9;
}

HelperCoherentFrequencySnapshot HelperCoherentFrequency::snapshot(double until) const {
    HelperCoherentFrequencySnapshot out;
    if(!support(until,out))return out;
    if(cache_){auto result=*cache_;result.observation_seconds=out.observation_seconds;return result;}
    auto result=compute(until);cache_=result;return result;
}

HelperCoherentFrequencySnapshot HelperCoherentFrequency::compute(double until) const {
    HelperCoherentFrequencySnapshot out;if(!support(until,out))return out;
    const auto& c=config_.measurement;
    const double earliest=apertures_.front().first_center;
    const auto& last=apertures_.back();
    const double latest=last.first_center+static_cast<double>(last.high.size()-1)*last.step;
    const double reference=(earliest+latest)/2,span=latest-earliest;
    out.reference_time_seconds=reference;
    if(!(span>0))return out;
    const double high_center=c.high_hz*(1+config_.audio_center_ppm*1e-6);
    const double lower=c.high_search_min_hz-high_center,upper=c.high_search_max_hz-high_center;
    const double rate=config_.rate_search_half_hz_per_second;
    const auto nf=static_cast<std::size_t>(std::max(1.,std::ceil((upper-lower)*span/.75)));
    const auto nr=static_cast<std::size_t>(std::max(1.,std::ceil(2*rate*span*span)));
    std::size_t points=0;for(const auto& a:apertures_)points+=a.high.size()+a.low.size();
    const long double work=static_cast<long double>(points)*
        (static_cast<long double>(nf+1)*static_cast<long double>(nr+1)+700);
    out.projected_sample_products=work>static_cast<long double>(std::numeric_limits<std::size_t>::max())?
        std::numeric_limits<std::size_t>::max():static_cast<std::size_t>(work);
    if(work>100000000.L){out.work_limited=true;return out;}
    struct Fit {bool valid=false,boundary=false;double time=0,f=0,r=0,high=0,low=0;
        std::array<std::array<double,2>,2> covariance{};};
    const auto scores=[&](double f,double r,double ref,double first,double end,unsigned block=3) {
        C sums[2]{};double weights[2]{};
        for(const auto& a:apertures_){
            const double center=(a.start+a.end)/2;
            if(center<first||center>=end)continue;
            const double dt=a.first_center-ref,w=1/a.noise;
            C phase=std::polar(1.,-tau*(f*dt+.5*r*dt*dt));
            C step=std::polar(1.,-tau*(f*a.step+r*dt*a.step+.5*r*a.step*a.step));
            const C chirp=std::polar(1.,-tau*r*a.step*a.step);
            for(std::size_t i=0;i<a.high.size();++i){
                const double t=a.first_center+static_cast<double>(i)*a.step;
                if(block==3||static_cast<unsigned>(std::min(2.,std::floor((t-earliest)/(span/3))))==block){
                    sums[0]+=a.high[i]*phase*w;sums[1]+=a.low[i]*phase*w;weights[0]+=w;weights[1]+=w;
                }
                phase*=step;step*=chirp;
            }
        }
        return std::array<double,2>{weights[0]>0?std::norm(sums[0])/weights[0]:0,
                                    weights[1]>0?std::norm(sums[1])/weights[1]:0};
    };
    const auto fit=[&](double first,double end,bool global,const Fit& initial) {
        Fit result;double tmin=std::numeric_limits<double>::infinity(),tmax=-tmin;
        for(const auto& a:apertures_){const double mid=(a.start+a.end)/2;if(mid>=first&&mid<end){tmin=std::min(tmin,a.first_center);tmax=std::max(tmax,a.first_center+static_cast<double>(a.high.size()-1)*a.step);}}
        if(!std::isfinite(tmin)||tmax-tmin<5)return result;
        result.time=(tmin+tmax)/2;const double tspan=tmax-tmin;
        const auto objective=[&](double f,double r){const auto s=scores(f,r,result.time,first,end);return s[0]+s[1];};
        double value=-1;
        if(global){for(std::size_t i=0;i<=nf;++i)for(std::size_t j=0;j<=nr;++j){
            const double f=lower+(upper-lower)*static_cast<double>(i)/static_cast<double>(nf);
            const double r=-rate+2*rate*static_cast<double>(j)/static_cast<double>(nr);
            const double v=objective(f,r);if(v>value){value=v;result.f=f;result.r=r;}
        }}else {result.f=initial.f+initial.r*(result.time-initial.time);result.r=initial.r;}
        for(unsigned pass=0;pass<5;++pass){
            result.f=maximize([&](double f){return objective(f,result.r);},
                std::max(lower,result.f-1.2/tspan),std::min(upper,result.f+1.2/tspan));
            if(rate>0)result.r=maximize([&](double r){return objective(result.f,r);},
                std::max(-rate,result.r-2/(tspan*tspan)),std::min(rate,result.r+2/(tspan*tspan)));
        }
        const auto evidence=scores(result.f,result.r,result.time,first,end);result.high=evidence[0];result.low=evidence[1];
        const double df=1e-4,dr=1e-4/tspan;
        if(result.f-lower<df||upper-result.f<df||(rate>0&&(rate-std::abs(result.r)<dr))){result.boundary=true;return result;}
        const double center=objective(result.f,result.r);
        const double h00=-(objective(result.f+df,result.r)+objective(result.f-df,result.r)-2*center)/(df*df);
        if(!(h00>0)||!std::isfinite(h00))return result;
        if(rate>0){
            const double h11=-(objective(result.f,result.r+dr)+objective(result.f,result.r-dr)-2*center)/(dr*dr);
            const double h01=-(objective(result.f+df,result.r+dr)-objective(result.f+df,result.r-dr)-objective(result.f-df,result.r+dr)+objective(result.f-df,result.r-dr))/(4*df*dr);
            const double determinant=h00*h11-h01*h01;
            if(!(h11>0&&determinant>0)||!std::isfinite(determinant))return result;
            result.covariance={{{h11/determinant,-h01/determinant},{-h01/determinant,h00/determinant}}};
        }else result.covariance[0][0]=1/h00;
        // Common Brownian phase is correlated across both pilots. Propagate
        // it through the local phase-regression influence at the ACTUAL dates,
        // including gaps and unequal thermal weights. This is a small-phase
        // local approximation, not a calibrated bound on arbitrary phase
        // motion or on the profile likelihood's threshold behavior.
        const double q=c.phase_noise_radians_per_sqrt_second;
        if(q>0) {
            struct Point {double t=0,step=0,w=0;std::array<double,2> b{};};
            std::vector<Point> data;double total_w=0,mean0=0,mean1=0;
            for(const auto& a:apertures_) {
                const double aperture_center=(a.start+a.end)/2;if(aperture_center<first||aperture_center>=end)continue;
                for(std::size_t i=0;i<a.high.size();++i) {
                    const double t=a.first_center+static_cast<double>(i)*a.step,dt=t-result.time,w=1/a.noise;
                    data.push_back({t,a.step,w,{tau*dt,.5*tau*dt*dt}});
                    total_w+=w;mean0+=w*tau*dt;mean1+=w*.5*tau*dt*dt;
                }
            }
            if(!(total_w>0))return result;
            mean0/=total_w;mean1/=total_w;
            const double amplitude_power=std::max(0.,result.high+result.low-2)/total_w;
            double n00=0,n01=0,n11=0;
            for(auto& p:data){p.w*=2*amplitude_power;p.b[0]-=mean0;p.b[1]-=mean1;
                n00+=p.w*p.b[0]*p.b[0];n01+=p.w*p.b[0]*p.b[1];n11+=p.w*p.b[1]*p.b[1];}
            if(!(n00>0))return result;
            const double det=n00*n11-n01*n01;
            if(rate>0&&!(det>0))return result;
            for(auto& p:data){const auto d=p.b;
                p.b[0]=rate>0?p.w*(n11*d[0]-n01*d[1])/det:p.w*d[0]/n00;
                p.b[1]=rate>0?p.w*(-n01*d[0]+n00*d[1])/det:0;
            }
            std::array<double,2> tail{};
            for(std::size_t i=data.size();i-- >0;){
                for(std::size_t j=0;j<2;++j)tail[j]+=data[i].b[j];
                const double dt=i?data[i].t-data[i-1].t:0;
                for(std::size_t j=0;j<2;++j)for(std::size_t k=0;k<2;++k)
                    result.covariance[j][k]+=q*q*(dt*tail[j]*tail[k]+data[i].step/12*data[i].b[j]*data[i].b[k]);
            }
        }
        for(const auto& row:result.covariance)for(double v:row)if(!std::isfinite(v))return result;
        result.valid=true;return result;
    };
    const Fit full=fit(-std::numeric_limits<double>::infinity(),std::numeric_limits<double>::infinity(),true,{});
    out.offset_hz=full.f;out.rate_hz_per_second=full.r;out.reference_time_seconds=full.time;
    out.high_evidence=full.high;out.low_evidence=full.low;out.boundary_limited=full.boundary;
    const double trials=static_cast<double>((nf+1)*(nr+1));
    out.evidence_threshold=std::log(std::max(1.,trials*100.)/1e-6);
    out.detected=full.high>out.evidence_threshold&&full.low>out.evidence_threshold;
    if(!out.detected||!full.valid)return out;
    const Fit recent=fit(reference,std::numeric_limits<double>::infinity(),false,full);
    out.covariance=full.covariance;out.recent_time_seconds=recent.time;out.recent_offset_hz=recent.f;
    out.recent_rate_hz_per_second=recent.r;out.recent_covariance=recent.covariance;
    double split=0;for(unsigned b=0;b<3;++b){const auto s=scores(full.f,full.r,full.time,-std::numeric_limits<double>::infinity(),std::numeric_limits<double>::infinity(),b);split+=s[0]+s[1];}
    out.consistency_score=2*std::max(0.,split-full.high-full.low);
    constexpr double df=8,log_alpha=9.210340371976184;
    out.consistency_threshold=df+2*std::sqrt(df*log_alpha)+2*log_alpha;
    out.model_consistent=out.consistency_score<=out.consistency_threshold&&recent.valid;
    out.uncertainty_valid=out.model_consistent;
    return out;
}

HelperCoherentFrequencyEstimate HelperCoherentFrequency::predict(double nominal,double time,double until) const {
    HelperCoherentFrequencyEstimate out;out.time_seconds=time;
    if(!std::isfinite(nominal)||!std::isfinite(time))return out;
    const auto s=snapshot(until);if(!s.uncertainty_valid)return out;
    const double dt=time-s.reference_time_seconds,rdt=time-s.recent_time_seconds;
    const double v=variance(s.covariance,dt),rv=variance(s.recent_covariance,rdt);
    if(!nonnegative(v)||!nonnegative(rv))return out;
    const double mean=s.offset_hz+s.rate_hz_per_second*dt;
    const double recent=s.recent_offset_hz+s.recent_rate_hz_per_second*rdt;
    const auto& c=config_.measurement;
    const double audio=(std::abs(nominal)+std::max(std::abs(c.high_hz),std::abs(c.low_hz)))*c.audio_error_bound_ppm*1e-6;
    out.frequency_hz=nominal*(1+config_.audio_center_ppm*1e-6)+mean;
    out.statistical_3sigma_hz=3*std::sqrt(v);
    out.half_width_hz=std::max(out.statistical_3sigma_hz,std::abs(recent-mean)+3*std::sqrt(rv))+audio+config_.additional_model_error_hz;
    out.valid=std::isfinite(out.frequency_hz)&&std::isfinite(out.half_width_hz);
    return out;
}
}
