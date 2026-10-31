#include "datapump/helper_joint_dwell.hpp"
#include "helper_live_probe_fixture.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cfenv>
#include <cmath>
#include <complex>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <thread>

namespace h=datapump::helper;namespace p=helper_live_probe;
namespace {
void require(bool v,const char* what){if(!v)throw std::runtime_error(what);}
double wrap(double x){return std::remainder(x,2*std::numbers::pi);}
void window_cache_control(){
    p::Config c;c.seed=1700000001U;c.expanded_schedule_key=true;c.path_loss_db=180;
    p::Source source(c);const auto d=source.generate(0);
    constexpr std::size_t start=480;const double fs=p::sample_rate;
    h::HelperAidedPhaseConfig ac;ac.sample_rate=fs;ac.compute_slope_covariance=true;
    ac.phase_noise_radians_per_sqrt_second=.05*std::numbers::pi/180;
    ac.slope_frequency_drift_bound_hz_per_second=.001;
    h::HelperJointDwellConfig jc;jc.sample_rate=fs;
    jc.phase_noise_radians_per_sqrt_second=ac.phase_noise_radians_per_sqrt_second;
    jc.frequency_drift_bound_hz_per_second=.001;
    const auto prepare=[&](std::size_t count){
        const double center=d.first_seconds+(static_cast<double>(start)+(static_cast<double>(count)-1)/2)/fs;
        h::HelperAidedPhasePrediction prior{true,
            2400*source.audio_scale_at(center)+source.shift_error_hz(d.shift_hz,center)+.1,
            550*source.audio_scale_at(center)+source.shift_error_hz(d.shift_hz,center)+.1,.2,.2};
        const auto input=std::span<const std::complex<float>>(d.samples).subspan(start,count);
        const auto base=h::estimate_helper_aided_phase(input,ac,prior,d.shift_hz,d.first_seconds+start/fs);
        require(base.valid&&base.thermal_covariance_valid&&base.process_covariance_valid,"cache control failed to acquire pilot");
        return std::pair{prior,base};
    };
    // Exercise simultaneous cold initialization, then require all current-data
    // outputs, including shared-sample covariance, to match exactly.
    constexpr std::size_t count=8640,threads_count=4;
    const auto [prior,base]=prepare(count);
    const auto input=std::span<const std::complex<float>>(d.samples).subspan(start,count);
    std::array<h::HelperJointDwellResult,threads_count> outputs{};
    std::array<std::thread,threads_count> threads;
    std::atomic<unsigned> ready{0};std::atomic<bool> go{false};
    for(std::size_t i=0;i<threads_count;++i)threads[i]=std::thread([&,i]{
        ready.fetch_add(1,std::memory_order_relaxed);
        while(!go.load(std::memory_order_acquire))std::this_thread::yield();
        outputs[i]=h::estimate_helper_joint_dwell(input,jc,prior,base);
    });
    while(ready.load(std::memory_order_relaxed)!=threads_count)std::this_thread::yield();
    go.store(true,std::memory_order_release);
    for(auto& thread:threads)thread.join();
    const auto equal=[](const h::HelperJointDwellResult& a,const h::HelperJointDwellResult& b){
        return a.valid&&b.valid&&a.fit_score==b.fit_score&&
            a.common_phase_model_half_width_radians==b.common_phase_model_half_width_radians&&
            a.phase_difference_model_half_width_radians==b.phase_difference_model_half_width_radians&&
            a.observation.measurement.high_frequency_hz==b.observation.measurement.high_frequency_hz&&
            a.observation.measurement.low_frequency_hz==b.observation.measurement.low_frequency_hz&&
            a.observation.high_center_phase_radians==b.observation.high_center_phase_radians&&
            a.observation.low_center_phase_radians==b.observation.low_center_phase_radians&&
            a.observation.high_center_phase_sigma_radians==b.observation.high_center_phase_sigma_radians&&
            a.observation.low_center_phase_sigma_radians==b.observation.low_center_phase_sigma_radians&&
            a.observation.high_frequency_model_half_width_hz==b.observation.high_frequency_model_half_width_hz&&
            a.observation.low_frequency_model_half_width_hz==b.observation.low_frequency_model_half_width_hz&&
            a.observation.pilot_thermal_covariance==b.observation.pilot_thermal_covariance&&
            a.observation.pilot_process_covariance==b.observation.pilot_process_covariance&&
            a.observation.thermal_covariance==b.observation.thermal_covariance&&
            a.observation.process_covariance==b.observation.process_covariance;
    };
    for(std::size_t i=1;i<threads_count;++i)require(equal(outputs[0],outputs[i]),"concurrent cold cache changed local result");
    require(equal(outputs[0],h::estimate_helper_joint_dwell(input,jc,prior,base)),"warm cache changed local result");
    auto uncached=jc;uncached.use_static_window_cache=false;
    require(equal(outputs[0],h::estimate_helper_joint_dwell(input,uncached,prior,base)),
        "cached and per-call windows changed local result");
    // A different valid aperture must retain dynamic coefficient construction.
    constexpr std::size_t alternate=8000;const auto [alt_prior,alt_base]=prepare(alternate);
    const auto alt=std::span<const std::complex<float>>(d.samples).subspan(start,alternate);
    const auto first=h::estimate_helper_joint_dwell(alt,jc,alt_prior,alt_base);
    require(equal(first,h::estimate_helper_joint_dwell(alt,jc,alt_prior,alt_base)),"alternate aperture changed local result");
    // Rounding modes are thread-local on supported hosts. The implementation
    // bypasses the common cache outside FE_TONEAREST and preserves the mode.
    const int original=std::fegetround();
    if(original==FE_TONEAREST&&std::fesetround(FE_DOWNWARD)==0){
        (void)h::estimate_helper_joint_dwell(input,jc,prior,base);
        require(std::fegetround()==FE_DOWNWARD,"local estimator changed rounding mode");
        require(std::fesetround(original)==0,"could not restore rounding mode");
    }
}
void short_check(){
    unsigned admitted=0,rejected=0;double base_f2=0,joint_f2=0,base_p2=0,joint_p2=0;unsigned n=0;
    for(unsigned seed=1690000001U;seed<=1690000008U;++seed){
        p::Config c;c.seed=seed;c.expanded_schedule_key=true;c.path_loss_db=180;
        p::Source source(c);
        for(unsigned dwell=0;dwell<6;++dwell){auto d=source.generate(dwell);if(dwell<2)continue;
            constexpr std::size_t start=480,count=8640;const double fs=p::sample_rate;
            const double center=d.first_seconds+(static_cast<double>(start)+(count-1)/2.)/fs;
            const double high=2400*source.audio_scale_at(center)+source.shift_error_hz(d.shift_hz,center);
            const double low=550*source.audio_scale_at(center)+source.shift_error_hz(d.shift_hz,center);
            // Truth-offset prediction is restricted to this short feasibility
            // control. The actual receiver must use previous Helper history.
            const double delta=((seed+dwell)&1U)?.1:-.1;
            h::HelperAidedPhasePrediction prior{true,high+delta,low+delta,.2,.2};
            h::HelperAidedPhaseConfig ac;ac.sample_rate=fs;ac.compute_slope_covariance=true;
            ac.phase_noise_radians_per_sqrt_second=.05*std::numbers::pi/180;
            ac.slope_frequency_drift_bound_hz_per_second=.001;
            const auto input=std::span<const std::complex<float>>(d.samples).subspan(start,count);
            const auto base=h::estimate_helper_aided_phase(input,ac,prior,d.shift_hz,d.first_seconds+start/fs);
            if(!base.valid||!base.thermal_covariance_valid||!base.process_covariance_valid)continue;
            h::HelperJointDwellConfig jc;jc.sample_rate=fs;jc.phase_noise_radians_per_sqrt_second=ac.phase_noise_radians_per_sqrt_second;
            jc.frequency_drift_bound_hz_per_second=.001;
            const auto result=h::estimate_helper_joint_dwell(input,jc,prior,base);
            if(!result.valid){++rejected;continue;}++admitted;
            const auto& x=result.observation;
            require(x.valid&&x.thermal_covariance_valid&&x.process_covariance_valid&&x.frequency_dates_valid,"joint result lacks valid covariance/date");
            require(x.measurement.first_sample_time_seconds==base.measurement.first_sample_time_seconds&&x.measurement.end_sample_time_seconds==base.measurement.end_sample_time_seconds,"joint changed sample support");
            require(std::abs(x.high_frequency_time_seconds-x.center_time_seconds)<1e-12&&std::abs(x.low_frequency_time_seconds-x.center_time_seconds)<1e-12,"joint frequency dates are inconsistent");
            for(unsigned i=0;i<3;++i){require(x.thermal_covariance[i][i]>0&&x.process_covariance[i][i]>=0,"invalid covariance diagonal");for(unsigned j=0;j<3;++j)require(std::abs(x.thermal_covariance[i][j]-x.thermal_covariance[j][i])<1e-10,"thermal covariance asymmetric");}
            const auto idx=start+count/2;const auto phasor=(std::complex<double>(d.common_phasor[idx-1])+std::complex<double>(d.common_phasor[idx]))*.5;
            const double true_phase=wrap(std::arg(phasor)+2*std::numbers::pi*std::remainder(2400*source.tx_time(center),1.));
            base_f2+=std::pow(base.measurement.high_frequency_hz-high,2);joint_f2+=std::pow(x.measurement.high_frequency_hz-high,2);
            base_p2+=std::pow(wrap(base.high_center_phase_radians-true_phase),2);joint_p2+=std::pow(wrap(x.high_center_phase_radians-true_phase),2);++n;
            auto wrong=prior;wrong.high_frequency_hz+=3;require(!h::estimate_helper_joint_dwell(input,jc,wrong,base).valid,"inconsistent frequency prediction did not fallback");
        }
    }
    require(n>=20,"insufficient joint observations in feasibility control");
    std::cout<<"helper_joint_dwell_short common="<<n<<" accepted="<<admitted<<" rejected="<<rejected
        <<" baseline_frequency_rms_hz="<<std::sqrt(base_f2/n)<<" joint_frequency_rms_hz="<<std::sqrt(joint_f2/n)
        <<" baseline_phase_rms_deg="<<std::sqrt(base_p2/n)*180/std::numbers::pi
        <<" joint_phase_rms_deg="<<std::sqrt(joint_p2/n)*180/std::numbers::pi<<"\n";
}
}
int main(){try{window_cache_control();short_check();return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
