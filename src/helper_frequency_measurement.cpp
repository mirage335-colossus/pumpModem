#include "datapump/helper_frequency_measurement.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace datapump::helper {
namespace {
using Complex=std::complex<double>;
constexpr double pi=3.1415926535897932384626433832795;
constexpr double tau=2*pi;

template<class Function>
double maximize(Function&& f,double lower,double upper) {
    if(lower==upper)return lower;
    constexpr double g=.6180339887498948482;
    double x=upper-g*(upper-lower),y=lower+g*(upper-lower);
    double fx=f(x),fy=f(y);
    for(int i=0;i<27;++i) {
        if(fx>fy){upper=y;y=x;fy=fx;x=upper-g*(upper-lower);fx=f(x);}
        else {lower=x;x=y;fx=fy;y=lower+g*(upper-lower);fy=f(y);}
    }
    return (lower+upper)/2;
}
double energy(const std::vector<Complex>& data,double step,double hz,double noise) {
    Complex sum=0,phase=1;const auto advance=std::polar(1.,-tau*hz*step);
    for(const auto z:data){sum+=z*phase;phase*=advance;}
    return std::norm(sum)/(static_cast<double>(data.size())*noise);
}
bool finite_nonnegative(double value){return std::isfinite(value)&&value>=0;}
}

HelperFrequencyMeasurement::HelperFrequencyMeasurement(HelperFrequencyConfig config):config_(config) {
    // Share the established capture-geometry checks, not its four-state model.
    const HelperMeasurement geometry(config.measurement);(void)geometry;
    const auto& c=config.measurement;
    if(c.high_search_max_hz-c.high_search_min_hz>64 ||
       !std::isfinite(config.reference_shift_hz)||config.reference_shift_hz<=0 ||
       !std::isfinite(config.audio_center_ppm)||std::abs(config.audio_center_ppm)>1000 ||
       c.audio_error_bound_ppm>1000 ||
       !finite_nonnegative(config.frequency_rate_search_half_hz_per_second) ||
       config.frequency_rate_search_half_hz_per_second>1 ||
       (config.fit_frequency_rate&&config.frequency_rate_search_half_hz_per_second==0) ||
       !finite_nonnegative(config.maximum_residual_rate_hz_per_second) ||
       !finite_nonnegative(config.maximum_residual_acceleration_hz_per_second2) ||
       !std::isfinite(config.minimum_observation_seconds)||config.minimum_observation_seconds<=0 ||
       !std::isfinite(config.observation_begin_seconds)||
       !std::isfinite(config.minimum_sampled_fraction)||config.minimum_sampled_fraction<=0||config.minimum_sampled_fraction>1 ||
       !finite_nonnegative(config.maximum_unobserved_seconds)||
       config.max_dwells<3||config.max_dwells>256)
        throw std::invalid_argument("invalid reduced Helper frequency model");
}

void HelperFrequencyMeasurement::reset(){apertures_.clear();cache_.reset();full_influence_={};recent_influence_={};}

bool HelperFrequencyMeasurement::observe(std::span<const std::complex<float>> samples,double shift,double start) {
    const auto& c=config_.measurement;
    const double duration=static_cast<double>(samples.size())/c.sample_rate;
    if(samples.size()<32||duration<.1||duration>.4||!std::isfinite(start)||!std::isfinite(shift)||shift<=0||
       apertures_.size()>=config_.max_dwells||start<config_.observation_begin_seconds||
       !std::isfinite(start+duration)||(!apertures_.empty()&&start<apertures_.back().end))return false;
    double raw_power=0;
    for(const auto z:samples){if(!std::isfinite(z.real())||!std::isfinite(z.imag()))return false;raw_power+=std::norm(Complex(z));}
    const auto group=std::max<std::size_t>(1,static_cast<std::size_t>(c.sample_rate/256));
    const auto count=samples.size()/group;
    Aperture a;a.start=start;a.end=start+duration;a.shift_hz=shift;
    a.step=static_cast<double>(group)/c.sample_rate;
    a.center=start+(static_cast<double>(count*group)-1)/(2*c.sample_rate);
    a.noise=std::max(raw_power/static_cast<double>(samples.size())/static_cast<double>(group),1e-30);
    a.high.reserve(count);a.low.reserve(count);
    const double audio_scale=1+config_.audio_center_ppm*1e-6;
    Complex hp=1,lp=1;const auto hs=std::polar(1.,-tau*c.high_hz*audio_scale/c.sample_rate);
    const auto ls=std::polar(1.,-tau*c.low_hz*audio_scale/c.sample_rate);
    for(std::size_t j=0;j<count;++j){
        Complex high=0,low=0;
        for(std::size_t k=0;k<group;++k){const Complex z(samples[j*group+k]);high+=z*hp;low+=z*lp;hp*=hs;lp*=ls;}
        a.high.push_back(high/static_cast<double>(group));a.low.push_back(low/static_cast<double>(group));
    }
    apertures_.push_back(std::move(a));cache_.reset();full_influence_={};recent_influence_={};return true;
}

bool HelperFrequencyMeasurement::support(double until,HelperFrequencySnapshot& out) const {
    out.dwell_count=apertures_.size();
    if(!std::isfinite(until)||until<config_.observation_begin_seconds)return false;
    out.observation_seconds=until-config_.observation_begin_seconds;
    if(apertures_.size()<3||out.observation_seconds<config_.minimum_observation_seconds||until<apertures_.back().end)return false;
    double end=config_.observation_begin_seconds,gap=0;
    for(const auto& a:apertures_){out.sampled_seconds+=a.end-a.start;gap=std::max(gap,a.start-end);end=a.end;}
    gap=std::max(gap,until-end);
    const double tolerance=64*std::numeric_limits<double>::epsilon()*std::max(1.,std::abs(until));
    return out.sampled_seconds+tolerance>=config_.minimum_sampled_fraction*out.observation_seconds&&
        gap<=config_.maximum_unobserved_seconds+tolerance;
}

HelperFrequencySnapshot HelperFrequencyMeasurement::snapshot(double until) const {
    HelperFrequencySnapshot s;if(!support(until,s))return s;
    if(!cache_)cache_=compute(until);
    auto result=*cache_;result.observation_seconds=s.observation_seconds;result.sampled_seconds=s.sampled_seconds;return result;
}

HelperFrequencySnapshot HelperFrequencyMeasurement::compute(double until) const {
    HelperFrequencySnapshot out;if(!support(until,out))return out;
    const auto& c=config_.measurement;
    out.earliest_sample_seconds=apertures_.front().start;out.latest_sample_seconds=apertures_.back().end;
    out.reference_time_seconds=(apertures_.front().center+apertures_.back().center)/2;
    const double span=apertures_.back().center-apertures_.front().center;if(!(span>0))return out;
    const double scale=1+config_.audio_center_ppm*1e-6;
    const double lo=c.high_search_min_hz-c.high_hz*scale,hi=c.high_search_max_hz-c.high_hz*scale;
    const double rate_bound=config_.fit_frequency_rate?config_.frequency_rate_search_half_hz_per_second:0;
    std::size_t retained=0;
    for(const auto& a:apertures_){
        retained+=a.high.size()+a.low.size();
        const double envelope=std::abs(a.shift_hz/config_.reference_shift_hz)*
            (std::max(std::abs(lo),std::abs(hi))+rate_bound*std::abs(a.center-out.reference_time_seconds))+
            std::max(std::abs(c.high_hz),std::abs(c.low_hz))*c.audio_error_bound_ppm*1e-6;
        if(!std::isfinite(envelope)||envelope>=.4/a.step)return out;
    }
    using Point=std::array<double,2>;
    const auto scores=[&](const Point& p,std::size_t begin,std::size_t end){
        std::array<double,2> result{};
        for(std::size_t i=begin;i<end;++i){const auto& a=apertures_[i];
            const double frequency=a.shift_hz/config_.reference_shift_hz*(p[0]+p[1]*(a.center-out.reference_time_seconds));
            result[0]+=energy(a.high,a.step,frequency,a.noise);result[1]+=energy(a.low,a.step,frequency,a.noise);
        }return result;
    };
    const auto objective=[&](const Point& p){const auto e=scores(p,0,apertures_.size());return e[0]+e[1];};
    const auto steps=[](double width,double spacing){const double n=std::ceil(width/spacing);
        return !std::isfinite(n)||n>100000?-1:std::max(1,static_cast<int>(n));};
    const int nf=steps(hi-lo,.5),nr=config_.fit_frequency_rate?steps(2*rate_bound,.5/span):0;
    const double candidates=static_cast<double>(nf+1)*static_cast<double>(nr+1);
    if(nf<0||nr<0||candidates>100000||static_cast<double>(retained)*(candidates+1536)>64000000){out.work_limited=true;return out;}
    Point best{};double best_score=-1;
    for(int i=0;i<=nf;++i)for(int j=0;j<=nr;++j){
        Point p{lo+(hi-lo)*static_cast<double>(i)/nf,nr?(-rate_bound+2*rate_bound*static_cast<double>(j)/nr):0};
        const double value=objective(p);if(value>best_score){best_score=value;best=p;}
    }
    for(int pass=0;pass<4;++pass){
        best[0]=maximize([&](double f){return objective({f,best[1]});},std::max(lo,best[0]-.75),std::min(hi,best[0]+.75));
        if(config_.fit_frequency_rate)best[1]=maximize([&](double rate){return objective({best[0],rate});},
            std::max(-rate_bound,best[1]-.75/span),std::min(rate_bound,best[1]+.75/span));
    }
    out.offset_hz=best[0];out.rate_hz_per_second=best[1];const auto e=scores(best,0,apertures_.size());
    out.high_evidence=e[0];out.low_evidence=e[1];
    const double n=static_cast<double>(apertures_.size());
    const double log_trials=std::log(std::max(1e6,candidates*1000.)/1e-6);
    out.evidence_threshold=n+std::sqrt(2*n*log_trials)+log_trials;
    if(e[0]<=out.evidence_threshold||e[1]<=out.evidence_threshold)return out;
    out.detected=true;
    constexpr double df=.01;const double dr=.01/span;
    if(best[0]-lo<df||hi-best[0]<df||(config_.fit_frequency_rate&&rate_bound-std::abs(best[1])<dr)){
        out.boundary_limited=true;return out;
    }
    const double center=objective(best);
    const double hff=-(objective({best[0]+df,best[1]})+objective({best[0]-df,best[1]})-2*center)/(df*df);
    if(!(hff>0)||!std::isfinite(hff))return out;
    if(config_.fit_frequency_rate){
        const double hrr=-(objective({best[0],best[1]+dr})+objective({best[0],best[1]-dr})-2*center)/(dr*dr);
        const double hfr=-(objective({best[0]+df,best[1]+dr})-objective({best[0]+df,best[1]-dr})-
            objective({best[0]-df,best[1]+dr})+objective({best[0]-df,best[1]-dr}))/(4*df*dr);
        const double determinant=hff*hrr-hfr*hfr;
        if(!(hrr>0)||!(determinant>0)||!std::isfinite(determinant))return out;
        out.covariance={{{hrr/determinant,-hfr/determinant},{-hfr/determinant,hff/determinant}}};
    }else out.covariance[0][0]=1/hff;
    // A common Wiener phase slope contributes independently per aperture.
    // The covariance is an engineering linearization, not calibrated coverage.
    const double q=c.phase_noise_radians_per_sqrt_second;
    if(q>0){
        double a00=0,a01=0,a11=0;
        for(const auto& a:apertures_){const double duration=static_cast<double>(a.high.size())*a.step;
            const double variance=3*q*q/(10*pi*pi*duration),r=a.shift_hz/config_.reference_shift_hz;
            const double dt=a.center-out.reference_time_seconds,w=r*r/variance;
            a00+=w;a01+=w*dt;a11+=w*dt*dt;
        }
        if(config_.fit_frequency_rate){const double determinant=a00*a11-a01*a01;if(!(determinant>0))return out;
            out.covariance[0][0]+=a11/determinant;out.covariance[0][1]-=a01/determinant;
            out.covariance[1][0]-=a01/determinant;out.covariance[1][1]+=a00/determinant;
        }else out.covariance[0][0]+=1/a00;
    }
    // Estimated positive local information defines the influence of each
    // aperture in the reduced linearized fit. Propagating external evolution
    // and audio bounds uses its absolute influence rather than assuming all
    // RF commands or all fitted weights are equal. Like the covariance, this
    // is conditional on the local likelihood model, not a deterministic bound
    // on a nonlinear estimator under an arbitrary violated signal model.
    const auto make_influence=[&](Influence& info,bool linear,double reference,
                                 const auto& score,std::size_t begin,std::size_t end,const Point& point){
        info={};info.linear=linear;info.reference_time=reference;info.weights.resize(apertures_.size());
        for(std::size_t i=begin;i<end;++i){
            const double center_i=score(point,i,i+1);
            const double curvature=-(score({point[0]+df,point[1]},i,i+1)+
                score({point[0]-df,point[1]},i,i+1)-2*center_i)/(df*df);
            const double weight=std::max(0.,curvature),dt=apertures_[i].center-reference;
            info.weights[i]=weight;info.normal00+=weight;info.normal01+=weight*dt;info.normal11+=weight*dt*dt;
        }
        const double determinant=info.normal00*info.normal11-info.normal01*info.normal01;
        return std::isfinite(info.normal00)&&std::isfinite(info.normal01)&&std::isfinite(info.normal11)&&
            info.normal00>0&&(!linear||(std::isfinite(determinant)&&determinant>0));
    };
    const auto score_range=[&](const Point& point,std::size_t begin,std::size_t end){const auto value=scores(point,begin,end);return value[0]+value[1];};
    if(!make_influence(full_influence_,config_.fit_frequency_rate,out.reference_time_seconds,
                       score_range,0,apertures_.size(),best))return out;
    for(const auto& row:out.covariance)for(double value:row)if(!std::isfinite(value))return out;
    out.conditional_uncertainty_valid=true;

    // Fit offset AND rate over the most recent half independently of the
    // tightly supplied slow-evolution bound. This costs sensitivity but prevents
    // a narrow external prior from masquerading as measured stability.
    const double last=apertures_.back().center;
    std::size_t recent_begin=0;
    while(recent_begin<apertures_.size()&&apertures_[recent_begin].center<last-span/2)++recent_begin;
    const double recent_span=last-apertures_[recent_begin].center;
    if(apertures_.size()-recent_begin>=6&&recent_span>0){
        const auto recent_score=[&](const Point& point,std::size_t begin,std::size_t end){
            double result=0;
            for(std::size_t i=begin;i<end;++i){const auto& a=apertures_[i];
                const double f=a.shift_hz/config_.reference_shift_hz*(point[0]+point[1]*(a.center-last));
                result+=energy(a.high,a.step,f,a.noise)+energy(a.low,a.step,f,a.noise);
            }return result;
        };
        const auto local=[&](const Point& p){return recent_score(p,recent_begin,apertures_.size());};
        const Point expected{best[0]+best[1]*(last-out.reference_time_seconds),best[1]};
        Point recent=expected;double value=-1;
        for(int i=-4;i<=4;++i)for(int j=-4;j<=4;++j){
            const Point p{expected[0]+static_cast<double>(i)*.25,expected[1]+static_cast<double>(j)*.025};
            const double v=local(p);if(v>value){value=v;recent=p;}
        }
        for(int pass=0;pass<4;++pass){
            recent[0]=maximize([&](double f){return local({f,recent[1]});},expected[0]-1,expected[0]+1);
            recent[1]=maximize([&](double r){return local({recent[0],r});},expected[1]-.1,expected[1]+.1);
        }
        const double local_dr=.01/recent_span,center_recent=local(recent);
        const double aa=-(local({recent[0]+df,recent[1]})+local({recent[0]-df,recent[1]})-2*center_recent)/(df*df);
        const double bb=-(local({recent[0],recent[1]+local_dr})+local({recent[0],recent[1]-local_dr})-2*center_recent)/(local_dr*local_dr);
        const double ab=-(local({recent[0]+df,recent[1]+local_dr})-local({recent[0]+df,recent[1]-local_dr})-
            local({recent[0]-df,recent[1]+local_dr})+local({recent[0]-df,recent[1]-local_dr}))/(4*df*local_dr);
        const double determinant=aa*bb-ab*ab;
        if(aa>0&&bb>0&&determinant>0&&std::isfinite(determinant)&&
           std::abs(recent[0]-expected[0])<.98&&std::abs(recent[1]-expected[1])<.098&&
           make_influence(recent_influence_,true,last,recent_score,recent_begin,apertures_.size(),recent)){
            out.recent_model_valid=true;out.recent_time_seconds=last;out.recent_offset_hz=recent[0];out.recent_rate_hz_per_second=recent[1];
            out.recent_covariance={{{bb/determinant,-ab/determinant},{-ab/determinant,aa/determinant}}};
            if(q>0){
                double p00=0,p01=0,p11=0;
                for(std::size_t i=recent_begin;i<apertures_.size();++i){const auto& a=apertures_[i];
                    const double duration=static_cast<double>(a.high.size())*a.step;
                    const double variance=3*q*q/(10*pi*pi*duration),ratio=a.shift_hz/config_.reference_shift_hz;
                    const double dt=a.center-last,weight=ratio*ratio/variance;
                    p00+=weight;p01+=weight*dt;p11+=weight*dt*dt;
                }
                const double d=p00*p11-p01*p01;
                if(!(d>0))out.recent_model_valid=false;
                else {out.recent_covariance[0][0]+=p11/d;out.recent_covariance[0][1]-=p01/d;
                    out.recent_covariance[1][0]-=p01/d;out.recent_covariance[1][1]+=p00/d;}
            }
        }
    }
    // Falsifying observation check: separately profile each third's common
    // frequency after the fitted rate. Disagreement can veto precision, but
    // passing this low-SNR check does not establish the externally declared
    // rate/acceleration bounds. Those always remain in predict().
    std::array<double,3> residual{},sigma{},date{};bool consistent=true;
    for(std::size_t block=0;block<3;++block){
        const std::size_t begin=apertures_.size()*block/3,end=apertures_.size()*(block+1)/3;
        date[block]=(apertures_[begin].center+apertures_[end-1].center)/2;
        const auto local=[&](double shift){const auto energy_pair=scores({best[0]+shift,best[1]},begin,end);return energy_pair[0]+energy_pair[1];};
        residual[block]=maximize(local,-1,1);
        if(std::abs(residual[block])>.98){consistent=false;break;}
        const double curvature=-(local(residual[block]+df)+local(residual[block]-df)-2*local(residual[block]))/(df*df);
        if(!(curvature>0)||!std::isfinite(curvature)){consistent=false;break;}sigma[block]=1/std::sqrt(curvature);
    }
    out.consistency_threshold=3;
    if(consistent){
        const double declared_change=config_.fit_frequency_rate?
            config_.maximum_residual_acceleration_hz_per_second2*span*span:
            config_.maximum_residual_rate_hz_per_second*(date[2]-date[0]);
        const double contrast=std::abs(residual[2]-residual[0]);
        const double noise=std::hypot(sigma[2],sigma[0]);
        const double score=std::max(0.,contrast-declared_change)/std::max(noise,1e-12);
        const double curvature=std::abs(residual[0]-2*residual[1]+residual[2]);
        const double curvature_noise=std::sqrt(sigma[0]*sigma[0]+4*sigma[1]*sigma[1]+sigma[2]*sigma[2]);
        const double curvature_bound=config_.maximum_residual_acceleration_hz_per_second2*span*span;
        out.consistency_score=std::max(score,std::max(0.,curvature-curvature_bound)/std::max(curvature_noise,1e-12));
        consistent=out.consistency_score<=out.consistency_threshold;
    }
    out.model_consistent=consistent;
    for(const auto& row:out.covariance)for(double value:row)if(!std::isfinite(value))return out;
    for(const auto& row:out.recent_covariance)for(double v:row)if(!std::isfinite(v))out.recent_model_valid=false;
    // A direct contradiction invalidates even the conditional interval. The
    // point/covariance fields remain diagnostics, never a usable narrow lock.
    out.conditional_uncertainty_valid=consistent;
    out.uncertainty_valid=consistent&&out.recent_model_valid;return out;
}

HelperFrequencyEstimate HelperFrequencyMeasurement::predict(double nominal,double shift,double date,double until) const {
    HelperFrequencyEstimate result;result.time_seconds=date;
    if(!std::isfinite(nominal)||!std::isfinite(shift)||shift<=0||!std::isfinite(date))return result;
    const auto state=snapshot(until);if(!state.detected||!state.conditional_uncertainty_valid)return result;
    const double dt=date-state.reference_time_seconds,r=shift/config_.reference_shift_hz;
    const double variance=r*r*(state.covariance[0][0]+2*dt*state.covariance[0][1]+dt*dt*state.covariance[1][1]);
    if(!finite_nonnegative(variance))return result;
    result.frequency_hz=nominal*(1+config_.audio_center_ppm*1e-6)+r*(state.offset_hz+state.rate_hz_per_second*dt);
    result.statistical_3sigma_hz=3*std::sqrt(variance);
    const auto& c=config_.measurement;
    const auto allowance=[&](const Influence& influence){
        double total=std::abs(nominal)*c.audio_error_bound_ppm*1e-6;
        const double dt_query=date-influence.reference_time;
        const double determinant=influence.normal00*influence.normal11-influence.normal01*influence.normal01;
        for(std::size_t i=0;i<apertures_.size();++i){
            const double dt_i=apertures_[i].center-influence.reference_time;
            const double coefficient=influence.linear?
                influence.weights[i]*(influence.normal11-influence.normal01*dt_query+
                    (influence.normal00*dt_query-influence.normal01)*dt_i)/determinant:
                influence.weights[i]/influence.normal00;
            const double distance=std::abs(date-apertures_[i].center);
            const double evolution=config_.fit_frequency_rate?
                .5*config_.maximum_residual_acceleration_hz_per_second2*distance*distance:
                config_.maximum_residual_rate_hz_per_second*distance;
            const double observed_ratio=apertures_[i].shift_hz/config_.reference_shift_hz;
            const double audio=std::max(std::abs(c.high_hz),std::abs(c.low_hz))*c.audio_error_bound_ppm*1e-6/observed_ratio;
            total+=std::abs(r*coefficient)*(evolution+audio);
        }return total;
    };
    result.bounded_model_error_hz=allowance(full_influence_);
    result.conditional_half_width_hz=result.statistical_3sigma_hz+result.bounded_model_error_hz;
    result.conditional_valid=std::isfinite(result.frequency_hz)&&finite_nonnegative(result.conditional_half_width_hz);
    result.half_width_hz=std::numeric_limits<double>::quiet_NaN();
    if(!result.conditional_valid||!state.uncertainty_valid)return result;
    const double recent_dt=date-state.recent_time_seconds;
    const double recent_frequency=nominal*(1+config_.audio_center_ppm*1e-6)+r*(state.recent_offset_hz+state.recent_rate_hz_per_second*recent_dt);
    const double recent_variance=r*r*(state.recent_covariance[0][0]+2*recent_dt*state.recent_covariance[0][1]+
        recent_dt*recent_dt*state.recent_covariance[1][1]);
    if(!finite_nonnegative(recent_variance))return result;
    result.half_width_hz=std::max(result.conditional_half_width_hz,std::abs(result.frequency_hz-recent_frequency)+
        3*std::sqrt(recent_variance)+allowance(recent_influence_));
    result.valid=finite_nonnegative(result.half_width_hz);return result;
}
}
