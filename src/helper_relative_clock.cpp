#include "datapump/helper_relative_clock.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>
#include <vector>

namespace datapump::helper {
namespace {
constexpr double tau=2*std::numbers::pi;
using V=std::array<double,3>;
using M=std::array<V,3>;
bool nonnegative(double x) {return std::isfinite(x)&&x>=0;}
double dot(const V& a,const V& b) {double r=0;for(unsigned i=0;i<3;++i)r+=a[i]*b[i];return r;}
V multiply(const M& a,const V& b) {V r{};for(unsigned i=0;i<3;++i)r[i]=dot(a[i],b);return r;}
V design(double t,double center,bool drift) {const double u=t-center;return {1,u,drift?.5*u*u:0};}
bool invert(M a,M& inverse,unsigned count) {
    inverse={};for(unsigned i=0;i<count;++i)inverse[i][i]=1;
    for(unsigned col=0;col<count;++col) {
        unsigned row=col;for(unsigned k=col+1;k<count;++k)if(std::abs(a[k][col])>std::abs(a[row][col]))row=k;
        if(!std::isfinite(a[row][col])||std::abs(a[row][col])<1e-15)return false;
        std::swap(a[row],a[col]);std::swap(inverse[row],inverse[col]);
        const double divisor=a[col][col];
        for(unsigned j=0;j<count;++j){a[col][j]/=divisor;inverse[col][j]/=divisor;}
        for(unsigned k=0;k<count;++k)if(k!=col) {
            const double f=a[k][col];
            for(unsigned j=0;j<count;++j){a[k][j]-=f*a[col][j];inverse[k][j]-=f*inverse[col][j];}
        }
    }
    return true;
}
struct Point {double t=0,y=0,sigma=0,bound=0;};
struct Radius {double independent=0,support=0,bounded=0;};
struct Fit {
    bool valid=false,consistent=false;
    double center=0,scale=1,score=0;
    V beta{};M inverse{};
    std::vector<double> weight;
};
struct Candidate {std::vector<Point> points;Fit fit;};
double value(const Fit& fit,double t,bool drift) {return dot(design(t,fit.center,drift),fit.beta);}
Radius radius(const Candidate& c,const V& v,bool drift) {
    Radius r;
    const auto f=multiply(c.fit.inverse,v);
    for(std::size_t i=0;i<c.points.size();++i) {
        const double influence=dot(f,design(c.points[i].t,c.fit.center,drift))*c.fit.weight[i];
        const double z=influence*c.points[i].sigma;
        r.independent+=z*z;r.support+=std::abs(z);
        r.bounded+=std::abs(influence)*c.points[i].bound;
    }
    r.independent=std::sqrt(r.independent);return r;
}
Fit fit_points(const std::vector<Point>& points,const HelperRelativeClock::Config& cfg) {
    Fit fit;
    if(points.size()<cfg.minimum_fit_observations||points.back().t-points.front().t<cfg.minimum_fit_span_seconds)return fit;
    fit.center=(points.front().t+points.back().t)/2;
    double min_sigma=std::numeric_limits<double>::infinity();
    for(const auto& p:points)min_sigma=std::min(min_sigma,p.sigma);
    fit.scale=min_sigma;
    M normal{};V rhs{};fit.weight.reserve(points.size());
    for(const auto& p:points) {
        const double w=std::pow(min_sigma/p.sigma,2);fit.weight.push_back(w);
        const auto x=design(p.t,fit.center,cfg.fit_audio_drift);
        for(unsigned i=0;i<3;++i){rhs[i]+=w*x[i]*p.y;for(unsigned j=0;j<3;++j)normal[i][j]+=w*x[i]*x[j];}
    }
    if(!invert(normal,fit.inverse,cfg.fit_audio_drift?3U:2U))return fit;
    fit.beta=multiply(fit.inverse,rhs);fit.valid=true;
    Candidate c{points,fit};
    for(const auto& p:points) {
        const auto r=radius(c,design(p.t,fit.center,cfg.fit_audio_drift),cfg.fit_audio_drift);
        const double support=cfg.sigma_multiplier*(p.sigma+r.support)+p.bound+r.bounded+1e-12;
        fit.score=std::max(fit.score,std::abs(p.y-value(fit,p.t,cfg.fit_audio_drift))/support);
    }
    // Audio bounds constrain the complete observed span. A fit outside them
    // by more than its declared uncertainty cannot select a timing branch.
    for(const double t:{points.front().t,points.back().t}) {
        const V v{0,1,cfg.fit_audio_drift?t-fit.center:0};
        const auto r=radius(c,v,cfg.fit_audio_drift);
        const double half=cfg.sigma_multiplier*r.support+r.bounded;
        const double audio=1+dot(v,fit.beta);
        if(audio+half<cfg.minimum_audio_scale||audio-half>cfg.maximum_audio_scale)fit.score=std::numeric_limits<double>::infinity();
    }
    if(cfg.fit_audio_drift) {
        const auto r=radius(c,V{0,0,1},true);
        const double half=cfg.sigma_multiplier*r.support+r.bounded;
        if(std::abs(fit.beta[2])>cfg.maximum_audio_drift_ppm_per_second*1e-6+half)
            fit.score=std::numeric_limits<double>::infinity();
    }
    fit.consistent=std::isfinite(fit.score)&&fit.score<=1;return fit;
}
}

struct HelperRelativeClock::State {
    Config config;
    std::vector<Candidate> candidates;
    bool initialized=false,seen=false,coverage_lost=false,limited=false,work_limited=false;
    bool last_consistent=false;
    double reference=0,last_seen=0;
    std::size_t accepted=0,rejected=0;
    explicit State(Config c):config(c){}
};

HelperRelativeClock::HelperRelativeClock(Config c):state_(std::make_unique<State>(c)) {
    if(!std::isfinite(c.high_hz)||!std::isfinite(c.low_hz)||c.low_hz<=0||c.high_hz<=c.low_hz||
       !std::isfinite(c.minimum_audio_scale)||!std::isfinite(c.maximum_audio_scale)||
       c.minimum_audio_scale<=0||c.minimum_audio_scale>c.maximum_audio_scale||
       !nonnegative(c.maximum_audio_drift_ppm_per_second)||!nonnegative(c.differential_phase_half_width_radians)||
       !nonnegative(c.numerical_phase_half_width_radians)||!std::isfinite(c.sigma_multiplier)||c.sigma_multiplier<3||
       !std::isfinite(c.minimum_fit_span_seconds)||c.minimum_fit_span_seconds<=0||
       !std::isfinite(c.maximum_prediction_age_seconds)||c.maximum_prediction_age_seconds<=0||
       c.minimum_fit_observations<(c.fit_audio_drift?4U:3U)||c.maximum_observations<c.minimum_fit_observations||
       c.maximum_observations>128||!c.maximum_alternatives||c.maximum_alternatives>8||
       c.differential_phase_half_width_radians>=std::numbers::pi)
        throw std::invalid_argument("invalid relative Helper clock configuration");
}
HelperRelativeClock::~HelperRelativeClock()=default;
HelperRelativeClock::HelperRelativeClock(HelperRelativeClock&&) noexcept=default;
HelperRelativeClock& HelperRelativeClock::operator=(HelperRelativeClock&&) noexcept=default;
void HelperRelativeClock::reset() noexcept {
    auto& s=*state_;s.candidates.clear();s.initialized=s.seen=s.coverage_lost=s.limited=s.work_limited=s.last_consistent=false;
    s.reference=s.last_seen=0;s.accepted=s.rejected=0;
}

HelperRelativeClock::Estimate HelperRelativeClock::predict(double t) const {
    const auto& s=*state_;Estimate out;out.input_seconds=t;out.reference_input_seconds=s.reference;
    out.reference_initialized=s.initialized;out.reference_preserved=s.initialized&&!s.coverage_lost;
    out.ambiguity_limited=s.limited;out.work_limited=s.work_limited;out.alternatives=s.candidates.size();
    out.accepted_observations=s.accepted;out.rejected_observations=s.rejected;
    out.model_consistent=s.last_consistent;
    if(!std::isfinite(t)||s.coverage_lost||!s.last_consistent||s.work_limited||s.candidates.size()!=1)return out;
    const auto& c=s.candidates.front();const auto& fit=c.fit;
    if(!fit.valid||!fit.consistent||t<c.points.front().t-s.config.maximum_prediction_age_seconds||
       t>c.points.back().t+s.config.maximum_prediction_age_seconds)return out;
    const auto x=design(t,fit.center,s.config.fit_audio_drift),ref=design(s.reference,fit.center,s.config.fit_audio_drift);
    V delta{};for(unsigned i=0;i<3;++i)delta[i]=x[i]-ref[i];
    const auto r=radius(c,delta,s.config.fit_audio_drift);
    out.source_seconds=t+dot(delta,fit.beta);
    out.independent_source_sigma_seconds=r.independent;out.support_source_sigma_seconds=r.support;
    out.source_half_width_seconds=s.config.sigma_multiplier*r.support+r.bounded;
    const V rate{0,1,s.config.fit_audio_drift?t-fit.center:0};const auto rr=radius(c,rate,s.config.fit_audio_drift);
    out.audio_scale=1+dot(rate,fit.beta);out.audio_drift_ppm_per_second=fit.beta[2]*1e6;
    out.independent_audio_sigma_ppm=rr.independent*1e6;out.support_audio_sigma_ppm=rr.support*1e6;
    out.audio_half_width_ppm=(s.config.sigma_multiplier*rr.support+rr.bounded)*1e6;
    out.model_residual_score=fit.score;
    out.valid=std::isfinite(out.source_seconds)&&std::isfinite(out.source_half_width_seconds)&&
        std::isfinite(out.audio_scale)&&out.audio_scale>0&&std::isfinite(out.audio_half_width_ppm);
    return out;
}

HelperRelativeClock::Estimate HelperRelativeClock::observe(const Input& input) {
    auto& s=*state_;const auto& cfg=s.config;const double t=input.time_seconds;
    if(!std::isfinite(t)||(s.seen&&t<=s.last_seen)) {
        auto out=predict(t);out.valid=false;return out;
    }
    s.seen=true;s.last_seen=t;
    const double separation=cfg.high_hz-cfg.low_hz;
    if(s.coverage_lost||s.work_limited)return predict(t);
    if(!input.valid||!std::isfinite(input.high_phase_radians)||!std::isfinite(input.low_phase_radians)||
       !nonnegative(input.phase_difference_sigma_radians)||!nonnegative(input.phase_prediction_half_width_radians)) {
        ++s.rejected;auto out=predict(t);out.valid=false;return out;
    }
    const double sigma=std::max(input.phase_difference_sigma_radians/(tau*separation),1e-12);
    const double bound=(cfg.differential_phase_half_width_radians+cfg.numerical_phase_half_width_radians+
        input.phase_prediction_half_width_radians)/(tau*separation);
    if(cfg.sigma_multiplier*sigma+bound>=.5/separation) {
        ++s.rejected;auto out=predict(t);out.valid=false;return out;
    }
    const double residual=std::remainder(input.high_phase_radians-input.low_phase_radians-
        tau*std::remainder(separation*t,1.),tau)/(tau*separation);
    if(!s.initialized) {
        s.initialized=true;s.reference=t;s.accepted=1;s.last_consistent=true;
        Candidate first;first.points.push_back({t,residual,sigma,bound});s.candidates.push_back(std::move(first));return predict(t);
    }
    std::vector<Candidate> next;next.reserve(cfg.maximum_alternatives);
    bool overflow=false;
    for(const auto& previous:s.candidates) {
        if(previous.points.size()>=cfg.maximum_observations){s.work_limited=true;break;}
        const auto& p=previous.points.back();const double dt=t-p.t;
        double lower=p.y+(cfg.minimum_audio_scale-1)*dt-cfg.sigma_multiplier*(p.sigma+sigma)-p.bound-bound;
        double upper=p.y+(cfg.maximum_audio_scale-1)*dt+cfg.sigma_multiplier*(p.sigma+sigma)+p.bound+bound;
        if(previous.fit.valid&&previous.fit.consistent) {
            const double center=value(previous.fit,t,cfg.fit_audio_drift);
            const auto r=radius(previous,design(t,previous.fit.center,cfg.fit_audio_drift),cfg.fit_audio_drift);
            const double half=cfg.sigma_multiplier*(r.support+sigma)+r.bounded+bound;
            lower=std::max(lower,center-half);upper=std::min(upper,center+half);
        }
        const long double first=std::ceil(static_cast<long double>(separation)*(lower-residual));
        const long double last=std::floor(static_cast<long double>(separation)*(upper-residual));
        if(!std::isfinite(first)||!std::isfinite(last)||first>last)continue;
        if(last-first+1>static_cast<long double>(cfg.maximum_alternatives)){overflow=true;break;}
        for(long double branch=first;branch<=last;branch+=1) {
            Candidate candidate=previous;
            candidate.points.push_back({t,residual+static_cast<double>(branch)/separation,sigma,bound});
            candidate.fit=fit_points(candidate.points,cfg);
            if(candidate.fit.valid&&!candidate.fit.consistent)continue;
            if(next.size()>=cfg.maximum_alternatives){overflow=true;break;}
            next.push_back(std::move(candidate));
        }
        if(overflow)break;
    }
    if(overflow) {
        s.limited=s.coverage_lost=true;++s.rejected;auto out=predict(t);out.valid=false;return out;
    }
    if(s.work_limited) {++s.rejected;auto out=predict(t);out.valid=false;return out;}
    if(next.empty()) {
        ++s.rejected;s.last_consistent=false;auto out=predict(t);out.valid=false;return out;
    }
    s.candidates=std::move(next);++s.accepted;s.last_consistent=true;return predict(t);
}
} // namespace datapump::helper
