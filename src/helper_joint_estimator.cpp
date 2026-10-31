#include "datapump/helper_joint_estimator.hpp"

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
using V=std::array<double,6>;
using M=std::array<V,6>;
using Q=std::array<std::array<double,3>,3>;
double dot(const V& a,const V& b){double v=0;for(std::size_t i=0;i<6;++i)v+=a[i]*b[i];return v;}
V mult(const M& m,const V& a){V v{};for(std::size_t i=0;i<6;++i)v[i]=dot(m[i],a);return v;}
double variance(const M& m,const V& a){return std::max(0.,dot(a,mult(m,a)));}
bool finite(double x){return std::isfinite(x);}
bool nonnegative(double x){return finite(x)&&x>=0;}
double wrap(double x){return std::remainder(x,tau);}

bool positive_semidefinite(const Q& input){
    Q a{};std::array<double,3> scale{};
    for(std::size_t i=0;i<3;++i){
        if(!finite(input[i][i])||input[i][i]<0)return false;
        scale[i]=std::sqrt(input[i][i]);
    }
    for(std::size_t i=0;i<3;++i)for(std::size_t j=0;j<3;++j){
        if(!finite(input[i][j]))return false;
        const double tolerance=1e-10*std::max({scale[i]*scale[j],std::abs(input[i][j]),std::abs(input[j][i]),1e-300});
        if(std::abs(input[i][j]-input[j][i])>tolerance)return false;
        // A zero variance forces its entire covariance row/column to zero.
        if(scale[i]==0||scale[j]==0){if(input[i][j]!=0)return false;a[i][j]=i==j?1:0;}
        else {a[i][j]=input[i][j]/scale[i]/scale[j];if(!finite(a[i][j]))return false;}
    }
    // Normalize each physical coordinate, rather than by one global scale:
    // Hz and phase variances may have very different magnitudes. A process
    // block may legitimately be singular; the separate thermal block makes
    // the total stochastic covariance positive definite.
    for(unsigned pass=0;pass<24;++pass){
        std::size_t p=0,q=1;
        for(std::size_t i=0;i<3;++i)for(std::size_t j=i+1;j<3;++j)
            if(std::abs(a[i][j])>std::abs(a[p][q])){p=i;q=j;}
        if(std::abs(a[p][q])<1e-14)break;
        const double angle=.5*std::atan2(2*a[p][q],a[q][q]-a[p][p]);
        const double cs=std::cos(angle),sn=std::sin(angle);
        const double pp=cs*cs*a[p][p]-2*cs*sn*a[p][q]+sn*sn*a[q][q];
        const double qq=sn*sn*a[p][p]+2*cs*sn*a[p][q]+cs*cs*a[q][q];
        for(std::size_t k=0;k<3;++k)if(k!=p&&k!=q){
            const double x=a[k][p],y=a[k][q];
            a[k][p]=a[p][k]=cs*x-sn*y;a[k][q]=a[q][k]=sn*x+cs*y;
        }
        a[p][p]=pp;a[q][q]=qq;a[p][q]=a[q][p]=0;
    }
    for(std::size_t i=0;i<3;++i)if(a[i][i]<-1e-11)return false;
    return true;
}

Q process_covariance(const HelperJointEstimator::Input& input){
    if(input.process_covariance_valid)return input.process_covariance;
    Q out{};const double v=input.common_frequency_sigma_hz*input.common_frequency_sigma_hz;
    for(std::size_t i=0;i<2;++i)for(std::size_t j=0;j<2;++j)out[i][j]=v;
    return out;
}

template<std::size_t N>
bool inverse(const std::array<std::array<double,N>,N>& input,
             std::array<std::array<double,N>,N>& output){
    // Normalize the coordinates before inversion. Frequency, phase and clock
    // drift parameters have different units; unscaled pivot size is not an
    // identifiability test. Positive pivots of the symmetric Schur complement
    // reject indefinite supplied covariance blocks rather than discarding the
    // caller's cross terms.
    std::array<double,N> scale{};
    std::array<std::array<double,2*N>,N> a{};
    for(std::size_t i=0;i<N;++i){
        if(!finite(input[i][i])||input[i][i]<=0)return false;
        scale[i]=std::sqrt(input[i][i]);
    }
    for(std::size_t i=0;i<N;++i)for(std::size_t j=0;j<N;++j){
        if(!finite(input[i][j])||std::abs(input[i][j]-input[j][i])>
            1e-9*std::max({scale[i]*scale[j],std::abs(input[i][j]),1e-300}))return false;
        a[i][j]=input[i][j]/scale[i]/scale[j];
        a[i][j+N]=i==j?1:0;
    }
    // Cholesky is only a definiteness check; Gauss-Jordan below obtains the
    // inverse without squaring a condition number.
    std::array<std::array<double,N>,N> l{};
    for(std::size_t i=0;i<N;++i)for(std::size_t j=0;j<=i;++j){
        double v=a[i][j];for(std::size_t k=0;k<j;++k)v-=l[i][k]*l[j][k];
        if(i==j){if(!finite(v)||v<=1e-12)return false;l[i][j]=std::sqrt(v);}
        else l[i][j]=v/l[j][j];
    }
    for(std::size_t i=0;i<N;++i){
        std::size_t pivot=i;for(std::size_t j=i+1;j<N;++j)if(std::abs(a[j][i])>std::abs(a[pivot][i]))pivot=j;
        if(!finite(a[pivot][i])||std::abs(a[pivot][i])<1e-12)return false;
        std::swap(a[pivot],a[i]);const double divisor=a[i][i];
        for(double& v:a[i])v/=divisor;
        for(std::size_t j=0;j<N;++j)if(j!=i){const double factor=a[j][i];
            for(std::size_t k=0;k<2*N;++k)a[j][k]-=factor*a[i][k];}
    }
    for(std::size_t i=0;i<N;++i)for(std::size_t j=0;j<N;++j){
        output[i][j]=a[i][j+N]/scale[i]/scale[j];if(!finite(output[i][j]))return false;
    }
    return true;
}

struct Step {bool active=false;HelperJointEstimator::FrequencyStepModel model=HelperJointEstimator::FrequencyStepModel::common_hz;double after=0,before=0;};
struct Point {HelperJointEstimator::Input input;Q precision{},process{};double phase_residual=0;};
std::pair<std::size_t,double> post_step_support(const std::vector<Point>& points,const Step& step){
    std::size_t count=0;double first=0,last=0;
    for(const auto& p:points)if(p.input.sample_support_valid&&p.input.first_sample_time_seconds>=step.before){
        if(!count)first=p.input.time_seconds;
        last=p.input.time_seconds;++count;
    }
    return {count,count?last-first:0};
}
struct Fit {bool valid=false,consistent=false;double center=0,inflation=1,score=0;V beta{};M covariance{},thermal_covariance{},process_covariance{};std::size_t begin=0;};
using Rows=std::array<V,3>;
Rows rows(const HelperJointEstimator::Config& c,const Point& p,double center,const Step& step){
    const double dt=p.input.frequency_time_seconds-center;
    const double rf=p.input.shift_hz/c.reference_shift_hz;
    const double low_dt=(p.input.separate_low_frequency_time?p.input.low_frequency_time_seconds:p.input.frequency_time_seconds)-center;
    const double phase_dt=p.input.time_seconds-center;
    const double jump=step.active&&p.input.frequency_time_seconds>=step.before?(step.model==HelperJointEstimator::FrequencyStepModel::common_hz?1:rf):0;
    const double low_jump=step.active&&(p.input.separate_low_frequency_time?p.input.low_frequency_time_seconds:p.input.frequency_time_seconds)>=step.before?(step.model==HelperJointEstimator::FrequencyStepModel::common_hz?1:rf):0;
    const double k=tau*(c.high_hz-c.low_hz)*1e-6;
    return {{{c.high_hz*1e-6,rf,c.high_hz*1e-6*dt,rf*dt,0,jump},
             {c.low_hz*1e-6,rf,c.low_hz*1e-6*low_dt,rf*low_dt,0,low_jump},
             {k*phase_dt,0,.5*k*phase_dt*phase_dt,0,k,0}}};
}
std::array<double,3> values(const HelperJointEstimator::Config& c,const Point& p){
    return {p.input.high_frequency_hz-c.high_hz,p.input.low_frequency_hz-c.low_hz,p.phase_residual};
}
std::array<double,3> bounds(const HelperJointEstimator::Config& c,const Point& p){
    return {p.input.frequency_model_half_width_hz,p.input.frequency_model_half_width_hz,
        c.differential_phase_half_width_radians+c.numerical_phase_half_width_radians+p.input.phase_model_half_width_radians};
}
Fit fit(const HelperJointEstimator::Config& c,const std::vector<Point>& points,std::size_t begin,const Step& step){
    Fit out;out.begin=begin;
    if(points.size()-begin<c.minimum_fit_observations||points.back().input.time_seconds-points[begin].input.time_seconds<c.minimum_fit_span_seconds)return out;
    out.center=points.back().input.time_seconds;M normal{};V rhs{};
    if(!step.active)normal[5][5]=1;
    for(std::size_t n=begin;n<points.size();++n){const auto& p=points[n];const auto x=rows(c,p,out.center,step);const auto y=values(c,p);
        for(std::size_t i=0;i<6;++i)for(std::size_t r=0;r<3;++r)for(std::size_t s=0;s<3;++s){
            rhs[i]+=x[r][i]*p.precision[r][s]*y[s];
            for(std::size_t j=0;j<6;++j)normal[i][j]+=x[r][i]*p.precision[r][s]*x[s][j];
        }
    }
    if(!inverse(normal,out.covariance))return out;
    out.beta=mult(out.covariance,rhs);
    for(double x:out.beta)if(!finite(x))return out;
    // GLS weights use total stochastic covariance, but retain separate
    // sandwich contributions for thermal noise and the declared common
    // oscillator process. No process term is relabeled as thermal noise.
    for(std::size_t n=begin;n<points.size();++n){const auto& p=points[n];const auto x=rows(c,p,out.center,step);
        std::array<V,3> influence{};
        for(std::size_t s=0;s<3;++s){V weighted{};for(std::size_t r=0;r<3;++r)for(std::size_t j=0;j<6;++j)weighted[j]+=x[r][j]*p.precision[r][s];influence[s]=mult(out.covariance,weighted);}

        for(std::size_t i=0;i<6;++i)for(std::size_t j=0;j<6;++j)for(std::size_t r=0;r<3;++r)for(std::size_t s=0;s<3;++s){
            out.thermal_covariance[i][j]+=influence[r][i]*p.input.thermal_covariance[r][s]*influence[s][j];
            out.process_covariance[i][j]+=influence[r][i]*p.process[r][s]*influence[s][j];
        }
    }
    double chi=0;
    for(std::size_t n=begin;n<points.size();++n){const auto& p=points[n];const auto x=rows(c,p,out.center,step);const auto y=values(c,p);const auto b=bounds(c,p);std::array<double,3> residual{};
        for(std::size_t r=0;r<3;++r){const double error=y[r]-dot(x[r],out.beta);residual[r]=std::copysign(std::max(0.,std::abs(error)-b[r]),error);}
        double contribution=0;for(std::size_t r=0;r<3;++r)for(std::size_t s=0;s<3;++s)contribution+=residual[r]*p.precision[r][s]*residual[s];
        if(!finite(contribution)||contribution< -1e-8)return out;
        chi+=std::max(0.,contribution);
    }
    const double degrees=static_cast<double>(3*(points.size()-begin)-(step.active?6:5));
    out.inflation=std::max(1.,chi/degrees);
    const double threshold=degrees+c.consistency_sigma_multiplier*std::sqrt(2*degrees)+
        c.consistency_sigma_multiplier*c.consistency_sigma_multiplier;
    out.score=chi/threshold;
    out.valid=true;out.consistent=out.score<=1;
    return out;
}
double model_radius(const HelperJointEstimator::Config& c,const std::vector<Point>& points,const Fit& f,const V& row,const Step& step){
    const auto influence=mult(f.covariance,row);double radius=0;
    for(std::size_t n=f.begin;n<points.size();++n){const auto x=rows(c,points[n],f.center,step);const auto b=bounds(c,points[n]);
        for(std::size_t s=0;s<3;++s){double coefficient=0;for(std::size_t r=0;r<3;++r)coefficient+=dot(influence,x[r])*points[n].precision[r][s];radius+=std::abs(coefficient)*b[s];}
    }
    // Residual overdispersion is a model allowance, not relabeled thermal
    // covariance. It can widen output while preserving the raw noise block.
    radius+=c.sigma_multiplier*std::sqrt(std::max(0.,f.inflation-1)*variance(f.covariance,row));
    return radius;
}
V frequency_row(const HelperJointEstimator::Config& c,double nominal,double rf,double t,double center,const Step& step){
    const double dt=t-center,ratio=rf/c.reference_shift_hz;
    const double jump=step.active&&t>=step.before?(step.model==HelperJointEstimator::FrequencyStepModel::common_hz?1:ratio):0;
    return {nominal*1e-6,ratio,nominal*1e-6*dt,ratio*dt,0,jump};
}
V audio_row(double t,double center){return {1,0,t-center,0,0};}
V drift_row(){return {0,0,1,0,0};}
V phase_row(const HelperJointEstimator::Config& c,double t,double center){const double dt=t-center,k=tau*(c.high_hz-c.low_hz)*1e-6;return {k*dt,0,.5*k*dt*dt,0,k};}
bool models_consistent(const HelperJointEstimator::Config& c,const std::vector<Point>& points,const Fit& all,const Fit& recent,double& score,const Step& step){
    score=std::max(all.score,recent.score);
    if(!all.valid||!all.consistent)return false;
    if(!recent.valid)return true;
    if(!recent.consistent)return false;
    const double t=all.center;
    std::array<V,6> probes{frequency_row(c,c.high_hz,c.reference_shift_hz,t,all.center,step),
        frequency_row(c,c.low_hz,c.reference_shift_hz,t,all.center,step),
        V{0,0,c.high_hz*1e-6,1,0},V{0,0,c.low_hz*1e-6,1,0},audio_row(t,all.center),drift_row()};
    for(const auto& row:probes){
        // Both fits use the same center (last observed date). Sum marginal
        // sigmas permits their dependence instead of counting overlapping
        // recent samples as independent evidence.
        const double denominator=c.consistency_sigma_multiplier*(std::sqrt(variance(all.covariance,row))+
            std::sqrt(variance(recent.covariance,row)))+model_radius(c,points,all,row,step)+model_radius(c,points,recent,row,step)+1e-12;
        score=std::max(score,std::abs(dot(row,all.beta)-dot(row,recent.beta))/denominator);
    }
    if(step.active){
        // A short new segment must not hide one transient in the much larger
        // pre-step residual sum. Use its marginal frequency covariance, not
        // the conditional block after reusing its phase observation.
        double chi=0;std::size_t count=0;
        for(const auto& p:points)if(p.input.sample_support_valid&&p.input.first_sample_time_seconds>=step.before){
            const auto x=rows(c,p,all.center,step);const auto y=values(c,p);
            std::array<std::array<double,2>,2> covariance{},precision{};
            std::array<double,2> residual{};
            for(std::size_t i=0;i<2;++i){
                const double error=y[i]-dot(x[i],all.beta);
                residual[i]=std::copysign(std::max(0.,std::abs(error)-p.input.frequency_model_half_width_hz),error);
                for(std::size_t j=0;j<2;++j)covariance[i][j]=p.input.thermal_covariance[i][j]+p.process[i][j];
            }
            if(!inverse(covariance,precision))return false;
            for(std::size_t i=0;i<2;++i)for(std::size_t j=0;j<2;++j)chi+=residual[i]*precision[i][j]*residual[j];
            ++count;
        }
        if(count>=c.minimum_post_step_observations){
            const double degrees=static_cast<double>(2*count-1);
            const double threshold=degrees+c.sigma_multiplier*std::sqrt(2*degrees)+c.sigma_multiplier*c.sigma_multiplier;
            if(!finite(chi))return false;
            score=std::max(score,chi/threshold);
        }
    }
    return score<=1;
}
}

struct HelperJointEstimator::State {
    explicit State(Config c):config(c){}
    struct Candidate{std::vector<Point> points;Fit full,recent;double score=0;};
    Config config;std::vector<Candidate> candidates;Step step;
    std::size_t step_excluded=0;
    bool initialized=false,seen=false,coverage_lost=false,work_limited=false,last_consistent=false;
    double reference=0,last_seen=0;std::size_t accepted=0,rejected=0;
    bool branch_propagated=false,gap_pending=false,gap_has_fresh_observation=false;
    double branch_gap=0,gap_first=0;std::size_t gap_observations=0;
};

HelperJointEstimator::HelperJointEstimator(Config c):state_(std::make_unique<State>(c)){
    if(!finite(c.high_hz)||!finite(c.low_hz)||c.high_hz<=c.low_hz||!finite(c.reference_shift_hz)||c.reference_shift_hz==0||
       !finite(c.minimum_audio_ppm)||!finite(c.maximum_audio_ppm)||c.minimum_audio_ppm>=c.maximum_audio_ppm||
       !nonnegative(c.maximum_audio_drift_ppm_per_second)||!nonnegative(c.differential_phase_half_width_radians)||
       !nonnegative(c.numerical_phase_half_width_radians)||!finite(c.sigma_multiplier)||c.sigma_multiplier<=0||
       !finite(c.ambiguity_sigma_multiplier)||c.ambiguity_sigma_multiplier<=0||!finite(c.consistency_sigma_multiplier)||c.consistency_sigma_multiplier<=0||
       !finite(c.maximum_prediction_age_seconds)||c.maximum_prediction_age_seconds<=0||!finite(c.minimum_fit_span_seconds)||c.minimum_fit_span_seconds<=0||
       c.minimum_fit_observations<3||c.maximum_observations<c.minimum_fit_observations||c.maximum_observations>256||
       c.maximum_alternatives==0||c.maximum_alternatives>8||c.minimum_post_step_observations<3||
       c.minimum_post_step_observations>16||!finite(c.minimum_post_step_span_seconds)||c.minimum_post_step_span_seconds<=0||
       !finite(c.maximum_step_bracket_seconds)||c.maximum_step_bracket_seconds<=0||
       !finite(c.maximum_frequency_step_hz)||c.maximum_frequency_step_hz<=0||
       !nonnegative(c.maximum_phase_branch_gap_seconds)||c.maximum_phase_branch_gap_seconds>120||
       (c.maximum_phase_branch_gap_seconds>0&&c.maximum_phase_branch_gap_seconds<c.maximum_prediction_age_seconds)||
       !nonnegative(c.phase_branch_audio_evolution_ppm_per_second)||c.minimum_post_gap_observations<3||
       c.minimum_post_gap_observations>16||!finite(c.minimum_post_gap_span_seconds)||c.minimum_post_gap_span_seconds<=0)
        throw std::invalid_argument("invalid joint Helper geometry/work bounds");
}
HelperJointEstimator::~HelperJointEstimator()=default;
HelperJointEstimator::HelperJointEstimator(HelperJointEstimator&&) noexcept=default;
HelperJointEstimator& HelperJointEstimator::operator=(HelperJointEstimator&&) noexcept=default;
void HelperJointEstimator::reset() noexcept{const auto c=state_->config;*state_=State(c);}

HelperJointEstimator::Estimate HelperJointEstimator::predict(double nominal,double rf,double t) const{
    const auto& s=*state_;const auto& c=s.config;Estimate out;
    out.reference_initialized=s.initialized;out.reference_preserved=s.initialized&&!s.coverage_lost;
    out.reference_input_seconds=s.reference;out.time_seconds=t;out.alternatives=s.candidates.size();
    out.accepted_observations=s.accepted;out.rejected_observations=s.rejected;
    out.ambiguity_limited=s.coverage_lost;out.work_limited=s.work_limited;out.model_consistent=s.last_consistent;
    out.frequency_step_active=s.step.active;out.frequency_step_model=s.step.model;
    out.frequency_step_after_seconds=s.step.after;out.frequency_step_before_seconds=s.step.before;
    out.step_excluded_observations=s.step_excluded;
    out.phase_branch_propagation_used=s.branch_propagated;out.post_gap_qualification_pending=s.gap_pending;
    out.phase_branch_gap_seconds=s.branch_gap;out.post_gap_observations=s.gap_observations;
    if(!finite(nominal)||!finite(rf)||!finite(t)||!s.last_consistent||s.coverage_lost||s.work_limited||s.candidates.size()!=1)return out;
    if(s.gap_pending)return out;
    const auto& candidate=s.candidates.front();const auto& f=candidate.full;
    if(s.step.active){const auto [count,span]=post_step_support(candidate.points,s.step);
        out.post_step_observations=count;
        out.frequency_step_confirmed=count>=c.minimum_post_step_observations&&span+1e-9>=c.minimum_post_step_span_seconds;
        if(!out.frequency_step_confirmed||(t>=s.step.after&&t<=s.step.before))return out;
    }
    out.recent_guard_available=candidate.recent.valid&&candidate.recent.consistent;
    if(!f.valid||!f.consistent||!out.recent_guard_available||t<candidate.points.front().input.time_seconds||t>candidate.points.back().input.time_seconds+c.maximum_prediction_age_seconds)return out;
    const auto row=frequency_row(c,nominal,rf,t,f.center,s.step),ar=audio_row(t,f.center),dr=drift_row(),pr=phase_row(c,t,f.center);
    const double dt=t-s.reference;V sr{dt,0,.5*((t-f.center)*(t-f.center)-(s.reference-f.center)*(s.reference-f.center)),0,0};
    out.audio_scale=1+dot(ar,f.beta)*1e-6;out.audio_drift_ppm_per_second=dot(dr,f.beta);
    if(out.audio_scale<=0)return out;
    out.audio_thermal_sigma_ppm=std::sqrt(variance(f.thermal_covariance,ar));out.audio_stochastic_sigma_ppm=std::sqrt(variance(f.covariance,ar));out.audio_model_half_width_ppm=model_radius(c,candidate.points,f,ar,s.step);
    out.source_seconds=t+dot(sr,f.beta)*1e-6;
    out.source_thermal_sigma_seconds=std::sqrt(variance(f.thermal_covariance,sr))*1e-6;out.source_stochastic_sigma_seconds=std::sqrt(variance(f.covariance,sr))*1e-6;out.source_model_half_width_seconds=model_radius(c,candidate.points,f,sr,s.step)*1e-6;
    out.frequency_hz=nominal+dot(row,f.beta);out.thermal_sigma_hz=std::sqrt(variance(f.thermal_covariance,row));
    out.process_sigma_hz=std::sqrt(variance(f.process_covariance,row));out.stochastic_sigma_hz=std::sqrt(variance(f.covariance,row));
    out.model_half_width_hz=model_radius(c,candidate.points,f,row,s.step);
    out.conditional_half_width_hz=c.sigma_multiplier*out.stochastic_sigma_hz+out.model_half_width_hz;
    out.recent_frequency_hz=out.frequency_hz;out.recent_thermal_sigma_hz=out.thermal_sigma_hz;
    if(candidate.recent.valid){const auto rr=frequency_row(c,nominal,rf,t,candidate.recent.center,s.step);
        out.recent_frequency_hz=nominal+dot(rr,candidate.recent.beta);
        out.recent_thermal_sigma_hz=std::sqrt(variance(candidate.recent.thermal_covariance,rr));
        const double radius=std::abs(out.recent_frequency_hz-out.frequency_hz)+c.sigma_multiplier*std::sqrt(variance(candidate.recent.covariance,rr))+
            model_radius(c,candidate.points,candidate.recent,rr,s.step);
        out.conditional_half_width_hz=std::max(out.conditional_half_width_hz,radius);
    }
    out.phase_difference_radians=wrap(tau*(c.high_hz-c.low_hz)*(t-s.reference)+dot(pr,f.beta));
    out.phase_difference_thermal_sigma_radians=std::sqrt(variance(f.thermal_covariance,pr));out.phase_difference_process_sigma_radians=std::sqrt(variance(f.process_covariance,pr));out.phase_difference_stochastic_sigma_radians=std::sqrt(variance(f.covariance,pr));
    out.phase_difference_model_half_width_radians=model_radius(c,candidate.points,f,pr,s.step);
    if(s.step.active){out.frequency_step_hz=f.beta[5];out.frequency_step_stochastic_sigma_hz=std::sqrt(f.covariance[5][5]);}
    out.model_consistency_score=candidate.score;out.valid=finite(out.conditional_half_width_hz)&&finite(out.phase_difference_radians);return out;
}

HelperJointEstimator::Estimate HelperJointEstimator::observe(const Input& input){
    auto& s=*state_;const auto& c=s.config;
    const auto fail=[&](){++s.rejected;
        if(s.gap_pending){s.gap_observations=0;s.gap_first=0;}
        auto out=predict(c.high_hz,c.reference_shift_hz,input.time_seconds);out.valid=false;return out;};
    // Contrary external evidence is fail-closed even when its notification
    // date is malformed or stale. Do not advance chronology or alter retained
    // observations in that case, but do not keep advertising the old model.
    if(input.model_contradicted)s.last_consistent=false;
    if(!finite(input.time_seconds)||(s.seen&&input.time_seconds<=s.last_seen))return fail();
    s.seen=true;s.last_seen=input.time_seconds;
    if(c.maximum_phase_branch_gap_seconds>0&&s.initialized&&!s.candidates.empty()){
        const double gap=input.time_seconds-s.candidates.front().points.back().input.time_seconds;
        if(gap>c.maximum_prediction_age_seconds){
            if(!s.gap_pending)s.gap_has_fresh_observation=false;
            s.gap_pending=true;s.branch_gap=gap;s.gap_observations=0;s.gap_first=0;
        }
    }
    if(s.coverage_lost||s.work_limited)return fail();
    if(input.model_contradicted)return fail();
    if(!input.valid)return fail();
    if(!finite(input.frequency_time_seconds)||std::abs(input.frequency_time_seconds-input.time_seconds)>1||!finite(input.shift_hz)||input.shift_hz==0||
       !finite(input.high_frequency_hz)||!finite(input.low_frequency_hz)||!finite(input.high_center_phase_radians)||!finite(input.low_center_phase_radians)||
       !nonnegative(input.common_frequency_sigma_hz)||!nonnegative(input.frequency_model_half_width_hz)||!nonnegative(input.phase_model_half_width_radians))return fail();
    const double low_date=input.separate_low_frequency_time?input.low_frequency_time_seconds:input.frequency_time_seconds;
    if(!finite(low_date)||std::abs(low_date-input.time_seconds)>1)return fail();
    if(input.sample_support_valid&&(!finite(input.first_sample_time_seconds)||!finite(input.end_sample_time_seconds)||
       input.first_sample_time_seconds>=input.end_sample_time_seconds||input.time_seconds<input.first_sample_time_seconds||
       input.time_seconds>input.end_sample_time_seconds||input.frequency_time_seconds<input.first_sample_time_seconds||
       input.frequency_time_seconds>input.end_sample_time_seconds||low_date<input.first_sample_time_seconds||low_date>input.end_sample_time_seconds))return fail();
    if(input.sample_support_valid&&!s.candidates.empty()&&!s.candidates.front().points.empty()){
        const auto& previous=s.candidates.front().points.back().input;
        if(previous.sample_support_valid&&input.first_sample_time_seconds<previous.end_sample_time_seconds-1e-9)return fail();
    }
    if(s.step.active&&(!input.sample_support_valid||
       !(input.end_sample_time_seconds<=s.step.after||input.first_sample_time_seconds>=s.step.before)))return fail();
    Q covariance=input.thermal_covariance;
    Q thermal_precision{};
    // A common process must not rescue an invalid thermal covariance. The
    // separate thermal sandwich is meaningful only when that supplied block
    // is itself positive definite.
    if(!inverse(input.thermal_covariance,thermal_precision))return fail();
    const auto process=process_covariance(input);
    if(!positive_semidefinite(process))return fail();
    for(std::size_t r=0;r<3;++r)for(std::size_t z=0;z<3;++z)covariance[r][z]+=process[r][z];
    Q precision{};if(!inverse(covariance,precision))return fail();
    Point point;point.input=input;point.precision=precision;point.process=process;
    if(!s.initialized){
        s.initialized=true;s.reference=input.time_seconds;s.last_consistent=true;s.accepted=1;
        point.phase_residual=wrap(wrap(input.high_center_phase_radians)-wrap(input.low_center_phase_radians));
        State::Candidate candidate;candidate.points.push_back(point);s.candidates.push_back(std::move(candidate));return predict(c.high_hz,c.reference_shift_hz,input.time_seconds);
    }
    const double nominal_cycles=(c.high_hz-c.low_hz)*(input.time_seconds-s.reference);
    if(!finite(nominal_cycles))return fail();
    const double raw=wrap(wrap(input.high_center_phase_radians)-wrap(input.low_center_phase_radians)-
        tau*std::remainder(nominal_cycles,1.));
    const double k=tau*(c.high_hz-c.low_hz)*1e-6;
    const double radius=c.ambiguity_sigma_multiplier*std::sqrt(covariance[2][2])+c.differential_phase_half_width_radians+
        c.numerical_phase_half_width_radians+input.phase_model_half_width_radians;
    std::vector<State::Candidate> next;
    for(const auto& previous:s.candidates){
        if(previous.points.size()>=c.maximum_observations){s.work_limited=true;return fail();}
        const auto& last=previous.points.back();const double dt=input.time_seconds-last.input.time_seconds;
        const double last_radius=c.ambiguity_sigma_multiplier*std::sqrt(last.input.thermal_covariance[2][2]+last.process[2][2])+
            c.differential_phase_half_width_radians+c.numerical_phase_half_width_radians+last.input.phase_model_half_width_radians;
        double lower=last.phase_residual-last_radius+k*(c.minimum_audio_ppm*dt-.5*c.maximum_audio_drift_ppm_per_second*dt*dt)-radius;
        double upper=last.phase_residual+last_radius+k*(c.maximum_audio_ppm*dt+.5*c.maximum_audio_drift_ppm_per_second*dt*dt)+radius;
        if(previous.full.valid&&previous.full.consistent&&dt<=c.maximum_prediction_age_seconds){
            const auto row=phase_row(c,input.time_seconds,previous.full.center);
            const double center=dot(row,previous.full.beta);
            const double support=c.ambiguity_sigma_multiplier*std::sqrt(variance(previous.full.covariance,row))+
                model_radius(c,previous.points,previous.full,row,s.step)+radius;
            lower=std::max(lower,center-support);upper=std::min(upper,center+support);
        }
        else if(c.maximum_phase_branch_gap_seconds>0&&dt>c.maximum_prediction_age_seconds&&
                dt<=c.maximum_phase_branch_gap_seconds&&!s.gap_has_fresh_observation&&s.last_consistent&&
                previous.full.valid&&previous.full.consistent&&previous.recent.valid&&previous.recent.consistent){
            // Enumeration only: propagate endpoint phase and endpoint audio
            // rate, NOT the fitted quadratic drift. The combined projection
            // keeps their complete covariance/cross covariance. model_radius
            // likewise uses each old observation once. The all/recent fits
            // overlap; enclose their intervals instead of multiplying evidence.
            double learned_lower=std::numeric_limits<double>::infinity();
            double learned_upper=-std::numeric_limits<double>::infinity();
            for(const auto* f:{&previous.full,&previous.recent}){
                auto row=phase_row(c,last.input.time_seconds,f->center);
                const auto ar=audio_row(last.input.time_seconds,f->center);
                for(std::size_t i=0;i<row.size();++i)row[i]+=k*dt*ar[i];
                const double center=dot(row,f->beta);
                const double support=c.ambiguity_sigma_multiplier*std::sqrt(variance(f->covariance,row))+
                    model_radius(c,previous.points,*f,row,s.step)+
                    .5*k*c.phase_branch_audio_evolution_ppm_per_second*dt*dt+radius;
                learned_lower=std::min(learned_lower,center-support);
                learned_upper=std::max(learned_upper,center+support);
            }
            s.branch_propagated=true;
            lower=std::max(lower,learned_lower);upper=std::min(upper,learned_upper);
        }
        if(!finite(lower)||!finite(upper)||upper<lower)continue;
        const double first_double=std::ceil((lower-raw)/tau),last_double=std::floor((upper-raw)/tau);
        if(!finite(first_double)||!finite(last_double)||last_double-first_double>static_cast<double>(c.maximum_alternatives)||
           std::abs(first_double)>1e9||std::abs(last_double)>1e9){s.coverage_lost=true;return fail();}
        const auto first=static_cast<long long>(first_double),end=static_cast<long long>(last_double);
        for(auto branch=first;branch<=end;++branch){
            State::Candidate candidate=previous;point.phase_residual=raw+tau*static_cast<double>(branch);candidate.points.push_back(point);
            candidate.full=fit(c,candidate.points,0,s.step);
            const bool fit_expected=candidate.points.size()>=c.minimum_fit_observations&&
                candidate.points.back().input.time_seconds-candidate.points.front().input.time_seconds>=c.minimum_fit_span_seconds;
            if(fit_expected&&!candidate.full.valid)continue;
            const double middle=.5*(candidate.points.front().input.time_seconds+candidate.points.back().input.time_seconds);
            std::size_t begin=0;while(begin+1<candidate.points.size()&&candidate.points[begin].input.time_seconds<middle)++begin;
            candidate.recent=fit(c,candidate.points,begin,s.step);
            if(candidate.full.valid&&!models_consistent(c,candidate.points,candidate.full,candidate.recent,candidate.score,s.step))continue;
            if(candidate.full.valid){const auto ar=audio_row(input.time_seconds,candidate.full.center),dr=drift_row();
                const double audio=dot(ar,candidate.full.beta);
                const double audio_radius=c.ambiguity_sigma_multiplier*std::sqrt(variance(candidate.full.covariance,ar))+model_radius(c,candidate.points,candidate.full,ar,s.step);
                const double drift_radius=c.ambiguity_sigma_multiplier*std::sqrt(variance(candidate.full.covariance,dr))+model_radius(c,candidate.points,candidate.full,dr,s.step);
                if(audio+audio_radius<c.minimum_audio_ppm||audio-audio_radius>c.maximum_audio_ppm||
                   std::abs(candidate.full.beta[2])-drift_radius>c.maximum_audio_drift_ppm_per_second)continue;}
            if(s.step.active&&candidate.full.valid){const V jump{0,0,0,0,0,1};
                const double step_radius=c.ambiguity_sigma_multiplier*std::sqrt(variance(candidate.full.covariance,jump))+
                    model_radius(c,candidate.points,candidate.full,jump,s.step);
                if(std::abs(candidate.full.beta[5])-step_radius>c.maximum_frequency_step_hz)continue;
            }
            next.push_back(std::move(candidate));
            if(next.size()>c.maximum_alternatives){s.coverage_lost=true;return fail();}
        }
    }
    if(next.empty()){s.last_consistent=false;return fail();}
    s.candidates=std::move(next);s.last_consistent=true;++s.accepted;
    if(s.gap_pending){
        s.gap_has_fresh_observation=true;
        if(s.gap_observations==0)s.gap_first=input.time_seconds;
        ++s.gap_observations;
        if(s.gap_observations>=c.minimum_post_gap_observations&&
           input.time_seconds-s.gap_first+1e-9>=c.minimum_post_gap_span_seconds&&s.candidates.size()==1){
            const auto& candidate=s.candidates.front();
            if(candidate.full.valid&&candidate.full.consistent&&candidate.recent.valid&&candidate.recent.consistent)
                s.gap_pending=s.gap_has_fresh_observation=false;
        }
    }
    return predict(c.high_hz,c.reference_shift_hz,input.time_seconds);
}

bool HelperJointEstimator::begin_frequency_step(FrequencyStepModel model,double after,double before,
                                                std::span<const Input> replay){
    const auto& original=*state_;const auto& c=original.config;
    if(!c.allow_frequency_step||original.step.active||!original.initialized||original.coverage_lost||original.work_limited||original.gap_pending||
       original.candidates.size()!=1||!original.candidates.front().full.valid||
       !finite(after)||!finite(before)||after>=before||before-after>c.maximum_step_bracket_seconds||
       after<=original.reference||replay.size()>16||
       (model!=FrequencyStepModel::common_hz&&model!=FrequencyStepModel::rf_scaled))return false;
    const auto same=[](const Input& a,const Input& b){
        return a.valid==b.valid&&a.time_seconds==b.time_seconds&&a.frequency_time_seconds==b.frequency_time_seconds&&
            a.shift_hz==b.shift_hz&&a.high_frequency_hz==b.high_frequency_hz&&a.low_frequency_hz==b.low_frequency_hz&&
            a.high_center_phase_radians==b.high_center_phase_radians&&a.low_center_phase_radians==b.low_center_phase_radians&&
            a.thermal_covariance==b.thermal_covariance&&a.common_frequency_sigma_hz==b.common_frequency_sigma_hz&&
            a.frequency_model_half_width_hz==b.frequency_model_half_width_hz&&a.phase_model_half_width_radians==b.phase_model_half_width_radians&&
            a.process_covariance_valid==b.process_covariance_valid&&a.process_covariance==b.process_covariance&&
            a.separate_low_frequency_time==b.separate_low_frequency_time&&a.low_frequency_time_seconds==b.low_frequency_time_seconds&&
            a.sample_support_valid==b.sample_support_valid&&a.first_sample_time_seconds==b.first_sample_time_seconds&&
            a.end_sample_time_seconds==b.end_sample_time_seconds;
    };
    std::vector<Point> prefix;std::vector<Input> post;
    std::vector<double> excluded;
    const auto exclude=[&](double date){if(std::find(excluded.begin(),excluded.end(),date)==excluded.end())excluded.push_back(date);};
    const auto& old=original.candidates.front().points;
    for(const auto& p:old){
        if(!p.input.sample_support_valid)return false;
        if(p.input.end_sample_time_seconds<=after)prefix.push_back(p);
        else if(p.input.first_sample_time_seconds>=before)post.push_back(p.input);
        else exclude(p.input.time_seconds);
    }
    if(prefix.empty()||prefix.front().input.time_seconds!=original.reference||
       prefix.size()<c.minimum_fit_observations)return false;
    for(std::size_t i=1;i<prefix.size();++i)
        if(prefix[i].input.first_sample_time_seconds<prefix[i-1].input.end_sample_time_seconds-1e-9)return false;
    for(const auto& raw:replay){
        if(!raw.valid||raw.model_contradicted||!raw.sample_support_valid||!finite(raw.first_sample_time_seconds)||!finite(raw.end_sample_time_seconds)||
           raw.first_sample_time_seconds>=raw.end_sample_time_seconds)return false;
        if(raw.end_sample_time_seconds<=after)continue;
        if(raw.first_sample_time_seconds<before){exclude(raw.time_seconds);continue;}
        auto existing=std::find_if(post.begin(),post.end(),[&](const Input& p){return p.time_seconds==raw.time_seconds;});
        if(existing!=post.end()){if(!same(*existing,raw))return false;}
        else post.push_back(raw);
    }
    if(post.size()<c.minimum_post_step_observations||prefix.size()+post.size()>c.maximum_observations)return false;
    std::sort(post.begin(),post.end(),[](const Input& a,const Input& b){return a.time_seconds<b.time_seconds;});
    if(post.back().time_seconds-post.front().time_seconds+1e-9<c.minimum_post_step_span_seconds)return false;
    for(std::size_t i=1;i<post.size();++i)
        if(post[i].time_seconds<=post[i-1].time_seconds||post[i].time_seconds-post[i-1].time_seconds>.800000001||
           post[i].first_sample_time_seconds<post[i-1].end_sample_time_seconds-1e-9)return false;
    HelperJointEstimator temporary(c);*temporary.state_=original;
    auto& next=*temporary.state_;next.step={true,model,after,before};next.step_excluded=excluded.size();
    next.coverage_lost=next.work_limited=false;next.last_consistent=true;
    auto& candidate=next.candidates.front();candidate.points=std::move(prefix);
    candidate.full=fit(c,candidate.points,0,Step{});
    if(!candidate.full.valid||!candidate.full.consistent)return false;
    const double middle=.5*(candidate.points.front().input.time_seconds+candidate.points.back().input.time_seconds);
    std::size_t begin=0;while(begin+1<candidate.points.size()&&candidate.points[begin].input.time_seconds<middle)++begin;
    candidate.recent=fit(c,candidate.points,begin,Step{});
    next.last_seen=candidate.points.back().input.time_seconds;next.seen=true;
    std::size_t newly_accepted=0;
    for(const auto& raw:post){
        const auto count=next.accepted;
        (void)temporary.observe(raw);
        if(next.accepted!=count+1||next.coverage_lost||next.work_limited)return false;
        if(std::none_of(old.begin(),old.end(),[&](const Point& p){return p.input.time_seconds==raw.time_seconds;}))++newly_accepted;
    }
    next.accepted=original.accepted+newly_accepted;
    next.rejected=original.rejected;
    next.last_seen=std::max(original.last_seen,next.last_seen);
    const auto outcome=temporary.predict(c.high_hz,c.reference_shift_hz,next.last_seen);
    if(!outcome.valid||!outcome.reference_preserved||outcome.reference_input_seconds!=original.reference||
       !outcome.frequency_step_confirmed)return false;
    const V jump{0,0,0,0,0,1};const auto& final=next.candidates.front();
    const double no_step_radius=c.sigma_multiplier*outcome.frequency_step_stochastic_sigma_hz+
        model_radius(c,final.points,final.full,jump,next.step);
    if(std::abs(outcome.frequency_step_hz)<=no_step_radius)return false;
    state_=std::move(temporary.state_);return true;
}
}
