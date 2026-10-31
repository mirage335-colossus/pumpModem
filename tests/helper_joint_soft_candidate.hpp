#pragma once
// Standalone opt-in experimental joint-phase candidate. The caller must pass
// prior predictions obtained before this dwell and retain the original raw
// aided observations as the sole historical joint-fit inputs. This produces
// only a current-dwell phase; its frequency result is diagnostic and must not
// drive conditioner frequency, recovery, or be ingested into history.
// Covariance is a local, plug-in white-noise model with guarded PSD inflation.
// Its finite-sample coverage, coloured noise, and arbitrary differential
// channel phase are not established. Failure always leaves baseline available.
#include "datapump/helper_aided_phase.hpp"
#include "datapump/helper_joint_estimator.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>
#include <span>
namespace helper_joint_soft_candidate {
namespace h=datapump::helper;
using C=std::complex<double>;
using S=std::complex<float>;
constexpr double fs=24000.,tau=2*std::numbers::pi;
inline double wrap(double x){return std::remainder(x,tau);}
struct Fit4 { bool valid=false; std::array<double,4> x{}; };
inline Fit4 solve4(std::array<std::array<double,4>,4> a,std::array<double,4> b){
    for(unsigned k=0;k<4;++k){
        unsigned pivot=k;for(unsigned i=k+1;i<4;++i)if(std::abs(a[i][k])>std::abs(a[pivot][k]))pivot=i;
        if(!(std::abs(a[pivot][k])>1e-18)||!std::isfinite(a[pivot][k]))return {};
        std::swap(a[k],a[pivot]);std::swap(b[k],b[pivot]);
        const double d=a[k][k];for(unsigned j=k;j<4;++j)a[k][j]/=d;b[k]/=d;
        for(unsigned i=0;i<4;++i)if(i!=k){const double t=a[i][k];for(unsigned j=k;j<4;++j)a[i][j]-=t*a[k][j];b[i]-=t*b[k];}
    }
    for(double v:b)if(!std::isfinite(v))return {};return {true,b};
}
struct JointResult {bool valid=false;double phase=0,frequency=0,phase_innovation=0,frequency_innovation=0,phase_box_radius=0;
    double conditional_phase_radius=0,thermal_phase_sigma=0,process_phase_sigma=0,residual_score=0;};
inline JointResult soft_joint_shared(std::span<const S> samples,double hf,double lf,const h::HelperJointEstimator::Estimate& prior,
                              const h::HelperAidedPhaseResult& data,double phase_bias,double sep_bias,double phase_q){
    if(samples.size()!=8640||!prior.valid||!prior.reference_preserved||prior.alternatives!=1||!data.valid)return {};
    constexpr unsigned n=8640,q=2160,m=10;constexpr double center=(n-1)/2.;
    std::array<std::array<C,5>,2> z{};std::array<std::array<double,5>,5> overlap{};
    std::array<std::array<C,5>,5> cross{};std::array<double,5> sumabs{};
    const double freq[2]{hf,lf+sep_bias};
    C osc[2]{std::polar(1.,tau*freq[0]*center/fs),std::polar(1.,tau*freq[1]*center/fs)};
    const C step[2]{std::polar(1.,-tau*freq[0]/fs),std::polar(1.,-tau*freq[1]/fs)};
    for(unsigned i=0;i<n;++i){
        const unsigned j=i/q+1,k=i%q;const double wf=.5-.5*std::cos(tau*i/(n-1)),ws=.5-.5*std::cos(tau*k/(q-1));
        sumabs[0]+=wf*std::abs(samples[i]);sumabs[j]+=ws*std::abs(samples[i]);
        for(unsigned p=0;p<2;++p){const C x=C(samples[i])*osc[p];z[p][0]+=wf*x;z[p][j]+=ws*x;}
        const C x=osc[0]*std::conj(osc[1]);
        overlap[0][0]+=wf*wf;overlap[0][j]+=wf*ws;overlap[j][0]+=wf*ws;overlap[j][j]+=ws*ws;
        cross[0][0]+=wf*wf*x;cross[0][j]+=wf*ws*x;cross[j][0]+=wf*ws*x;cross[j][j]+=ws*ws*x;
        osc[0]*=step[0];osc[1]*=step[1];
    }
    const double noise=fs*std::max(data.high_raw_noise_density,data.low_raw_noise_density);
    if(!(noise>0)||!std::isfinite(noise)||std::norm(z[0][0])<1e-20)return {};
    const double inflation[2]{std::sqrt(std::max(1.,data.high_phase_fit_score/2)),std::sqrt(std::max(1.,data.low_phase_fit_score/2))};
    std::array<std::array<double,m>,m> covariance{};
    for(unsigned i=0;i<m;++i)for(unsigned j=0;j<m;++j){
        const unsigned p=i/5,k=i%5,r=j/5,l=j%5;
        if(std::norm(z[p][k])<1e-20||std::norm(z[r][l])<1e-20)return {};
        const C kernel=p==r?C(overlap[k][l],0):(p==0?cross[k][l]:std::conj(cross[l][k]));
        covariance[i][j]=noise*inflation[p]*inflation[r]*std::real(kernel/(z[p][k]*std::conj(z[r][l])));
    }
    // Cholesky whitening includes shared samples and cross-pilot input noise.
    // The dependence of this plug-in covariance on observed samples makes the
    // resulting estimator an uncalibrated local approximation.
    std::array<std::array<double,m>,m> L{};
    for(unsigned i=0;i<m;++i)for(unsigned j=0;j<=i;++j){double v=covariance[i][j];
        for(unsigned k=0;k<j;++k)v-=L[i][k]*L[j][k];
        if(i==j){if(!(v>1e-18)||!std::isfinite(v))return {};L[i][j]=std::sqrt(v);}
        else L[i][j]=v/L[j][j];}
    const double reference=std::arg(z[0][0]),delta0=prior.phase_difference_radians+phase_bias;
    std::array<double,m> y{};std::array<std::array<double,4>,m> A{};
    for(unsigned p=0;p<2;++p)for(unsigned k=0;k<5;++k){const unsigned i=p*5+k;
        const double t=k?(static_cast<double>((k-1)*q)+(q-1)/2.-center)/fs:0;
        y[i]=wrap(std::arg(z[p][k])-reference+(p?delta0:0));
        if(std::abs(y[i])>std::numbers::pi/2)return {}; // unresolved phase branch
        A[i]=p?std::array<double,4>{1,tau*t,-1,-tau*t}:std::array<double,4>{1,tau*t,0,0};
    }
    for(unsigned i=0;i<m;++i){for(unsigned j=0;j<i;++j){y[i]-=L[i][j]*y[j];for(unsigned k=0;k<4;++k)A[i][k]-=L[i][j]*A[j][k];}
        y[i]/=L[i][i];for(double& x:A[i])x/=L[i][i];}
    std::array<std::array<double,4>,4> normal{};std::array<double,4>b{};
    for(unsigned nrow=0;nrow<m;++nrow)for(unsigned i=0;i<4;++i){b[i]+=A[nrow][i]*y[nrow];for(unsigned j=0;j<4;++j)normal[i][j]+=A[nrow][i]*A[nrow][j];}
    const double sp=std::max(1e-5,std::sqrt(2.)*(prior.phase_difference_thermal_sigma_radians+prior.phase_difference_process_sigma_radians));
    const double sf=std::max(1e-5,std::sqrt(2.)*1850e-6*prior.audio_stochastic_sigma_ppm);
    normal[2][2]+=1/(sp*sp);normal[3][3]+=1/(sf*sf);
    const auto fit=solve4(normal,b);if(!fit.valid||std::abs(fit.x[1])>=.75||std::abs(fit.x[3])>.75)return {};
    const double phase_bound=prior.phase_difference_model_half_width_radians+.02;
    const double frequency_bound=1850e-6*prior.audio_model_half_width_ppm+data.high_frequency_model_half_width_hz+data.low_frequency_model_half_width_hz;
    double box=0;for(int i=-1;i<=1;++i)for(int j=-1;j<=1;++j){auto shifted=b;
        shifted[2]+=i*phase_bound/(sp*sp);shifted[3]+=j*frequency_bound/(sf*sf);
        const auto alt=solve4(normal,shifted);if(!alt.valid)return {};box=std::max(box,std::abs(alt.x[0]-fit.x[0]));}
    double score=0;for(unsigned i=0;i<m;++i){double residual=y[i];for(unsigned j=0;j<4;++j)residual-=A[i][j]*fit.x[j];score+=residual*residual;}
    if(!std::isfinite(score)||score>30)return {}; // bounded consistency fallback
    const auto sensitivity=solve4(normal,{1,0,0,0});
    if(!sensitivity.valid||!(sensitivity.x[0]>0))return {};
    std::array<double,m> whitened_weight{},original_weight{};
    for(unsigned i=0;i<m;++i)for(unsigned j=0;j<4;++j)whitened_weight[i]+=A[i][j]*sensitivity.x[j];
    for(int i=static_cast<int>(m)-1;i>=0;--i){double x=whitened_weight[static_cast<unsigned>(i)];
        for(unsigned j=static_cast<unsigned>(i)+1;j<m;++j)x-=L[j][static_cast<unsigned>(i)]*original_weight[j];
        original_weight[static_cast<unsigned>(i)]=x/L[static_cast<unsigned>(i)][static_cast<unsigned>(i)];}
    // Local stochastic process allowance. Under the conditional circular
    // Gaussian noise model, a union bound gives all n complex-noise samples
    // a 1e-6 tail probability for exceeding this amplitude envelope. On that
    // event, |signal| <= |observed|+envelope; Minkowski then bounds each
    // signed GLS phase influence, including the true center reference. This
    // remains conditional on the estimated inflated PSD upper-bounding noise.
    const double noise_envelope=std::sqrt(2*noise*std::log(static_cast<double>(n)/1e-6));
    const double sumw[5]{.5*(n-1),.5*(q-1),.5*(q-1),.5*(q-1),.5*(q-1)};
    double influence=1;
    for(unsigned i=0;i<m;++i)influence+=std::abs(original_weight[i])*(sumabs[i%5]+noise_envelope*sumw[i%5])/std::abs(z[i/5][i%5]);
    double current_thermal=0;for(const double v:whitened_weight)current_thermal+=v*v;
    const double prior_phase_gain=std::abs(sensitivity.x[2])/(sp*sp),prior_frequency_gain=std::abs(sensitivity.x[3])/(sf*sf);
    const double audio_process=std::sqrt(std::max(0.,prior.audio_stochastic_sigma_ppm*prior.audio_stochastic_sigma_ppm-
                                                       prior.audio_thermal_sigma_ppm*prior.audio_thermal_sigma_ppm));
    const double prior_thermal=std::sqrt(2.)*(prior_phase_gain*prior.phase_difference_thermal_sigma_radians+
        prior_frequency_gain*1850e-6*prior.audio_thermal_sigma_ppm);
    const double prior_process=std::sqrt(2.)*(prior_phase_gain*prior.phase_difference_process_sigma_radians+
        prior_frequency_gain*1850e-6*audio_process);
    const double process=phase_q*std::sqrt(n/fs)*influence+prior_process;
    const double thermal=(std::sqrt(current_thermal)+prior_thermal)*std::sqrt(std::max(1.,score/6));
    const double radius=3*(thermal+process)+box;
    if(!std::isfinite(radius)||radius>=.5)return {};
    return {true,wrap(reference+fit.x[0]),hf+fit.x[1],fit.x[2],fit.x[3],box,radius,thermal,process,score};
}

// Strictly previous prior: reference preserved, one branch, latest raw input
// before this center and same predicted center. Post-current timing radius is
// supplied by caller, and is *added* to phase radius via L1 because both use
// the same current observation. This makes no independence assumption.
inline JointResult estimate(std::span<const S> samples,double hf,double lf,
    const h::HelperJointEstimator::Estimate& prior,const h::HelperAidedPhaseResult& data,
    double latest_previous_seconds,double baseline_phase_sigma,double current_time_radius_seconds,
    double drift_bound_hz_per_second,double phase_q){
    if(!prior.valid||!prior.reference_preserved||prior.alternatives!=1||!data.valid||
       !data.thermal_covariance_valid||!data.frequency_dates_valid||
       !std::isfinite(latest_previous_seconds)||latest_previous_seconds>=data.center_time_seconds||
       std::abs(prior.time_seconds-data.center_time_seconds)>1e-5||
       !std::isfinite(baseline_phase_sigma)||baseline_phase_sigma<0||
       !std::isfinite(current_time_radius_seconds)||current_time_radius_seconds<0||
       !std::isfinite(drift_bound_hz_per_second)||drift_bound_hz_per_second<0||
       !std::isfinite(phase_q)||phase_q<0)return {};
    const double fdiff=(data.measurement.high_frequency_hz-data.measurement.low_frequency_hz)-(hf-lf);
    const auto difference=[](const auto& c){return std::max(0.,c[0][0]+c[1][1]-2*c[0][1]);};
    const double ft=std::sqrt(difference(data.thermal_covariance));
    const double fp=data.process_covariance_valid?std::sqrt(difference(data.process_covariance)):2*data.measurement.common_phase_frequency_sigma_hz;
    const double fprior=1850e-6*prior.audio_stochastic_sigma_ppm;
    const double fb=data.high_frequency_model_half_width_hz+data.low_frequency_model_half_width_hz+
        1850e-6*prior.audio_model_half_width_ppm+2*drift_bound_hz_per_second*samples.size()/fs;
    const double fsig=ft+fp+fprior;
    const double pdiff=wrap(data.high_center_phase_radians-data.low_center_phase_radians-prior.phase_difference_radians);
    const double psig=std::sqrt(std::max(0.,data.thermal_covariance[2][2]))+prior.phase_difference_stochastic_sigma_radians;
    const double pbound=prior.phase_difference_model_half_width_radians+.02;
    if(!std::isfinite(fdiff)||!std::isfinite(fsig)||!std::isfinite(pdiff)||!std::isfinite(psig)||
       std::abs(fdiff)>fb+5*fsig||std::abs(pdiff)>pbound+5*psig)return {};
    auto fit=soft_joint_shared(samples,hf,lf,prior,data,0,0,phase_q);
    if(!fit.valid)return {};
    fit.conditional_phase_radius=std::max(3*baseline_phase_sigma,
        fit.conditional_phase_radius+tau*2400*current_time_radius_seconds);
    if(!std::isfinite(fit.conditional_phase_radius)||fit.conditional_phase_radius>=.5)return {};
    return fit;
}
} // namespace helper_joint_soft_candidate
