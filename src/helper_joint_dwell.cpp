#include "datapump/helper_joint_dwell.hpp"

#include <algorithm>
#include <array>
#include <cfenv>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

namespace datapump::helper {
namespace {
using C = std::complex<double>;
constexpr double tau = 2 * std::numbers::pi;
using M4 = std::array<std::array<double,4>,4>;
using M3 = std::array<std::array<double,3>,3>;
constexpr std::size_t m = 10;
using A10 = std::array<std::array<double,4>,m>;
using V4 = std::array<double,4>;
using V10 = std::array<double,m>;
double wrap(double x) { return std::remainder(x,tau); }

// One fixed, immutable aperture cache. The normal Helper dwell uses 8,640
// settled samples at 24 kHz. C++ local-static initialization is synchronized;
// after construction these coefficients are read-only. Other legal lengths and
// non-default rounding modes retain the original per-call construction, so no
// cache grows with untrusted input or captures another thread's FP environment.
struct CommonWindowWeights {
    static constexpr std::size_t size = 8640;
    std::array<double,size> full{};
    std::array<double,size/4> quarter{};
    CommonWindowWeights() {
        for(std::size_t i=0;i<full.size();++i)
            full[i]=.5-.5*std::cos(tau*static_cast<double>(i)/static_cast<double>(full.size()-1));
        for(std::size_t i=0;i<quarter.size();++i)
            quarter[i]=.5-.5*std::cos(tau*static_cast<double>(i)/static_cast<double>(quarter.size()-1));
    }
};

bool solve4(M4 a,V4 b,V4& out) {
    for(std::size_t k=0;k<4;++k){
        std::size_t p=k;
        for(std::size_t i=k+1;i<4;++i)if(std::abs(a[i][k])>std::abs(a[p][k]))p=i;
        if(!(std::abs(a[p][k])>1e-18)||!std::isfinite(a[p][k]))return false;
        std::swap(a[k],a[p]);std::swap(b[k],b[p]);
        const double d=a[k][k];for(std::size_t j=k;j<4;++j)a[k][j]/=d;b[k]/=d;
        for(std::size_t i=0;i<4;++i)if(i!=k){const double t=a[i][k];for(std::size_t j=k;j<4;++j)a[i][j]-=t*a[k][j];b[i]-=t*b[k];}
    }
    if(!std::all_of(b.begin(),b.end(),[](double x){return std::isfinite(x);}))return false;
    out=b;return true;
}
M3 reduced(const M4& a){
    // Input coordinates are (fH, fL, phiH, phiL).
    constexpr std::array<V4,3> t{{{{1,0,0,0}},{{0,1,0,0}},{{0,0,1,-1}}}};
    M3 out{};
    for(std::size_t i=0;i<3;++i)for(std::size_t j=0;j<3;++j)
        for(std::size_t k=0;k<4;++k)for(std::size_t l=0;l<4;++l)out[i][j]+=t[i][k]*a[k][l]*t[j][l];
    return out;
}
}

HelperJointDwellResult estimate_helper_joint_dwell(
    std::span<const std::complex<float>> samples,const HelperJointDwellConfig& c,
    const HelperAidedPhasePrediction& prediction,const HelperAidedPhaseResult& gated) {
    HelperJointDwellResult out;
    const std::size_t n=samples.size();
    if(!prediction.valid||!gated.valid||!gated.thermal_covariance_valid||!gated.frequency_dates_valid||
       !std::isfinite(c.sample_rate)||c.sample_rate<=0||!std::isfinite(c.phase_noise_radians_per_sqrt_second)||
       c.phase_noise_radians_per_sqrt_second<0||!std::isfinite(c.frequency_drift_bound_hz_per_second)||
       c.frequency_drift_bound_hz_per_second<0||!std::isfinite(c.maximum_residual_hz)||c.maximum_residual_hz<=0||
       !std::isfinite(c.maximum_fit_score)||c.maximum_fit_score<=0||n<400||n>c.maximum_samples||n%4!=0||
       !std::isfinite(prediction.high_frequency_hz)||!std::isfinite(prediction.low_frequency_hz))return out;
    const auto q=n/4;const double fs=c.sample_rate,center=static_cast<double>(n-1)/(2*fs);
    const auto& g=gated.measurement;
    if(!g.valid||!std::isfinite(g.first_sample_time_seconds)||!std::isfinite(g.end_sample_time_seconds)||
       !std::isfinite(gated.center_time_seconds)||!std::isfinite(gated.high_residual_hz)||!std::isfinite(gated.low_residual_hz)||
       std::abs((g.end_sample_time_seconds-g.first_sample_time_seconds)-static_cast<double>(n)/fs)>1e-8||
       std::abs(gated.center_time_seconds-(g.first_sample_time_seconds+center))>1e-8||
       std::abs((prediction.high_frequency_hz+gated.high_residual_hz)-g.high_frequency_hz)>1e-7||
       std::abs((prediction.low_frequency_hz+gated.low_residual_hz)-g.low_frequency_hz)>1e-7)return out;
    const double f[2]{prediction.high_frequency_hz,prediction.low_frequency_hz};
    std::array<std::array<C,5>,2> z{};
    std::array<std::array<double,5>,5> overlap{};
    std::array<std::array<C,5>,5> cross{};
    C osc[2]{std::polar(1.,tau*f[0]*center),std::polar(1.,tau*f[1]*center)};
    const C step[2]{std::polar(1.,-tau*f[0]/fs),std::polar(1.,-tau*f[1]/fs)};
    std::vector<double> dynamic_full,dynamic_quarter;
    std::span<const double> wf,ws;
    if(c.use_static_window_cache&&n==CommonWindowWeights::size&&std::fegetround()==FE_TONEAREST){
        static const CommonWindowWeights common;
        wf=common.full;ws=common.quarter;
    }else{
        dynamic_full.resize(n);dynamic_quarter.resize(q);
        for(std::size_t i=0;i<n;++i)dynamic_full[i]=.5-.5*std::cos(tau*static_cast<double>(i)/static_cast<double>(n-1));
        for(std::size_t i=0;i<q;++i)dynamic_quarter[i]=.5-.5*std::cos(tau*static_cast<double>(i)/static_cast<double>(q-1));
        wf=dynamic_full;ws=dynamic_quarter;
    }
    for(std::size_t i=0;i<n;++i){
        if(!std::isfinite(samples[i].real())||!std::isfinite(samples[i].imag()))return out;
        const std::size_t j=i/q+1,k=i%q;const double a=wf[i],b=ws[k];
        for(std::size_t p=0;p<2;++p){const C x=C(samples[i])*osc[p];z[p][0]+=a*x;z[p][j]+=b*x;}
        const C x=osc[0]*std::conj(osc[1]);
        overlap[0][0]+=a*a;overlap[0][j]+=a*b;overlap[j][0]+=a*b;overlap[j][j]+=b*b;
        cross[0][0]+=a*a*x;cross[0][j]+=a*b*x;cross[j][0]+=a*b*x;cross[j][j]+=b*b*x;
        osc[0]*=step[0];osc[1]*=step[1];
    }
    const double noise=fs*std::max(gated.high_raw_noise_density,gated.low_raw_noise_density);
    if(!(noise>0)||!std::isfinite(noise))return out;
    const double inflation[2]{std::sqrt(std::max(1.,gated.high_phase_fit_score/2)),std::sqrt(std::max(1.,gated.low_phase_fit_score/2))};
    std::array<std::array<double,m>,m> cov{},L{};
    for(std::size_t i=0;i<m;++i)for(std::size_t j=0;j<m;++j){
        const auto p=i/5,k=i%5,r=j/5,l=j%5;
        if(std::norm(z[p][k])<1e-20||std::norm(z[r][l])<1e-20)return out;
        const C kernel=p==r?C(overlap[k][l],0):(p==0?cross[k][l]:std::conj(cross[l][k]));
        cov[i][j]=noise*inflation[p]*inflation[r]*std::real(kernel/(z[p][k]*std::conj(z[r][l])));
    }
    for(std::size_t i=0;i<m;++i)for(std::size_t j=0;j<=i;++j){double v=cov[i][j];
        for(std::size_t k=0;k<j;++k)v-=L[i][k]*L[j][k];
        if(i==j){if(!(v>1e-18)||!std::isfinite(v))return out;L[i][j]=std::sqrt(v);}
        else L[i][j]=v/L[j][j];}
    const double ref[2]{std::arg(z[0][0]),std::arg(z[1][0])};
    V10 y{};A10 A{};
    for(std::size_t p=0;p<2;++p)for(std::size_t k=0;k<5;++k){const auto i=p*5+k;
        const double t=k?(static_cast<double>((k-1)*q)+static_cast<double>(q-1)/2.-static_cast<double>(n-1)/2.)/fs:0;
        y[i]=wrap(std::arg(z[p][k])-ref[p]);if(std::abs(y[i])>std::numbers::pi/2)return out;
        if(p==0)A[i]={1,tau*t,0,0};else A[i]={0,0,1,tau*t};
    }
    for(std::size_t i=0;i<m;++i){for(std::size_t j=0;j<i;++j){y[i]-=L[i][j]*y[j];for(std::size_t k=0;k<4;++k)A[i][k]-=L[i][j]*A[j][k];}
        y[i]/=L[i][i];for(double& v:A[i])v/=L[i][i];}
    M4 normal{};V4 rhs{};
    for(std::size_t j=0;j<m;++j)for(std::size_t a=0;a<4;++a){rhs[a]+=A[j][a]*y[j];for(std::size_t b=0;b<4;++b)normal[a][b]+=A[j][a]*A[j][b];}
    V4 fit{};if(!solve4(normal,rhs,fit)||std::abs(fit[1])>=c.maximum_residual_hz||std::abs(fit[3])>=c.maximum_residual_hz)return out;
    double score=0;for(std::size_t i=0;i<m;++i){double e=y[i];for(std::size_t k=0;k<4;++k)e-=A[i][k]*fit[k];score+=e*e;}
    if(!std::isfinite(score)||score>c.maximum_fit_score)return out;
    M4 P{};for(std::size_t i=0;i<4;++i){V4 basis{},column{};basis[i]=1;if(!solve4(normal,basis,column))return out;for(std::size_t j=0;j<4;++j)P[j][i]=column[j];}
    // G maps the ten unwhitened correlation phases to the four fitted
    // parameters; the observations overlap and share the same sample noise.
    std::array<V10,4> G{};
    for(std::size_t a=0;a<4;++a){V10 w{};for(std::size_t i=0;i<m;++i)for(std::size_t j=0;j<4;++j)w[i]+=P[a][j]*A[i][j];
        for(std::size_t ii=m;ii-->0;){double x=w[ii];for(std::size_t j=ii+1;j<m;++j)x-=L[j][ii]*G[a][j];G[a][ii]=x/L[ii][ii];}}
    // Influence of a common Wiener phase on the fitted two-tone model. The
    // observed local amplitudes are plug-in estimates, not a bound on arbitrary
    // interference or nonlinear phase evolution.
    const double sumw=.5*static_cast<double>(n-1);
    const double fhat[2]{f[0]+fit[1],f[1]+fit[3]};
    const double phat[2]{wrap(ref[0]+fit[0]),wrap(ref[1]+fit[2])};
    C amplitude[2]{};
    for(std::size_t p=0;p<2;++p){double response=0;
        for(std::size_t i=0;i<n;++i)response+=wf[i]*std::cos(tau*(fhat[p]-f[p])*(static_cast<double>(i)/fs-center));
        if(!(std::abs(response)>.25*sumw))return out;
        amplitude[p]=std::polar(std::abs(z[p][0])/std::abs(response),phat[p]);}
    // Propagate a declared within-aperture frequency-drift radius through the
    // observed correlation magnitudes and the fitted GLS phase influence. A
    // correlation angle cannot move by more than asin(|delta z|/|z|) while
    // |delta z|<|z|. This remains conditional on the plug-in two-tone model;
    // arbitrary other signals, phase jumps and underestimated noise are not
    // covered by this model allowance.
    const double maximum_model_phase=.5*tau*c.frequency_drift_bound_hz_per_second*center*center;
    const double model_amplitude=std::abs(amplitude[0])+std::abs(amplitude[1]);
    double phase_model=0,difference_model=0,frequency_model[2]{};
    for(std::size_t j=0;j<m;++j){
        const std::size_t pilot=j/5,window=j%5;
        const double window_sum=window==0?sumw:.5*static_cast<double>(q-1);
        const double ratio=window_sum*model_amplitude*maximum_model_phase/std::abs(z[pilot][window]);
        if(!std::isfinite(ratio)||ratio>=1)return out;
        const double bound=std::asin(std::max(0.,ratio));
        phase_model+=std::abs(G[0][j])*bound;
        difference_model+=std::abs(G[0][j]-G[2][j])*bound;
        frequency_model[0]+=std::abs(G[1][j])*bound;
        frequency_model[1]+=std::abs(G[3][j])*bound;
    }
    std::vector<V4> influence(n);
    C d[2]{std::polar(1.,tau*f[0]*center),std::polar(1.,tau*f[1]*center)};
    C s[2]{std::polar(1.,-tau*fhat[0]*center),std::polar(1.,-tau*fhat[1]*center)};
    const C ss[2]{std::polar(1.,tau*fhat[0]/fs),std::polar(1.,tau*fhat[1]/fs)};
    for(std::size_t i=0;i<n;++i){const auto j=i/q+1,k=i%q;const C signal=amplitude[0]*s[0]+amplitude[1]*s[1];
        V10 phase_effect{};
        for(std::size_t p=0;p<2;++p){const C v=signal*d[p];phase_effect[p*5]=wf[i]*std::real(v/z[p][0]);phase_effect[p*5+j]=ws[k]*std::real(v/z[p][j]);d[p]*=step[p];s[p]*=ss[p];}
        for(std::size_t a=0;a<4;++a)for(std::size_t b=0;b<m;++b)influence[i][a]+=G[a][b]*phase_effect[b];
    }
    M4 process_theta{};M3 process_reduced{};V4 tail{};const double process_scale=1/fs;
    for(std::size_t i=n;i-->0;){
        for(std::size_t a=0;a<4;++a)tail[a]+=influence[i][a];
        for(std::size_t a=0;a<4;++a)for(std::size_t b=0;b<4;++b)process_theta[a][b]+=process_scale*tail[a]*tail[b];
        const std::array<double,3> reduced_tail{tail[1],tail[3],tail[0]-tail[2]};
        for(std::size_t a=0;a<3;++a)for(std::size_t b=0;b<3;++b)process_reduced[a][b]+=process_scale*reduced_tail[a]*reduced_tail[b];
    }
    constexpr std::array<std::size_t,4> order{1,3,0,2};M4 thermal{},process{};
    const double q2=c.phase_noise_radians_per_sqrt_second*c.phase_noise_radians_per_sqrt_second;
    const double score_inflation=std::max(1.,score/6.);
    for(std::size_t i=0;i<4;++i)for(std::size_t j=0;j<4;++j){thermal[i][j]=P[order[i]][order[j]]*score_inflation;process[i][j]=q2*process_theta[order[i]][order[j]];
        if(!std::isfinite(thermal[i][j])||!std::isfinite(process[i][j]))return out;}
    for(auto& row:process_reduced)for(double& x:row)x*=q2;
    for(std::size_t i=0;i<4;++i)if(!(thermal[i][i]>0)||process[i][i]<0)return out;
    auto r=gated;r.measurement.high_frequency_hz=fhat[0];r.measurement.low_frequency_hz=fhat[1];
    r.high_center_phase_radians=phat[0];r.low_center_phase_radians=phat[1];
    r.high_residual_hz=fit[1];r.low_residual_hz=fit[3];
    r.high_frequency_time_seconds=r.low_frequency_time_seconds=r.center_time_seconds;
    r.measurement.high_phase_radians=wrap(phat[0]-tau*fhat[0]*center);
    r.measurement.low_phase_radians=wrap(phat[1]-tau*fhat[1]*center);
    // HelperMeasurement's legacy global fit accepts only a rank-one common
    // process plus diagonal marginals. Add a diagonally dominant PSD envelope
    // for the full new process block instead of pretending its H/L processes
    // are exactly equal. The joint fit consumes the exact separate blocks.
    const double common=gated.measurement.common_phase_frequency_sigma_hz;
    const double common_variance=common*common;
    const double off=std::abs(process[0][1]-common_variance);
    const double extra_high=std::max(0.,process[0][0]-common_variance)+off;
    const double extra_low=std::max(0.,process[1][1]-common_variance)+off;
    r.measurement.high_sigma_hz=std::sqrt(thermal[0][0]+extra_high);
    r.measurement.low_sigma_hz=std::sqrt(thermal[1][1]+extra_low);
    r.measurement.common_phase_frequency_sigma_hz=common;
    r.common_center_phase_sigma_radians=std::max(gated.common_center_phase_sigma_radians,std::sqrt(std::max(process[2][2],process[3][3])));
    r.high_center_phase_sigma_radians=std::hypot(std::sqrt(thermal[2][2]),r.common_center_phase_sigma_radians);
    r.low_center_phase_sigma_radians=std::hypot(std::sqrt(thermal[3][3]),r.common_center_phase_sigma_radians);
    r.pilot_thermal_covariance=thermal;r.thermal_covariance=reduced(thermal);
    r.pilot_process_covariance=process;r.process_covariance=process_reduced;
    r.thermal_covariance_valid=true;r.process_covariance_valid=true;
    r.phase_difference_sigma_radians=std::sqrt(std::max(0.,r.thermal_covariance[2][2]))+std::sqrt(std::max(0.,r.process_covariance[2][2]));
    r.high_first_phase_sigma_radians=r.high_center_phase_sigma_radians+std::sqrt(process[2][2])+tau*center*(r.measurement.high_sigma_hz+std::sqrt(process[0][0]));
    r.low_first_phase_sigma_radians=r.low_center_phase_sigma_radians+std::sqrt(process[3][3])+tau*center*(r.measurement.low_sigma_hz+std::sqrt(process[1][1]));
    const double drift=c.frequency_drift_bound_hz_per_second*static_cast<double>(n)/fs+10e-6;
    r.high_frequency_model_half_width_hz=std::max({gated.high_frequency_model_half_width_hz,drift,frequency_model[0]});
    r.low_frequency_model_half_width_hz=std::max({gated.low_frequency_model_half_width_hz,drift,frequency_model[1]});
    r.common_frequency_process_variance_factor=std::max(process_theta[1][1],process_theta[3][3]);
    r.work_sample_products+=50*n;
    out.valid=true;out.fallback_required=false;out.fit_score=score;
    out.common_phase_model_half_width_radians=phase_model;
    out.phase_difference_model_half_width_radians=difference_model;
    out.observation=std::move(r);return out;
}
} // namespace datapump::helper
