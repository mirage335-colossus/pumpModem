#include "datapump/helper_boundary_phase.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
namespace datapump::helper {
namespace { constexpr double tau=2*std::numbers::pi;using C=std::complex<double>;
bool good(double x){return std::isfinite(x);}
}
HelperBoundaryPhaseResult estimate_helper_boundary_support(std::span<const std::complex<float>> dwell,double start,
    const HelperAidedPhaseResult& a,const HelperBoundaryPhaseConfig& c){
    HelperBoundaryPhaseResult out; const auto n=dwell.size();
    if(!a.valid||!a.measurement.valid||!good(start)||!good(c.sample_rate)||c.sample_rate<=0||
       n<128||n>c.maximum_samples||c.maximum_samples>24000||!good(c.retune_guard_seconds)||c.retune_guard_seconds<0||
       !good(c.baseline_margin_seconds)||c.baseline_margin_seconds<=0||!good(c.edge_window_seconds)||c.edge_window_seconds<=0||
       !good(c.sigma_multiplier)||c.sigma_multiplier<3||!good(c.maximum_edge_error_radians)||c.maximum_edge_error_radians<=0||
       !good(c.bounded_model_error_radians)||c.bounded_model_error_radians<0||!good(a.high_raw_noise_density)||a.high_raw_noise_density<=0||
       !good(a.noise_psd_safety_factor)||a.noise_psd_safety_factor<1||!good(a.center_time_seconds)||
       !good(a.high_center_phase_radians)||!good(a.high_center_phase_sigma_radians)||a.high_center_phase_sigma_radians<0||
       !good(a.measurement.high_frequency_hz)||!good(a.measurement.high_sigma_hz)||a.measurement.high_sigma_hz<0||
       !good(a.measurement.common_phase_frequency_sigma_hz)||a.measurement.common_phase_frequency_sigma_hz<0)return out;
    const double fs=c.sample_rate,sec=static_cast<double>(n)/fs;
    if(c.retune_guard_seconds+c.edge_window_seconds>=sec/2||c.retune_guard_seconds+c.baseline_margin_seconds>=sec/2)return out;
    const auto guard=static_cast<std::size_t>(std::ceil(c.retune_guard_seconds*fs));
    const auto margin=static_cast<std::size_t>(std::ceil(c.baseline_margin_seconds*fs));
    const auto window=static_cast<std::size_t>(std::ceil(c.edge_window_seconds*fs));
    if(guard+window>=n||guard+margin>=n/2||window<16)return out;
    const double expected_start=start+static_cast<double>(guard)/fs;
    const double expected_end=start+static_cast<double>(n-guard)/fs;
    if(std::abs(a.measurement.first_sample_time_seconds-expected_start)>2/fs||
       std::abs(a.measurement.end_sample_time_seconds-expected_end)>2/fs||
       a.center_time_seconds<expected_start||a.center_time_seconds>=expected_end)return out;
    out.support_first_sample=guard+margin;out.support_end_sample=n-guard-margin;
    auto edge=[&](std::size_t first,double& residual,double& radius){
        C z{};double wsum=0,w2=0;const double mid=(static_cast<double>(window)-1)/2;
        const double date=start+(static_cast<double>(first)+mid)/fs;
        C osc=std::polar(1.,tau*a.measurement.high_frequency_hz*mid/fs);
        const C step=std::polar(1.,-tau*a.measurement.high_frequency_hz/fs);
        for(std::size_t i=0;i<window;++i){const auto x=dwell[first+i];if(!good(x.real())||!good(x.imag()))return false;
            const double w=.5-.5*std::cos(tau*static_cast<double>(i)/static_cast<double>(window-1));
            z+=C(x)*osc*w;wsum+=w;w2+=w*w;osc*=step;}
        if(!(std::norm(z)>0)||!(wsum>0)||!(w2>0))return false;
        const double eff=wsum*wsum/(fs*w2),amp2=std::norm(z)/(wsum*wsum);
        const double edge_sigma=std::sqrt(a.noise_psd_safety_factor*a.high_raw_noise_density/(2*eff*amp2));
        const double dt=std::abs(date-a.center_time_seconds);
        const double frequency_sigma=a.measurement.high_sigma_hz+a.measurement.common_phase_frequency_sigma_hz;
        residual=std::remainder(std::arg(z)-a.high_center_phase_radians-tau*a.measurement.high_frequency_hz*(date-a.center_time_seconds),tau);
        // L1 for shared observations, never an independence claim. Include
        // frequency transport from edge window center to the outer edge.
        radius=c.sigma_multiplier*(edge_sigma+a.high_center_phase_sigma_radians+tau*(dt+mid/fs)*frequency_sigma)+c.bounded_model_error_radians;
        return good(radius)&&good(residual)&&std::abs(residual)+radius<c.maximum_edge_error_radians;
    };
    out.early_admitted=edge(guard,out.early_phase_residual_radians,out.early_conditional_radius_radians);
    out.late_admitted=edge(n-guard-window,out.late_phase_residual_radians,out.late_conditional_radius_radians);
    if(out.early_admitted)out.support_first_sample=guard;
    if(out.late_admitted)out.support_end_sample=n-guard;
    out.evaluated=true;return out;
}
}
