// Exploratory local-taper study, not a receiver.  The fixture generates the
// actual Helper waveform; simulator truth is used only to place a controlled
// frequency prediction and score results.  All tapers see the same samples and
// the admission mask from the existing aided estimator.  No value here may be
// used to synchronize, tune, qualify or correct a receiver.
#include "helper_live_probe_fixture.hpp"
#include "datapump/helper_aided_phase.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
namespace p=helper_live_probe;
namespace h=datapump::helper;
namespace {
constexpr double pi=std::numbers::pi, tau=2*pi, fs=p::sample_rate;
constexpr std::size_t begin_sample=480, n=8640;
using C=std::complex<double>;
struct Taper {
    std::array<double,n> w{};
    Taper(double alpha) {
        for(std::size_t i=0;i<n;++i){const double x=static_cast<double>(i)/(n-1);
            if(x<alpha/2)w[i]=.5*(1+std::cos(pi*(2*x/alpha-1)));
            else if(x>1-alpha/2)w[i]=.5*(1+std::cos(pi*(2*x/alpha-2/alpha+1)));
            else w[i]=1;
        }
    }
};
struct Moment {C z{},m1{},m2{};};
Moment moment(std::span<const std::complex<float>> x,const Taper& w,double f){
    const double center=static_cast<double>(n-1)/(2*fs);
    C oscillator=std::polar(1.,tau*f*center),step=std::polar(1.,-tau*f/fs);Moment out;
    for(std::size_t i=0;i<n;++i){const double t=static_cast<double>(i)/fs-center;
        const C v=C(x[i])*w.w[i]*oscillator;out.z+=v;out.m1+=t*v;out.m2+=t*t*v;oscillator*=step;}
    return out;
}
struct Fit {double phase=0, phase_fixed=0, freq=0, freq_once=0;bool valid=false;};
Fit fit(std::span<const std::complex<float>> x,const Taper& w,double prediction){
    double f=prediction;bool converged=false; Moment z;
    const auto fixed=moment(x,w,prediction);
    const double fixed_phase=std::arg(fixed.z);
    const double first_derivative=2*tau*std::imag(std::conj(fixed.z)*fixed.m1);
    const double first_curvature=2*tau*tau*(std::norm(fixed.m1)-std::real(std::conj(fixed.z)*fixed.m2));
    const double freq_once=first_curvature<0?prediction-first_derivative/first_curvature:prediction;
    for(int k=0;k<6;++k){z=moment(x,w,f);const double d=2*tau*std::imag(std::conj(z.z)*z.m1);
        const double d2=2*tau*tau*(std::norm(z.m1)-std::real(std::conj(z.z)*z.m2));
        if(!(d2<0)||!std::isfinite(d)||!std::isfinite(d2))return {};
        const double update=-d/d2;if(std::abs(update)<1e-5){converged=true;break;}
        f+=std::clamp(update,-.25,.25);if(std::abs(f-prediction)>=.75)return {};
    }
    if(!converged)return {};
    return {std::arg(z.z),fixed_phase,f,freq_once,true};
}
struct Stats {std::size_t paired=0,miss=0;double phase2=0,frequency2=0,phase_max=0,frequency_max=0;};
void record(Stats& s,double phase,double frequency){++s.paired;s.phase2+=phase*phase;s.frequency2+=frequency*frequency;
    s.phase_max=std::max(s.phase_max,std::abs(phase));s.frequency_max=std::max(s.frequency_max,std::abs(frequency));}
double wrap(double x){return std::remainder(x,tau);}
}
int main(int argc,char** argv){
    const std::string mode=argc>1?argv[1]:"short";
    const unsigned seeds=argc>2?static_cast<unsigned>(std::stoul(argv[2])):4;
    const unsigned first_seed=argc>3?static_cast<unsigned>(std::stoul(argv[3])):901;
    if(argc>4||seeds==0||seeds>256||
       (mode!="short"&&mode!="probe"&&mode!="bright"&&mode!="near"&&mode!="near-strong"&&
        mode!="settle"&&mode!="settle-mid"&&mode!="prediction"))
        throw std::invalid_argument("mode [short|probe|bright|near|near-strong|settle|settle-mid|prediction], seeds 1..256, first_seed");
    const bool probe=mode=="probe";const bool bright=mode=="bright";
    const bool near=mode=="near"||mode=="near-strong";
    const bool settle=mode=="settle"||mode=="settle-mid";
    const double predict_offset=mode=="prediction"?.45:.15;
    const unsigned dwell_count=probe?1:4;
    const std::array<Taper,4> windows{Taper(1.),Taper(.75),Taper(.5),Taper(.25)};
    std::array<Stats,10> stats{};unsigned admitted=0,attempted=0,common=0;
    std::array<unsigned,3> phase_out{},frequency_out{},once_out{};
    for(unsigned seed=first_seed;seed<first_seed+seeds;++seed){p::Config config;config.seed=seed;config.probe=p::Probe::apsk64;
        p::Source src(config);const unsigned first=probe?96:2;
        for(unsigned d=0;d<first+dwell_count;++d){auto dwell=src.generate(d);if(d<first)continue;
            auto x=std::span<std::complex<float>>(dwell.samples).subspan(begin_sample,n);
            const double center_time=dwell.first_seconds+(begin_sample+(n-1)/2.)/fs;
            const double frequency=2400*src.audio_scale_at(center_time)+src.shift_error_hz(dwell.shift_hz,center_time);
            const double low_frequency=550*src.audio_scale_at(center_time)+src.shift_error_hz(dwell.shift_hz,center_time);
            const auto k=begin_sample+n/2;const C phasor=(C(dwell.common_phasor[k-1])+C(dwell.common_phasor[k]))*.5;
            const double phase=wrap(std::arg(phasor)+tau*std::remainder(2400*src.tx_time(center_time),1.));
            const double prediction=frequency+((seed+d)&1?predict_offset:-predict_offset);
            if(bright||near||settle){for(std::size_t i=0;i<n;++i){const double t=(static_cast<double>(i)-(n-1)/2.)/fs;
                if(bright)x[i]+=std::complex<float>(std::polar(100.,tau*(1600*src.audio_scale_at(center_time)+src.shift_error_hz(dwell.shift_hz,center_time))*t+1.1));
                if(near)x[i]+=std::complex<float>(std::polar(mode=="near-strong"?.8:.3,tau*(frequency+(mode=="near-strong"?16.3:30))*t+1.1));
                if(settle&&i<(mode=="settle-mid"?1152U:144U))x[i]+=std::complex<float>(std::polar(1.,tau*frequency*t+phase+pi/2));
            }}
            h::HelperAidedPhaseConfig c;c.sample_rate=fs;
            h::HelperAidedPhasePrediction prior{true,prediction,low_frequency+(prediction-frequency),.2,.2};
            const auto base=h::estimate_helper_aided_phase(x,c,prior,dwell.shift_hz,dwell.first_seconds+begin_sample/fs);
            ++attempted;if(!base.valid)continue;++admitted;
            std::array<Fit,4> fits{};bool all_valid=true;
            for(std::size_t k2=0;k2<windows.size();++k2){fits[k2]=fit(x,windows[k2],prediction);
                if(!fits[k2].valid){++stats[k2+1].miss;all_valid=false;}}
            if(!all_valid)continue;
            ++common;
            const double phase_difference=std::abs(wrap(fits[2].phase_fixed-base.high_center_phase_radians))*180/pi;
            const double frequency_difference=std::abs(fits[2].freq-base.measurement.high_frequency_hz);
            const double once_difference=std::abs(fits[2].freq_once-base.measurement.high_frequency_hz);
            for(std::size_t j=0;j<3;++j){if(phase_difference>static_cast<double>(j+1))++phase_out[j];
                if(frequency_difference>.10+.05*static_cast<double>(j))++frequency_out[j];
                if(once_difference>.10+.05*static_cast<double>(j))++once_out[j];}
            record(stats[0],wrap(base.high_center_phase_radians-phase),base.measurement.high_frequency_hz-frequency);
            for(std::size_t k2=0;k2<windows.size();++k2){const auto& z=fits[k2];
                record(stats[k2+1],wrap(z.phase_fixed-phase),z.freq-frequency);
                record(stats[k2+5],wrap(z.phase-phase),z.freq-frequency);}
            record(stats[9],wrap((once_difference>.15?base.high_center_phase_radians:fits[2].phase_fixed)-phase),
                base.measurement.high_frequency_hz-frequency);
        }
    }
    std::cout<<"taper_study mode="<<mode<<" seeds="<<seeds<<" seed_first="<<first_seed<<" waveform=actual_helper keyed_hf50="<<1
        <<" helper_w=40 probe_w="<<(probe?40:0)<<" loss_db=180 temperature_k=290 noise_figure_db=10 aperture_seconds=.36 guard_seconds=.02 prediction_offset_hz="<<predict_offset
        <<" attempted="<<attempted<<" baseline_accepted="<<admitted<<" all_methods_common="<<common
        <<" admission=baseline_high_and_low_gate truth_prediction=isolated_feasibility_only"<<'\n';
    std::cout<<"taper_witness mode="<<mode<<" phase_gt1deg="<<phase_out[0]<<" phase_gt2deg="<<phase_out[1]<<" phase_gt3deg="<<phase_out[2]
        <<" freq_gt0.10hz="<<frequency_out[0]<<" freq_gt0.15hz="<<frequency_out[1]<<" freq_gt0.20hz="<<frequency_out[2]<<" denominator="<<common<<'\n';
    std::cout<<"taper_witness_once mode="<<mode<<" freq_gt0.10hz="<<once_out[0]<<" freq_gt0.15hz="<<once_out[1]<<" freq_gt0.20hz="<<once_out[2]<<" denominator="<<common<<'\n';
    const std::array<const char*,10> names{"baseline_hann_fourwindow","hann_fixed_phase","tukey75_fixed_phase","tukey50_fixed_phase","tukey25_fixed_phase", "hann_refit_phase","tukey75_refit_phase","tukey50_refit_phase","tukey25_refit_phase","conditional_tukey50_hann_fallback"};
    for(std::size_t i=0;i<stats.size();++i){const auto&s=stats[i];
        std::cout<<"taper_result mode="<<mode<<" method="<<names[i]<<" paired="<<s.paired<<" failed="<<s.miss
            <<" phase_rms_deg="<<(s.paired?std::sqrt(s.phase2/static_cast<double>(s.paired))*180/pi:0)<<" phase_max_deg="<<s.phase_max*180/pi
            <<" freq_rms_hz="<<(s.paired?std::sqrt(s.frequency2/static_cast<double>(s.paired)):0)<<" freq_max_hz="<<s.frequency_max<<'\n';}
    return common?0:1;
}
