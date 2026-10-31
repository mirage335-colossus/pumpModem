#include "datapump/helper_piecewise_dwell.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>

namespace datapump::helper {
namespace {
constexpr double tau=2*std::numbers::pi;
constexpr std::size_t bins=24;
using C=std::complex<double>;

struct Correlation { C value{}; double effective_seconds=0; };
Correlation correlate(std::span<const std::complex<float>> input,double fs,double frequency,double relative_first) {
    C z{},osc=std::polar(1.,-tau*frequency*relative_first);
    const C step=std::polar(1.,-tau*frequency/fs);
    double sum=0,square=0;
    const double n=static_cast<double>(input.size()-1);
    for(std::size_t i=0;i<input.size();++i){
        const double w=.5-.5*std::cos(tau*static_cast<double>(i)/n);
        z+=C(input[i])*osc*w;sum+=w;square+=w*w;osc*=step;
    }
    return {z/sum,sum*sum/(fs*square)};
}
bool finite(double x){return std::isfinite(x);}
}

HelperPiecewiseDwellResult estimate_helper_piecewise_dwell(
    std::span<const std::complex<float>> samples,const HelperAidedPhaseConfig& c,
    const HelperPiecewiseDwellConfig& pc,const HelperAidedPhasePrediction& prediction,
    const HelperAidedPhaseResult& baseline,double start){
    HelperPiecewiseDwellResult out;
    if(!baseline.valid||!baseline.measurement.valid||!prediction.valid||!finite(start)||
       !finite(c.sample_rate)||c.sample_rate<=0||samples.size()<bins*32||samples.size()>24000||samples.size()%bins!=0||
       samples.size()>pc.maximum_samples||samples.size()>c.maximum_samples||
       !finite(c.slope_frequency_drift_bound_hz_per_second)||c.slope_frequency_drift_bound_hz_per_second<0||
       !finite(baseline.noise_psd_safety_factor)||baseline.noise_psd_safety_factor<1||
       !finite(baseline.center_time_seconds)||!finite(baseline.measurement.first_sample_time_seconds)||
       !finite(baseline.measurement.end_sample_time_seconds)||!finite(baseline.measurement.common_phase_frequency_sigma_hz)||
       baseline.measurement.common_phase_frequency_sigma_hz<0||
       !finite(pc.detection_sigma)||pc.detection_sigma<3.5||pc.detection_sigma>10||
       !finite(pc.model_half_width_radians)||pc.model_half_width_radians<0||pc.model_half_width_radians>.5||
       !finite(pc.minimum_support_seconds)||pc.minimum_support_seconds<.1||
       pc.boundary_guard_segments<1||pc.boundary_guard_segments>4||
       !finite(c.phase_noise_radians_per_sqrt_second)||c.phase_noise_radians_per_sqrt_second<0)return out;
    const std::array<double,2> freq{prediction.high_frequency_hz,prediction.low_frequency_hz};
    const std::array<double,2> freq_sigma{prediction.high_sigma_hz,prediction.low_sigma_hz};
    const std::array<double,2> density{baseline.high_raw_noise_density,baseline.low_raw_noise_density};
    for(std::size_t p=0;p<2;++p)if(!finite(freq[p])||!finite(freq_sigma[p])||freq_sigma[p]<0||
        !finite(density[p])||density[p]<=0)return out;
    const auto n=samples.size()/bins;
    const double fs=c.sample_rate,center=static_cast<double>(samples.size()-1)/(2*fs);
    const double end=start+static_cast<double>(samples.size())/fs;
    const double tolerance=std::max(1e-10,1e-6/fs);
    if(!finite(end)||std::abs(baseline.measurement.first_sample_time_seconds-start)>tolerance||
       std::abs(baseline.measurement.end_sample_time_seconds-end)>tolerance||
       std::abs(baseline.center_time_seconds-(start+center))>tolerance)return out;
    std::array<std::array<C,bins+1>,2> prefix{};
    std::array<C,2> first_moment{};
    std::array<double,2> eff{};
    constexpr double mid=static_cast<double>(bins-1)/2;
    double sum_time2=0;for(std::size_t j=0;j<bins;++j){const double t=static_cast<double>(j)-mid;sum_time2+=t*t;}
    for(std::size_t p=0;p<2;++p)for(std::size_t j=0;j<bins;++j){
        const auto r=correlate(samples.subspan(j*n,n),fs,freq[p],static_cast<double>(j*n)/fs-center);
        if(!finite(r.value.real())||!finite(r.value.imag())||!finite(r.effective_seconds)||r.effective_seconds<=0)return out;
        prefix[p][j+1]=prefix[p][j]+r.value;
        first_moment[p]+=(static_cast<double>(j)-mid)*r.value;eff[p]=r.effective_seconds;
    }
    std::array<C,2> mean{};
    std::array<double,2> variance{};
    for(std::size_t p=0;p<2;++p){mean[p]=prefix[p][bins]/static_cast<double>(bins);
        if(!(std::norm(mean[p])>0))return out;
        // raw_noise_density is E[|complex correlation|^2]*effective;
        // each circular thermal quadrature has half that variance. Retain
        // the gated estimator's explicit finite-PSD safety factor instead
        // of treating the eight-probe PSD estimate as exact.
        variance[p]=baseline.noise_psd_safety_factor*density[p]/(2*eff[p]*std::norm(mean[p]));
        if(!finite(variance[p])||variance[p]<=0)return out;}
    // Exhaustive scan of <=6 contiguous 15 ms bins (at 24 kHz/360 ms):
    // 129 intervals x six two-sided scalar models have nominal Gaussian
    // union bound about .0053 at 4.5 sigma, plus .00021 for radial models.
    // These are not calibrated false-alarm guarantees after plug-in PSD/gain
    // estimation or when the declared channel/noise models are violated.
    // shared real phase contrast and common gain, plus either pilot separately.
    // Under circular, locally white thermal noise the cross-pilot correlation
    // is bounded by the Hann-squared transform at their predicted separation.
    // Same-sample common oscillator process is bounded separately below. The pulse template
    // is projected off constant and linear-time nuisance terms, fitting local
    // intercept and frequency without treating the prior as another sample.
    // The contrast denominator accounts for all shared bins exactly under the
    // linearized independent-bin noise model. Plug-in phase, model and process
    // uncertainties are further carried as conservative conditional radii.
    C cross_window{};double weight_square=0;
    for(std::size_t i=0;i<n;++i){const double w=.5-.5*std::cos(tau*static_cast<double>(i)/static_cast<double>(n-1));
        cross_window+=w*w*std::polar(1.,tau*(freq[0]-freq[1])*static_cast<double>(i)/fs);weight_square+=w*w;}
    const double rho=std::min(1.,std::abs(cross_window)/weight_square);
    std::size_t best_first=0,best_end=0;unsigned best_model=0;double best_margin=0,best_score=0;
    for(std::size_t k=1;k<=6;++k)for(std::size_t a=0;a+k<=bins;++a){
        const auto b=a+k;const double in=static_cast<double>(k);
        const double sum_in_time=in*(static_cast<double>(a)+.5*static_cast<double>(k-1)-mid);
        const double beta=sum_in_time/sum_time2;
        const double energy=in-in*in/static_cast<double>(bins)-sum_in_time*beta;
        if(!(energy>0))continue;
        double contrast_l1=0;
        for(std::size_t j=0;j<bins;++j){const double member=(j>=a&&j<b)?1.:0.;
            contrast_l1+=std::abs(member-in/static_cast<double>(bins)-beta*(static_cast<double>(j)-mid))/energy;}
        std::array<double,2> phase{},gain{},sigma{},radius{};
        for(std::size_t p=0;p<2;++p){
            const C projected=(prefix[p][b]-prefix[p][a])-(in/static_cast<double>(bins))*prefix[p][bins]-beta*first_moment[p];
            const C delta=projected*std::conj(mean[p])/(energy*std::norm(mean[p]));
            phase[p]=delta.imag();gain[p]=delta.real();
            sigma[p]=std::sqrt(variance[p]/energy);
            // A linear frequency nuisance is profiled from these same bins;
            // the term below covers second-order residual phase curvature under
            // the declared 3-sigma prediction excursion, plus modeled process.
            const double halfspan=static_cast<double>(bins)*static_cast<double>(n)/(2*fs);
            const double excursion=tau*halfspan*3*freq_sigma[p];
            radius[p]=pc.model_half_width_radians+contrast_l1*(.5*excursion*excursion+
                tau*c.slope_frequency_drift_bound_hz_per_second*halfspan*halfspan+
                3*c.phase_noise_radians_per_sqrt_second*std::sqrt(2*halfspan));
        }
        // Cross covariance is bounded by rho, never assumed zero. Gain and
        // phase use fixed quadratic weights; the phase orientation and PSD
        // are observed plug-ins, so finite-SNR coverage requires validation.
        const double w0=1/(sigma[0]*sigma[0]),w1=1/(sigma[1]*sigma[1]),den=w0+w1;
        const double common_sigma=std::sqrt(w0*w0*sigma[0]*sigma[0]+w1*w1*sigma[1]*sigma[1]+
            2*rho*w0*w1*sigma[0]*sigma[1])/den;
        const double common_radius=(w0*radius[0]+w1*radius[1])/den;
        const std::array<double,8> value{
            (w0*phase[0]+w1*phase[1])/den,
            phase[0],phase[1],(w0*gain[0]+w1*gain[1])/den,
            gain[0],gain[1],std::hypot(phase[0],gain[0]),std::hypot(phase[1],gain[1])};
        const std::array<double,8> sd{common_sigma,sigma[0],sigma[1],common_sigma,sigma[0],sigma[1],sigma[0],sigma[1]};
        const std::array<double,8> bound{common_radius,radius[0],radius[1],common_radius,radius[0],radius[1],
            std::sqrt(2.)*radius[0],std::sqrt(2.)*radius[1]};
        for(unsigned model=0;model<8;++model){
            const double score=(std::abs(value[model])-bound[model])/sd[model];
            // Radial tests have two noise degrees of freedom; the fixed 5.3
            // threshold bounds the nominal chi-square_2 family tail for this
            // capped search. This is conditional on fixed plug-in PSD/gains.
            const double gate=model>=6?std::max(pc.detection_sigma,5.3):pc.detection_sigma;
            if(score-gate>best_margin){best_margin=score-gate;best_score=score;best_first=a;best_end=b;best_model=model+1;}
        }
    }
    out.evaluated=true;out.maximum_score=best_score;out.winning_model=best_model;
    if(!best_model)return out;
    out.disturbance_detected=true;out.change_first_sample=best_first*n;out.change_end_sample=best_end*n;
    const std::size_t guard=pc.boundary_guard_segments*n;
    const std::size_t exclude_start=out.change_first_sample>guard?out.change_first_sample-guard:0;
    const std::size_t exclude_end=std::min(samples.size(),out.change_end_sample+guard);
    const double retune_samples=std::ceil(.04*fs);
    // Check before conversion: an otherwise finite sample rate may make the
    // guard exceed size_t or the entire sample support.
    if(!finite(retune_samples)||retune_samples>=static_cast<double>(samples.size())/2)return out;
    const auto retune=static_cast<std::size_t>(retune_samples);
    const auto lo1=retune,hi1=std::min(exclude_start,samples.size()-retune);
    const auto lo2=std::max(exclude_end,retune),hi2=samples.size()-retune;
    const bool first=hi1>lo1&&(hi2<=lo2||hi1-lo1>=hi2-lo2);
    const auto lo=first?lo1:lo2,hi=first?hi1:hi2;
    if(hi<=lo||static_cast<double>(hi-lo)/fs<pc.minimum_support_seconds)return out;
    const auto data=samples.subspan(lo,hi-lo);
    const double offset=static_cast<double>(data.size()-1)/(2*fs);
    std::array<Correlation,2> selected{};
    for(std::size_t p=0;p<2;++p){selected[p]=correlate(data,fs,freq[p],-offset);
        if(!finite(std::norm(selected[p].value))||!(std::norm(selected[p].value)>0))return out;}
    const double date=start+static_cast<double>(lo)/fs+offset;
    const auto calc=[&](std::size_t p){
        // 3*1.7=5.1 sigma; roughly 258 selectable sides x two pilots give
        // nominal Gaussian union tail ~0.00018. Amplitude/PSD are selected
        // plug-ins, so this remains a conditional, uncalibrated allowance.
        constexpr double selection=1.7;
        return selection*std::sqrt(baseline.noise_psd_safety_factor*density[p]/(2*std::norm(selected[p].value)*selected[p].effective_seconds))+
            tau*(std::abs(date-baseline.center_time_seconds)+offset)*(freq_sigma[p]+baseline.measurement.common_phase_frequency_sigma_hz)+
            c.phase_noise_radians_per_sqrt_second*std::sqrt(static_cast<double>(hi-lo)/fs);
    };
    out.high_sigma_radians=calc(0);out.low_sigma_radians=calc(1);
    if(!finite(out.high_sigma_radians)||!finite(out.low_sigma_radians))return out;
    out.support_first_sample=lo;out.support_end_sample=hi;out.anchor_time_seconds=date;
    out.high_phase_radians=std::arg(selected[0].value);out.low_phase_radians=std::arg(selected[1].value);
    out.model_half_width_radians=pc.model_half_width_radians;out.valid=true;return out;
}
}
