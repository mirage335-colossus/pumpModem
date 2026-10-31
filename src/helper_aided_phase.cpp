#include "datapump/helper_aided_phase.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace datapump::helper {
namespace {
constexpr double pi=3.1415926535897932384626433832795;
constexpr double tau=2*pi;
using C=std::complex<double>;

struct Window {
    std::vector<double> w;
    double sum=0,square_sum=0,time_square_sum=0,square_time_square_sum=0,brownian_frequency_variance_factor=0;
    explicit Window(std::size_t n,unsigned repetitions=1):w(n) {
        const double denominator=static_cast<double>(n-1);
        for(std::size_t i=0;i<n;++i) {
            const double a=.5-.5*std::cos(tau*repetitions*static_cast<double>(i)/denominator);
            w[i]=a;sum+=a;square_sum+=a*a;
        }
    }
    double effective_seconds(double fs)const {return sum*sum/(fs*square_sum);}
    void date(double fs) {
        const double center=static_cast<double>(w.size()-1)/(2*fs);
        for(std::size_t i=0;i<w.size();++i) {
            const double t=static_cast<double>(i)/fs-center;
            time_square_sum+=w[i]*t*t;
            square_time_square_sum+=w[i]*w[i]*t*t;
        }
        // Brownian phase B_i has independent increments of variance q²/fs.
        // The local frequency influence is w_i*t_i/(2*pi*sum(w*t²)); summing
        // its tails gives the EXACT discrete linearized Wiener transfer.
        double tail=0,square_tails=0;
        for(std::size_t i=w.size();i-->0;) {
            const double t=static_cast<double>(i)/fs-center;
            tail+=w[i]*t;square_tails+=tail*tail;
        }
        brownian_frequency_variance_factor=square_tails/(fs*tau*tau*time_square_sum*time_square_sum);
    }
};

C correlate(std::span<const std::complex<float>> input,const Window& window,
            double fs,double hz,double first_offset) {
    C sum=0,oscillator=std::polar(1.,-tau*hz*first_offset);
    const C step=std::polar(1.,-tau*hz/fs);
    for(std::size_t i=0;i<input.size();++i) {
        sum+=C(input[i].real(),input[i].imag())*(window.w[i]*oscillator);
        oscillator*=step;
    }
    return sum/window.sum;
}

struct Pilot {
    bool valid=false;
    HelperAidedPhaseFailure failure=HelperAidedPhaseFailure::insufficient_signal;
    double center_phase=0,center_white_sigma=0,first_sigma=0;
    double frequency=0,frequency_white_sigma=0,cn0=0,residual=0,score=0;
    double raw_noise_density=0,inflation=1;
    double frequency_amplitude=0,frequency_phase=0,phase_amplitude=0;
    double prediction_hz=0,frequency_date_offset=0;
    C center_correlation{};
    std::array<C,4> subcorrelations{};
    std::array<double,4> slope_phase_gain{},slope_power_gain{};
    std::array<bool,4> slope_local_power{};
    std::size_t iterations=0;
};

struct Moments {C z=0,first=0,second=0;};
Moments moments(std::span<const std::complex<float>> input,const Window& window,double fs,double hz) {
    Moments out;
    const double center=static_cast<double>(input.size()-1)/(2*fs);
    C oscillator=std::polar(1.,tau*hz*center);
    const C step=std::polar(1.,-tau*hz/fs);
    for(std::size_t i=0;i<input.size();++i) {
        const double t=static_cast<double>(i)/fs-center;
        const C v=C(input[i].real(),input[i].imag())*(window.w[i]*oscillator);
        out.z+=v;out.first+=t*v;out.second+=t*t*v;oscillator*=step;
    }
    out.z/=window.sum;out.first/=window.sum;out.second/=window.sum;return out;
}

// Current-aperture objective only. The prediction bounds the local branch but
// contributes no extra likelihood or information. All iterations and steps
// are bounded, and a non-converged/boundary result requests broad acquisition.
bool refine_frequency(std::span<const std::complex<float>> samples,const Window& window,
    const HelperAidedPhaseConfig& c,double prediction,double initial,Pilot& out,std::size_t& work) {
    double f=prediction+initial;
    bool converged=false;
    Moments fit;
    for(std::size_t k=0;k<c.maximum_refinement_iterations;++k) {
        fit=moments(samples,window,c.sample_rate,f);work+=samples.size();out.iterations=k+1;
        const double derivative=2*tau*std::imag(std::conj(fit.z)*fit.first);
        const double curvature=2*tau*tau*(std::norm(fit.first)-std::real(std::conj(fit.z)*fit.second));
        if(!std::isfinite(derivative)||!std::isfinite(curvature)||!(curvature<0))return false;
        const double step=-derivative/curvature;
        if(!std::isfinite(step))return false;
        if(std::abs(step)<=c.refinement_tolerance_hz){converged=true;break;}
        const double applied=std::clamp(step,-c.refinement_step_limit_hz,c.refinement_step_limit_hz);
        const double next=f+applied;
        if(std::abs(next-prediction)>=c.maximum_residual_frequency_hz)return false;
        f=next;
    }
    if(!converged||!(std::abs(f-prediction)<c.maximum_residual_frequency_hz))return false;
    out.frequency=f;out.residual=f-prediction;
    out.frequency_amplitude=std::abs(fit.z);out.frequency_phase=std::arg(fit.z);
    return out.frequency_amplitude>0;
}

bool covariance(const Window& phase_window,const Window& frequency_window,
    const HelperAidedPhaseConfig& c,const Pilot& high,const Pilot& low,
    HelperAidedPhaseResult& out) {
    const double fs=c.sample_rate;
    const double center=static_cast<double>(phase_window.w.size()-1)/(2*fs);
    const double density=2*std::max(high.raw_noise_density,low.raw_noise_density);
    const double variance=density*fs/2;
    if(!(variance>0)||!std::isfinite(variance)||!(frequency_window.time_square_sum>0))return false;
    const std::array<const Pilot*,2> pilots{&high,&low};
    std::array<C,2> oscillators{},steps{},frequency_rotations{},phase_rotations{};
    for(std::size_t k=0;k<2;++k) {
        oscillators[k]=std::polar(1.,tau*pilots[k]->frequency*center);
        steps[k]=std::polar(1.,-tau*pilots[k]->frequency/fs);
        frequency_rotations[k]=std::polar(1.,-pilots[k]->frequency_phase);
        phase_rotations[k]=std::polar(1.,-pilots[k]->center_phase);
    }
    std::array<std::array<double,4>,4> normal{};
    for(std::size_t i=0;i<phase_window.w.size();++i) {
        const double t=static_cast<double>(i)/fs-center;
        std::array<C,4> influence{};
        for(std::size_t k=0;k<2;++k) {
            const auto& p=*pilots[k];
            influence[k]=frequency_window.w[i]*t*p.inflation/
                (tau*p.frequency_amplitude*frequency_window.time_square_sum)*
                frequency_rotations[k]*oscillators[k];
            influence[k+2]=phase_window.w[i]*p.inflation/(p.phase_amplitude*phase_window.sum)*
                phase_rotations[k]*oscillators[k];
            oscillators[k]*=steps[k];
        }
        for(std::size_t a=0;a<4;++a)for(std::size_t b=0;b<=a;++b)
            normal[a][b]+=variance*std::real(influence[a]*std::conj(influence[b]));
    }
    for(std::size_t a=0;a<4;++a)for(std::size_t b=0;b<=a;++b) {
        if(!std::isfinite(normal[a][b]))return false;
        out.pilot_thermal_covariance[a][b]=normal[a][b];
        out.pilot_thermal_covariance[b][a]=normal[a][b];
    }
    constexpr std::array<std::array<double,4>,3> transform{{{{1,0,0,0}},{{0,1,0,0}},{{0,0,1,-1}}}};
    for(std::size_t a=0;a<3;++a)for(std::size_t b=0;b<3;++b)
        for(std::size_t i=0;i<4;++i)for(std::size_t j=0;j<4;++j)
            out.thermal_covariance[a][b]+=transform[a][i]*out.pilot_thermal_covariance[i][j]*transform[b][j];
    out.work_sample_products+=16*phase_window.w.size();
    out.thermal_covariance_valid=true;return true;
}

using Matrix4=std::array<std::array<double,4>,4>;
using Matrix3=std::array<std::array<double,3>,3>;

Matrix3 difference_covariance(const Matrix4& input) {
    constexpr std::array<std::array<double,4>,3> transform{{{{1,0,0,0}},{{0,1,0,0}},{{0,0,1,-1}}}};
    Matrix3 result{};
    for(std::size_t a=0;a<3;++a)for(std::size_t b=0;b<3;++b)
        for(std::size_t i=0;i<4;++i)for(std::size_t j=0;j<4;++j)
            result[a][b]+=transform[a][i]*input[i][j]*transform[b][j];
    return result;
}

// Linearize the ACTUAL original estimator: quarter phases and their adaptive
// guarded-power weights, together with the full-Hann phase correlation at the
// prediction. The weight derivative vanishes at a noiseless linear phase fit,
// but is retained for the observed residuals. This is a local plug-in sandwich,
// not finite-SNR covariance conditional on passing detection/unwrap selection.
bool slope_covariance(const Window& full,const Window& part,
    const HelperAidedPhaseConfig& c,const Pilot& high,const Pilot& low,
    HelperAidedPhaseResult& out) {
    const std::array<const Pilot*,2> pilots{&high,&low};
    const std::size_t n=full.w.size();
    out.work_sample_products+=40*n;
    const double fs=c.sample_rate,center=static_cast<double>(n-1)/(2*fs);
    const double noise_variance=fs*std::max(high.raw_noise_density,low.raw_noise_density);
    const double process_scale=c.phase_noise_radians_per_sqrt_second*
        c.phase_noise_radians_per_sqrt_second/fs;
    if(!(noise_variance>0)||!std::isfinite(noise_variance)||!std::isfinite(process_scale))return false;
    std::array<C,2> oscillators{},steps{},model{},model_steps{};
    std::array<double,2> full_power_gain{};
    std::array<C,2> full_frequency_gain{},full_phase_gain{};
    std::array<std::array<C,4>,2> quarter_frequency_gain{};
    for(std::size_t k=0;k<2;++k) {
        const auto& p=*pilots[k];
        if(!std::isfinite(p.frequency_date_offset)||std::abs(p.frequency_date_offset)>center||
           !(std::norm(p.center_correlation)>0))return false;
        oscillators[k]=std::polar(1.,tau*p.prediction_hz*center);
        steps[k]=std::polar(1.,-tau*p.prediction_hz/fs);
        C response{},rotation=std::polar(1.,-tau*p.residual*center);
        const C response_step=std::polar(1.,tau*p.residual/fs);
        for(std::size_t i=0;i<n;++i){response+=full.w[i]*rotation;rotation*=response_step;}
        response/=full.sum;
        if(!(std::abs(response)>.25)||!std::isfinite(std::abs(response)))return false;
        // The paired fitted-tone model gives the common phase-process
        // derivative. It includes finite cross-pilot leakage. Unknown other
        // signals/changing transfer remain outside this local process model.
        const C amplitude=p.center_correlation/response;
        model[k]=amplitude*std::polar(1.,-tau*p.frequency*center);
        model_steps[k]=std::polar(1.,tau*p.frequency/fs);
        for(std::size_t j=0;j<4;++j) {
            const double local_power=std::norm(p.subcorrelations[j]);
            const double full_power=std::norm(p.center_correlation);
            if(!(local_power>0)||!std::isfinite(local_power))return false;
            // min(P_full,P_quarter) has no unique derivative exactly at a
            // material kink. The estimator remains valid; only this optional
            // local covariance is declined in that case.
            if(std::abs(local_power-full_power)<=32*std::numeric_limits<double>::epsilon()*
               std::max(local_power,full_power)&&
               std::abs(p.slope_power_gain[j])*std::max(local_power,full_power)>1e-8)return false;
            if(!p.slope_local_power[j])full_power_gain[k]+=p.slope_power_gain[j];
            quarter_frequency_gain[k][j]=p.slope_phase_gain[j]/p.subcorrelations[j];
            if(p.slope_local_power[j])quarter_frequency_gain[k][j]+=C(0,2*p.slope_power_gain[j])*
                std::conj(p.subcorrelations[j]);
            quarter_frequency_gain[k][j]/=part.sum;
        }
        full_frequency_gain[k]=C(0,2*full_power_gain[k])*std::conj(p.center_correlation)/full.sum;
        full_phase_gain[k]=1./(full.sum*p.center_correlation);
    }
    Matrix4 thermal{},process{};Matrix3 process_reduced{};
    std::vector<std::array<double,4>> phase_influence(n);
    for(std::size_t i=0;i<n;++i) {
        std::array<C,4> raw{};
        for(std::size_t k=0;k<2;++k) {
            raw[k]=full.w[i]*full_frequency_gain[k]*oscillators[k];
            raw[k+2]=full.w[i]*full_phase_gain[k]*oscillators[k];
            if(i<4*part.w.size()) {
                const std::size_t j=i/part.w.size();
                raw[k]+=part.w[i%part.w.size()]*quarter_frequency_gain[k][j]*oscillators[k];
            }
            oscillators[k]*=steps[k];
        }
        const C paired_signal=model[0]+model[1];
        for(std::size_t a=0;a<4;++a) {
            // d theta_hat = Im(J dx), and dx=i*s*dB for common phase dB.
            phase_influence[i][a]=std::real(raw[a]*paired_signal);
            for(std::size_t b=0;b<=a;++b)
                thermal[a][b]+=std::real(raw[a]*std::conj(raw[b]));
        }
        for(std::size_t k=0;k<2;++k)model[k]*=model_steps[k];
    }
    std::array<double,4> tail{};
    for(std::size_t i=n;i-->0;) {
        for(std::size_t a=0;a<4;++a)tail[a]+=phase_influence[i][a];
        for(std::size_t a=0;a<4;++a)for(std::size_t b=0;b<=a;++b)
            process[a][b]+=tail[a]*tail[b];
        const std::array<double,3> reduced{tail[0],tail[1],tail[2]-tail[3]};
        for(std::size_t a=0;a<3;++a)for(std::size_t b=0;b<=a;++b)
            process_reduced[a][b]+=reduced[a]*reduced[b];
    }
    // The inflation is fixed for this aperture and is a diagonal congruence
    // of the same raw Gram matrix. Apply it and the common noise/process
    // scales once, rather than to each sample product. In particular, retain
    // the separate directly accumulated differential process Gram: subtracting
    // nearly equal common-phase entries would lose its small residual.
    const std::array<double,4> inflation{high.inflation,low.inflation,high.inflation,low.inflation};
    for(std::size_t a=0;a<4;++a)for(std::size_t b=0;b<=a;++b) {
        thermal[a][b]*=noise_variance*inflation[a]*inflation[b];
        process[a][b]*=process_scale;
        if(!std::isfinite(thermal[a][b])||!std::isfinite(process[a][b]))return false;
        thermal[b][a]=thermal[a][b];process[b][a]=process[a][b];
    }
    for(std::size_t a=0;a<3;++a)for(std::size_t b=0;b<=a;++b) {
        process_reduced[a][b]*=process_scale;
        if(!std::isfinite(process_reduced[a][b]))return false;
        process_reduced[b][a]=process_reduced[a][b];
    }
    out.pilot_thermal_covariance=thermal;out.thermal_covariance=difference_covariance(thermal);
    out.pilot_process_covariance=process;out.process_covariance=process_reduced;
    out.thermal_covariance_valid=true;out.process_covariance_valid=true;
    return true;
}

Pilot measure_pilot(std::span<const std::complex<float>> samples,const Window& full,
    const Window& part,const Window& frequency_window,const HelperAidedPhaseConfig& c,double prediction,double prior_sigma,
    double mean_power,std::size_t& work) {
    Pilot out;out.prediction_hz=prediction;
    const double fs=c.sample_rate,duration=static_cast<double>(samples.size())/fs;
    const double center_offset=static_cast<double>(samples.size()-1)/(2*fs);
    const double effective=full.effective_seconds(fs),part_effective=part.effective_seconds(fs);
    const auto center=correlate(samples,full,fs,prediction,-center_offset);
    out.center_correlation=center;
    work+=samples.size();
    double noise_density=0;
    for(unsigned k=0;k<4;++k)for(int sign:{-1,1}) {
        const double offset=(c.noise_inner_offset_hz+k*c.noise_offset_spacing_hz)*sign;
        noise_density+=std::norm(correlate(samples,full,fs,prediction+offset,-center_offset))*effective;
        work+=samples.size();
    }
    // A finite numerical floor accompanies the inflated matched-taper local
    // estimate. Distant bright signals are not misclassified as white noise;
    // Hann sidelobe leakage is instead present in the SAME fitted projection.
    out.raw_noise_density=std::max(noise_density/8,mean_power*1e-14/(2*fs));
    noise_density=2*out.raw_noise_density;
    const double power=std::norm(center);
    if(!(noise_density>0) || !std::isfinite(noise_density) || !(power>0))return out;
    const double cn0=power/noise_density;
    if(!(cn0*effective>=c.minimum_coherent_energy))return out;
    constexpr std::size_t parts=4;
    std::array<double,parts> times{},angles{},variance{},weights{};
    double subpower=0;
    for(std::size_t k=0;k<parts;++k) {
        const std::size_t offset=k*part.w.size();
        const double t=(static_cast<double>(offset)+static_cast<double>(part.w.size()-1)/2)/fs-center_offset;
        const auto z=correlate(samples.subspan(offset,part.w.size()),part,fs,prediction,
                               static_cast<double>(offset)/fs-center_offset);
        out.subcorrelations[k]=z;
        work+=part.w.size();
        const double local_power=std::norm(z);
        subpower+=local_power;
        const double guarded_power=std::min(power,local_power);
        out.slope_local_power[k]=local_power<power;
        if(!(guarded_power*part_effective/noise_density>=2))return out;
        times[k]=t;angles[k]=std::arg(z);
        if(k)angles[k]=angles[k-1]+std::remainder(angles[k]-angles[k-1],tau);
        variance[k]=noise_density/(2*guarded_power*part_effective);
        weights[k]=1/variance[k];
    }
    // A residual that aliases between sub-apertures loses full-window
    // coherence. This independent check guards the small-residual unwrap.
    if(power<.5*subpower/static_cast<double>(parts)) {
        out.failure=HelperAidedPhaseFailure::residual_frequency;return out;
    }
    double sw=0,st=0,sp=0;
    for(std::size_t k=0;k<parts;++k){sw+=weights[k];st+=weights[k]*times[k];sp+=weights[k]*angles[k];}
    const double tmean=st/sw,pmean=sp/sw;
    double sxx=0,sxp=0;
    for(std::size_t k=0;k<parts;++k) {
        const double t=times[k]-tmean;
        sxx+=weights[k]*t*t;sxp+=weights[k]*t*(angles[k]-pmean);
    }
    if(!(sxx>0))return out;
    const double residual=sxp/(tau*sxx);
    const double white_sigma=1/(tau*std::sqrt(sxx));
    const double common_frequency_sigma=std::sqrt(3*c.phase_noise_radians_per_sqrt_second*
        c.phase_noise_radians_per_sqrt_second/(10*pi*pi*duration));
    const double allowed=c.residual_check_sigma*std::hypot(prior_sigma,
        std::hypot(white_sigma,common_frequency_sigma));
    if(std::abs(residual)>c.maximum_residual_frequency_hz || std::abs(residual)>allowed) {
        out.failure=HelperAidedPhaseFailure::residual_frequency;return out;
    }
    double score=0;
    for(std::size_t k=0;k<parts;++k) {
        const double e=angles[k]-pmean-tau*residual*(times[k]-tmean);
        const double process=c.phase_noise_radians_per_sqrt_second*c.phase_noise_radians_per_sqrt_second*duration;
        score+=e*e/(variance[k]+process);
    }
    out.score=score;
    if(score>c.maximum_phase_fit_score){out.failure=HelperAidedPhaseFailure::phase_evolution;return out;}
    // This is a data-only slope likelihood: the prediction is a coordinate
    // translation and a consistency gate, never an independent Bayesian datum.
    const double inflation=std::sqrt(std::max(1.,score/2));out.inflation=inflation;
    if(c.compute_slope_covariance&&c.frequency_method==HelperAidedFrequencyMethod::subaperture_slope) {
        double quadratic_response=0;
        const double power_weight_scale=2*part_effective/noise_density;
        for(std::size_t k=0;k<parts;++k) {
            const double t=times[k]-tmean;
            const double e=angles[k]-pmean-tau*residual*t;
            out.slope_phase_gain[k]=weights[k]*t/(tau*sxx);
            out.slope_power_gain[k]=power_weight_scale*t*e/(tau*sxx);
            quadratic_response+=weights[k]*t*times[k]*times[k];
        }
        out.frequency_date_offset=quadratic_response/(2*sxx);
    }
    out.frequency=prediction+residual;out.frequency_white_sigma=std::hypot(white_sigma*inflation,10e-6);
    out.center_phase=std::arg(center);out.phase_amplitude=std::abs(center);
    out.center_white_sigma=std::sqrt(noise_density/(2*power*effective))*inflation;
    if(c.frequency_method!=HelperAidedFrequencyMethod::subaperture_slope) {
        if(!refine_frequency(samples,frequency_window,c,prediction,residual,out,work)) {
            out.failure=HelperAidedPhaseFailure::residual_frequency;return out;
        }
        if(c.frequency_method==HelperAidedFrequencyMethod::partitioned_hann) {
            double frequency_noise=0;
            const double fit_effective=frequency_window.effective_seconds(fs);
            for(unsigned k=0;k<4;++k)for(int sign:{-1,1}) {
                const double offset=(c.noise_inner_offset_hz+k*c.noise_offset_spacing_hz)*sign;
                frequency_noise+=std::norm(correlate(samples,frequency_window,fs,out.frequency+offset,-center_offset))*fit_effective;
                work+=samples.size();
            }
            // A composite taper has different sidelobes. Use its matching
            // probes too, retaining whichever local white PSD is larger.
            out.raw_noise_density=std::max(out.raw_noise_density,frequency_noise/8);
            noise_density=2*out.raw_noise_density;
        }
        // The old slope remains a separate data-only evolution witness. A
        // sharp objective maximum is not evidence of a constant-tone model.
        const double refined_sigma=std::sqrt(noise_density*fs*frequency_window.square_time_square_sum/
            (2*std::norm(C(out.frequency_amplitude,0))*tau*tau*
             frequency_window.time_square_sum*frequency_window.time_square_sum))*inflation;
        const double check=c.residual_check_sigma*std::hypot(refined_sigma,white_sigma*inflation);
        if(std::abs(out.residual-residual)>check || std::abs(out.residual)>allowed) {
            out.failure=HelperAidedPhaseFailure::residual_frequency;return out;
        }
        // Phase continues to use the original full Hann taper, now centered
        // on the fitted frequency. The composite frequency taper is therefore
        // not allowed to make the phase anchor less leakage-resistant.
        const auto phase=correlate(samples,full,fs,out.frequency,-center_offset);work+=samples.size();
        out.center_phase=std::arg(phase);out.phase_amplitude=std::abs(phase);
        if(!(out.phase_amplitude>0))return out;
        out.frequency_white_sigma=std::hypot(refined_sigma,10e-6);
        out.center_white_sigma=std::sqrt(noise_density/(2*std::norm(phase)*effective))*inflation;
    }
    out.first_sigma=out.center_white_sigma+tau*center_offset*out.frequency_white_sigma;
    out.cn0=out.phase_amplitude*out.phase_amplitude/noise_density;
    if(!(out.cn0*effective>=c.minimum_coherent_energy))return out;
    if(c.frequency_method==HelperAidedFrequencyMethod::subaperture_slope)out.residual=residual;
    out.valid=true;out.failure=HelperAidedPhaseFailure::none;
    return out;
}
}

HelperAidedPhaseResult estimate_helper_aided_phase(std::span<const std::complex<float>> samples,
    const HelperAidedPhaseConfig& c,const HelperAidedPhasePrediction& p,double shift,double start) {
    HelperAidedPhaseResult out;
    const bool finite_config=std::isfinite(c.sample_rate)&&c.sample_rate>0&&
        std::isfinite(c.high_hz)&&std::isfinite(c.low_hz)&&c.high_hz>c.low_hz&&
        std::isfinite(c.phase_noise_radians_per_sqrt_second)&&c.phase_noise_radians_per_sqrt_second>=0&&
        std::isfinite(c.maximum_residual_frequency_hz)&&c.maximum_residual_frequency_hz>0&&
        std::isfinite(c.residual_check_sigma)&&c.residual_check_sigma>0&&
        std::isfinite(c.minimum_coherent_energy)&&c.minimum_coherent_energy>=8&&
        std::isfinite(c.maximum_phase_fit_score)&&c.maximum_phase_fit_score>0&&
        std::isfinite(c.noise_inner_offset_hz)&&c.noise_inner_offset_hz>0&&
        std::isfinite(c.noise_offset_spacing_hz)&&c.noise_offset_spacing_hz>0&&
        c.maximum_refinement_iterations>0&&c.maximum_refinement_iterations<=12&&
        std::isfinite(c.refinement_step_limit_hz)&&c.refinement_step_limit_hz>0&&
        std::isfinite(c.refinement_tolerance_hz)&&c.refinement_tolerance_hz>0&&
        c.refinement_tolerance_hz<=1e-3&&
        std::isfinite(c.slope_frequency_drift_bound_hz_per_second)&&c.slope_frequency_drift_bound_hz_per_second>=0&&
        (c.frequency_method==HelperAidedFrequencyMethod::subaperture_slope||
         c.frequency_method==HelperAidedFrequencyMethod::whole_hann||
         c.frequency_method==HelperAidedFrequencyMethod::partitioned_hann);
    if(!finite_config||!std::isfinite(shift)||shift==0||!std::isfinite(start))return out;
    if(samples.size()>c.maximum_samples){out.failure=HelperAidedPhaseFailure::work_limit;return out;}
    const double fs=c.sample_rate,duration=static_cast<double>(samples.size())/fs;
    if(samples.size()<128||duration<.1||duration>1||!std::isfinite(start+duration)||start+duration<=start)return out;
    if(!p.valid){out.failure=HelperAidedPhaseFailure::prediction_unavailable;return out;}
    const double outer=c.noise_inner_offset_hz+3*c.noise_offset_spacing_hz;
    if(!std::isfinite(p.high_frequency_hz)||!std::isfinite(p.low_frequency_hz)||
       !std::isfinite(p.high_sigma_hz)||p.high_sigma_hz<0||!std::isfinite(p.low_sigma_hz)||p.low_sigma_hz<0||
       p.high_frequency_hz<=p.low_frequency_hz||
       p.low_frequency_hz-outer<=-fs/2||p.high_frequency_hz+outer>=fs/2||
       outer>=.4*(p.high_frequency_hz-p.low_frequency_hz))return out;
    double mean_power=0;
    for(auto x:samples) {
        if(!std::isfinite(x.real())||!std::isfinite(x.imag()))return out;
        mean_power+=std::norm(C(x.real(),x.imag()));
    }
    mean_power/=static_cast<double>(samples.size());
    Window full(samples.size());full.date(fs);
    const Window part(samples.size()/4);
    std::optional<Window> composite;
    if(c.frequency_method==HelperAidedFrequencyMethod::partitioned_hann) {
        composite.emplace(samples.size(),4U);composite->date(fs);
    }
    const Window& frequency_window=composite?*composite:full;
    auto high=measure_pilot(samples,full,part,frequency_window,c,p.high_frequency_hz,p.high_sigma_hz,mean_power,out.work_sample_products);
    if(!high.valid){out.failure=high.failure;return out;}
    auto low=measure_pilot(samples,full,part,frequency_window,c,p.low_frequency_hz,p.low_sigma_hz,mean_power,out.work_sample_products);
    if(!low.valid){out.failure=low.failure;return out;}
    if(c.frequency_method!=HelperAidedFrequencyMethod::subaperture_slope) {
        if(!covariance(full,frequency_window,c,high,low,out))return out;
        high.frequency_white_sigma=std::hypot(std::sqrt(out.pilot_thermal_covariance[0][0]),10e-6);
        low.frequency_white_sigma=std::hypot(std::sqrt(out.pilot_thermal_covariance[1][1]),10e-6);
        high.center_white_sigma=std::sqrt(out.pilot_thermal_covariance[2][2]);
        low.center_white_sigma=std::sqrt(out.pilot_thermal_covariance[3][3]);
    } else if(c.compute_slope_covariance) {
        // Optional summary only: the original point estimates, gates and
        // historical marginal errors remain byte-for-byte the same path.
        (void)slope_covariance(full,part,c,high,low,out);
    }
    const double center_offset=static_cast<double>(samples.size()-1)/(2*fs);
    high.first_sigma=high.center_white_sigma+tau*center_offset*high.frequency_white_sigma;
    low.first_sigma=low.center_white_sigma+tau*center_offset*low.frequency_white_sigma;
    auto& m=out.measurement;
    m.valid=true;m.time_seconds=start+duration/2;m.first_sample_time_seconds=start;m.end_sample_time_seconds=start+duration;
    m.shift_hz=shift;m.high_frequency_hz=high.frequency;m.low_frequency_hz=low.frequency;
    m.high_phase_radians=std::remainder(high.center_phase-tau*high.frequency*center_offset,tau);
    m.low_phase_radians=std::remainder(low.center_phase-tau*low.frequency*center_offset,tau);
    m.high_sigma_hz=high.frequency_white_sigma;m.low_sigma_hz=low.frequency_white_sigma;
    m.common_phase_frequency_sigma_hz=c.frequency_method==HelperAidedFrequencyMethod::subaperture_slope?
        std::sqrt(3*c.phase_noise_radians_per_sqrt_second*c.phase_noise_radians_per_sqrt_second/(10*pi*pi*duration)):
        c.phase_noise_radians_per_sqrt_second*std::sqrt(frequency_window.brownian_frequency_variance_factor);
    m.high_cn0_db_hz=10*std::log10(high.cn0);m.low_cn0_db_hz=10*std::log10(low.cn0);
    out.center_time_seconds=start+center_offset;
    out.high_frequency_time_seconds=out.center_time_seconds+high.frequency_date_offset;
    out.low_frequency_time_seconds=out.center_time_seconds+low.frequency_date_offset;
    out.frequency_dates_valid=c.frequency_method!=HelperAidedFrequencyMethod::subaperture_slope||c.compute_slope_covariance;
    out.high_center_phase_radians=high.center_phase;out.low_center_phase_radians=low.center_phase;
    out.common_center_phase_sigma_radians=c.phase_noise_radians_per_sqrt_second*std::sqrt(duration);
    out.high_center_phase_sigma_radians=std::hypot(high.center_white_sigma,out.common_center_phase_sigma_radians);
    out.low_center_phase_sigma_radians=std::hypot(low.center_white_sigma,out.common_center_phase_sigma_radians);
    out.high_first_phase_sigma_radians=std::hypot(high.first_sigma,out.common_center_phase_sigma_radians);
    out.low_first_phase_sigma_radians=std::hypot(low.first_sigma,out.common_center_phase_sigma_radians);
    out.phase_difference_sigma_radians=high.center_white_sigma+low.center_white_sigma;
    out.high_residual_hz=high.residual;out.low_residual_hz=low.residual;
    out.high_phase_fit_score=high.score;out.low_phase_fit_score=low.score;
    out.high_raw_noise_density=high.raw_noise_density;out.low_raw_noise_density=low.raw_noise_density;
    out.high_refinement_iterations=high.iterations;out.low_refinement_iterations=low.iterations;
    out.common_frequency_process_variance_factor=c.frequency_method==HelperAidedFrequencyMethod::subaperture_slope?
        3/(10*pi*pi*duration):frequency_window.brownian_frequency_variance_factor;
    if(c.frequency_method!=HelperAidedFrequencyMethod::subaperture_slope||c.compute_slope_covariance) {
        out.high_frequency_model_half_width_hz=10e-6;out.low_frequency_model_half_width_hz=10e-6;
        if(c.frequency_method==HelperAidedFrequencyMethod::subaperture_slope) {
            const double date_radius=c.slope_frequency_drift_bound_hz_per_second*duration;
            if(!std::isfinite(date_radius))return HelperAidedPhaseResult{};
            out.high_frequency_model_half_width_hz+=date_radius;
            out.low_frequency_model_half_width_hz+=date_radius;
        }
    }
    out.valid=true;out.fallback_required=false;out.failure=HelperAidedPhaseFailure::none;
    return out;
}

HelperTransientPhaseResult estimate_helper_transient_phase(
    std::span<const std::complex<float>> samples,const HelperAidedPhaseConfig& c,
    const HelperTransientPhaseConfig& tc,const HelperAidedPhasePrediction& prediction,
    const HelperAidedPhaseResult& baseline,double start) {
    HelperTransientPhaseResult out;
    constexpr std::size_t segments=8;
    if(!baseline.valid||!baseline.measurement.valid||!prediction.valid||samples.size()<segments*128||
       samples.size()>24000||samples.size()>c.maximum_samples||!std::isfinite(start)||
       !std::isfinite(c.sample_rate)||c.sample_rate<=0||
       !std::isfinite(c.phase_noise_radians_per_sqrt_second)||c.phase_noise_radians_per_sqrt_second<0||
       !std::isfinite(tc.detection_sigma)||tc.detection_sigma<1||tc.detection_sigma>10||
       !std::isfinite(tc.model_half_width_radians)||tc.model_half_width_radians<0||
       tc.model_half_width_radians>.5||tc.minimum_contiguous_segments<3||
       tc.minimum_contiguous_segments>segments||tc.excluded_neighbor_segments>2)return out;
    const double fs=c.sample_rate;
    const std::size_t size=samples.size()/segments;
    const double center=static_cast<double>(samples.size()-1)/(2*fs);
    const auto& m=baseline.measurement;
    if(!std::isfinite(prediction.high_frequency_hz)||!std::isfinite(prediction.low_frequency_hz)||
       !std::isfinite(prediction.high_sigma_hz)||prediction.high_sigma_hz<0||
       !std::isfinite(prediction.low_sigma_hz)||prediction.low_sigma_hz<0||
       !std::isfinite(baseline.high_raw_noise_density)||!(baseline.high_raw_noise_density>0)||
       !std::isfinite(baseline.low_raw_noise_density)||!(baseline.low_raw_noise_density>0)||
       !std::isfinite(m.high_sigma_hz)||m.high_sigma_hz<0||
       !std::isfinite(m.low_sigma_hz)||m.low_sigma_hz<0||
       !std::isfinite(m.common_phase_frequency_sigma_hz)||m.common_phase_frequency_sigma_hz<0)return out;
    const Window part(size);
    const double effective=part.effective_seconds(fs);
    std::array<std::array<C,segments>,2> z{};
    const std::array<double,2> frequency{prediction.high_frequency_hz,prediction.low_frequency_hz};
    const std::array<double,2> density{baseline.high_raw_noise_density,baseline.low_raw_noise_density};
    const std::array<double,2> baseline_sigma{baseline.high_center_phase_sigma_radians,baseline.low_center_phase_sigma_radians};
    for(std::size_t p=0;p<2;++p)for(std::size_t j=0;j<segments;++j) {
        const double first_offset=static_cast<double>(j*size)/fs-center;
        z[p][j]=correlate(samples.subspan(j*size,size),part,fs,frequency[p],first_offset);
        if(!std::isfinite(z[p][j].real())||!std::isfinite(z[p][j].imag()))return out;
    }
    std::array<double,2> reference{},segment_sigma{};
    for(std::size_t p=0;p<2;++p) {
        C sum{};std::array<double,segments> magnitudes{};
        for(std::size_t j=0;j<segments;++j){sum+=z[p][j];magnitudes[j]=std::abs(z[p][j]);}
        if(!(std::abs(sum)>0))return out;
        std::sort(magnitudes.begin(),magnitudes.end());
        const double amplitude=.5*(magnitudes[segments/2-1]+magnitudes[segments/2]);
        if(!(amplitude>0)||!std::isfinite(amplitude))return out;
        reference[p]=std::arg(sum);
        // Three bounded Huber steps pool all short observations. No segment
        // has to satisfy the full-dwell acquisition threshold individually.
        for(unsigned iteration=0;iteration<3;++iteration) {
            C robust{};
            for(const auto& value:z[p]) {
                const double e=std::remainder(std::arg(value)-reference[p],tau);
                const double weight=std::min(1.,.45/std::max(std::abs(e),1e-12));
                robust+=weight*std::polar(1.,e);
            }
            if(!(std::abs(robust)>0))return out;
            reference[p]=std::remainder(reference[p]+std::arg(robust),tau);
        }
        segment_sigma[p]=std::sqrt(density[p]/(amplitude*amplitude*effective));
        if(!std::isfinite(segment_sigma[p])||!std::isfinite(baseline_sigma[p]))return out;
    }
    std::array<bool,segments> suspect{};
    for(std::size_t j=0;j<segments;++j)for(std::size_t p=0;p<2;++p) {
        // Sum the reused baseline and segment uncertainties rather than assume
        // independence. This fixed witness is not a calibrated false-alarm
        // probability and cannot establish that unflagged samples are clean.
        const double bound=tc.detection_sigma*(segment_sigma[p]+baseline_sigma[p])+tc.model_half_width_radians;
        if(std::abs(std::remainder(std::arg(z[p][j])-reference[p],tau))>bound)suspect[j]=true;
    }
    out.evaluated=true;
    for(bool flag:suspect)if(flag)++out.flagged_segments;
    if(!out.flagged_segments)return out; // no change: preserve baseline path
    out.disturbance_detected=true;
    auto excluded=suspect;
    for(std::size_t j=0;j<segments;++j)if(suspect[j])
        for(std::size_t k=1;k<=tc.excluded_neighbor_segments;++k){
            if(j>=k)excluded[j-k]=true;
            if(j+k<segments)excluded[j+k]=true;
        }
    std::size_t best_first=0,best_end=0,first=0;
    for(std::size_t j=0;j<=segments;++j) {
        if(j<segments&&!excluded[j])continue;
        if(j-first>best_end-best_first){best_first=first;best_end=j;}
        first=j+1;
    }
    if(best_end-best_first<tc.minimum_contiguous_segments)return out;
    const std::size_t global_guard=static_cast<std::size_t>(std::ceil(.04*fs));
    const std::size_t lo=std::max(best_first*size,global_guard);
    const std::size_t hi=std::min(best_end*size,samples.size()-std::min(global_guard,samples.size()));
    if(hi<=lo||static_cast<double>(hi-lo)/fs<.10)return out;
    const Window selected(hi-lo);
    const double selected_center=static_cast<double>(hi-lo-1)/(2*fs);
    const double selected_effective=selected.effective_seconds(fs);
    const auto sub=samples.subspan(lo,hi-lo);
    std::array<C,2> selected_z{};
    for(std::size_t p=0;p<2;++p) {
        selected_z[p]=correlate(sub,selected,fs,frequency[p],-selected_center);
        if(!(std::norm(selected_z[p])>0)||!std::isfinite(std::norm(selected_z[p])))return out;
    }
    const double anchor=start+static_cast<double>(lo)/fs+selected_center;
    const double distance=std::abs(anchor-baseline.center_time_seconds);
    // For 32 approximately Gaussian phase hypotheses, z=3*1.7 gives a
    // union-bound tail below 1e-4 without assuming independent windows. The
    // amplitude and local PSD are plug-in estimates and the phase response is
    // linearized: this is an engineering allowance conditional on those
    // assumptions, not calibrated finite-SNR post-selection coverage.
    constexpr double selection_factor=1.7;
    const auto phase_sigma=[&](std::size_t p,double frequency_sigma){
        const double local=selection_factor*std::sqrt(density[p]/(std::norm(selected_z[p])*selected_effective));
        // Bound both transport to the selected anchor and evolution across
        // its full proposed interval. The conditioner separately erodes this
        // interval by its interpolation halo and checks the clock map.
        return local+tau*(distance+selected_center)*(frequency_sigma+m.common_phase_frequency_sigma_hz)+
            c.phase_noise_radians_per_sqrt_second*std::sqrt(static_cast<double>(hi-lo)/fs);
    };
    out.high_sigma_radians=phase_sigma(0,prediction.high_sigma_hz);
    out.low_sigma_radians=phase_sigma(1,prediction.low_sigma_hz);
    if(!std::isfinite(out.high_sigma_radians)||!std::isfinite(out.low_sigma_radians))return out;
    out.support_first_sample=lo;out.support_end_sample=hi;out.anchor_time_seconds=anchor;
    out.high_phase_radians=std::arg(selected_z[0]);out.low_phase_radians=std::arg(selected_z[1]);
    out.model_half_width_radians=tc.model_half_width_radians;out.valid=true;return out;
}
}
