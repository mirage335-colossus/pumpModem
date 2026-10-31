// Standalone waveform-level joint-frequency feasibility experiment.
// It shares the actual Helper chain and impaired capture, but NEVER feeds its
// predicted-prior-constrained fit back into the receiver's historical fit.
// Conditional phase uncertainty is provisionally borrowed from the baseline
// same-aperture Hann summary; this experiment is not a qualified receiver.
#define main helper_continuation_original_main
#include "test_helper_continuation_study.cpp"
#undef main

namespace {
struct WaveMoment { C z{},m1{},m2{}; };
struct WavePair { WaveMoment high,low; };
WavePair wave_moments(std::span<const S> samples,double hf,double lf){
    const double center=static_cast<double>(samples.size()-1)/2;
    C ho=std::polar(1.,tau*hf*center/fs),lo=std::polar(1.,tau*lf*center/fs);
    const C hs=std::polar(1.,-tau*hf/fs),ls=std::polar(1.,-tau*lf/fs);
    WavePair out;
    for(std::size_t i=0;i<samples.size();++i){
        const double t=(static_cast<double>(i)-center)/fs;
        const double w=.5-.5*std::cos(tau*static_cast<double>(i)/static_cast<double>(samples.size()-1));
        const C v=C(samples[i])*w,hv=v*ho,lv=v*lo;
        out.high.z+=hv;out.high.m1+=t*hv;out.high.m2+=t*t*hv;
        out.low.z+=lv;out.low.m1+=t*lv;out.low.m2+=t*t*lv;
        ho*=hs;lo*=ls;
    }
    return out;
}
struct WaveFit {bool valid=false;double frequency=0,high_phase=0,low_phase=0,residual=0;unsigned iterations=0;};
WaveFit fit_wave(std::span<const S> samples,double high,double low){
    // Profile independent complex H/L gains while searching a common residual.
    // Pilot separation and Hann taper make the two spectral columns nearly
    // orthogonal in this fixture. No phase is carried across the hop.
    double df=0;WavePair moments;unsigned iteration=0;
    for(;iteration<6;++iteration){
        moments=wave_moments(samples,high+df,low+df);
        const auto deriv=[](const WaveMoment& m){return 2*tau*std::imag(std::conj(m.z)*m.m1);};
        const auto curv=[](const WaveMoment& m){return 2*tau*tau*(std::norm(m.m1)-std::real(std::conj(m.z)*m.m2));};
        const double g=deriv(moments.high)+deriv(moments.low),h=curv(moments.high)+curv(moments.low);
        if(!std::isfinite(g)||!std::isfinite(h)||h>=0)return {};
        const double move=std::clamp(-g/h,-.25,.25);
        if(std::abs(move)<1e-5)return {true,high+df,std::arg(moments.high.z),std::arg(moments.low.z),df,iteration+1};
        df+=move;if(std::abs(df)>=.75)return {};
    }
    return {};
}
struct Acc {unsigned n=0;double square=0,max=0;void add(double x){++n;square+=x*x;max=std::max(max,std::abs(x));}double rms()const{return n?std::sqrt(square/n):0;}};
void experiment(double loss,unsigned seed,unsigned dwells,const std::string& mode,double relation_bias){
    hp::Config c;c.seed=seed;c.path_loss_db=loss;c.probe=hp::Probe::apsk64;
    const ht::Impairments problem=impairment_named(mode);
    ContinuationSource source(c,problem);
    const double width=3*(31.225e6+2400*100)*1e-6;
    h::HelperMeasurementConfig mc{fs,550,2400,2400-width,2400+width,300,
        c.phase_noise_degrees_per_sqrt_second*std::numbers::pi/180/std::sqrt(source.base().rx_scale())};
    Chain chain("baseline",h::HelperAidedFrequencyMethod::subaperture_slope,true,true,mc,30e6,true);
    h::HelperDualPilotPhase candidate_dual({});
    Acc base_freq,candidate_freq,base_phase,candidate_phase,high_phase;unsigned attempts=0,fit_fail=0,dual_fail=0,relation_declined=0;
    double max_relation_support=0;
    for(unsigned dwell=0;dwell<dwells;++dwell){
        auto d=source.generate(dwell);
        const double date=d.first_seconds+settle/fs+(aperture-1)/(2*fs);
        const auto previous=chain.joint.predict(1600,d.shift_hz,date);
        const auto high=chain.joint.predict(2400,d.shift_hz,date);
        const auto low=chain.joint.predict(550,d.shift_hz,date);
        const double latest=chain.latest_joint_input;
        const bool prior_suspended=chain.joint_suspended;
        chain.observe(d,source.missing(dwell),dwell);
        const auto& r=chain.receiver.last_aided;
        if(prior_suspended||!previous.valid||!high.valid||!low.valid||chain.joint_suspended||!chain.current_joint_accepted||!r.valid||!r.thermal_covariance_valid||!chain.receiver.phase.valid)continue;
        ++attempts;
        const double relation_support=1850e-6*(3*previous.audio_stochastic_sigma_ppm+previous.audio_model_half_width_ppm);
        max_relation_support=std::max(max_relation_support,relation_support);
        const auto variance_difference=[](const auto& matrix){return std::max(0.,matrix[0][0]+matrix[1][1]-2*matrix[0][1]);};
        const double measurement_sigma=std::sqrt(variance_difference(r.thermal_covariance));
        const double process_sigma=r.process_covariance_valid?std::sqrt(variance_difference(r.process_covariance)):2*r.measurement.common_phase_frequency_sigma_hz;
        const double model=r.high_frequency_model_half_width_hz+r.low_frequency_model_half_width_hz+
            1850e-6*previous.audio_model_half_width_ppm+
            2*chain.receiver.aided_config.slope_frequency_drift_bound_hz_per_second*(aperture/fs);
        const double relation_gate=5*(measurement_sigma+process_sigma+1850e-6*previous.audio_stochastic_sigma_ppm)+model;
        const double relation_error=(r.measurement.high_frequency_hz-r.measurement.low_frequency_hz)-
            (high.frequency_hz-low.frequency_hz-relation_bias);
        if(!std::isfinite(relation_gate)||std::abs(relation_error)>relation_gate){++relation_declined;continue;}
        // Deliberate relation_bias changes only the prediction in a falsifying
        // control, not the simulated waveform or baseline receiver.
        const auto fit=fit_wave(std::span<const S>(d.samples).subspan(settle,aperture),high.frequency_hz,low.frequency_hz+relation_bias);
        if(!fit.valid){++fit_fail;continue;}
        h::HelperDualPilotPhaseInput in;in.valid=true;in.time_seconds=r.center_time_seconds;
        in.high_phase_radians=fit.high_phase;in.low_phase_radians=fit.low_phase;
        in.thermal_covariance={{{r.pilot_thermal_covariance[2][2],r.pilot_thermal_covariance[2][3]},
                                {r.pilot_thermal_covariance[3][2],r.pilot_thermal_covariance[3][3]}}};
        in.common_phase_sigma_radians=r.common_center_phase_sigma_radians;in.low_model_half_width_radians=.02;
        h::HelperDualPilotPhasePrediction p;p.valid=previous.valid;p.previous_observations_only=true;p.reference_preserved=previous.reference_preserved;
        p.alternatives=static_cast<unsigned>(previous.alternatives);p.time_seconds=in.time_seconds;p.latest_observation_seconds=latest;
        p.reference_input_seconds=previous.reference_input_seconds;p.high_minus_low_radians=previous.phase_difference_radians;
        p.thermal_sigma_radians=previous.phase_difference_thermal_sigma_radians;p.process_sigma_radians=previous.phase_difference_process_sigma_radians;
        p.model_half_width_radians=previous.phase_difference_model_half_width_radians;
        const auto blended=candidate_dual.observe(in,p);
        if(!blended.valid){++dual_fail;continue;}
        // Evaluator-only truth; never read by candidate or baseline.
        const auto i=settle+(aperture-1)/2;const double common=std::arg(C(d.common_phasor[i]))+.5*wrap(std::arg(C(d.common_phasor[i+1]))-std::arg(C(d.common_phasor[i])));
        const double true_phase=wrap(tau*std::remainder(2400*source.tx_time(date),1.)+common);
        const double true_frequency=2400*source.audio_scale_at(date)+source.shift_error_hz(d.shift_hz,date);
        const double true_baseline_frequency=2400*source.audio_scale_at(r.high_frequency_time_seconds)+source.shift_error_hz(d.shift_hz,r.high_frequency_time_seconds);
        base_freq.add(r.measurement.high_frequency_hz-true_baseline_frequency);candidate_freq.add(fit.frequency-true_frequency);
        base_phase.add(wrap(chain.receiver.phase.high-true_phase));candidate_phase.add(wrap(blended.high_gauge_phase_radians-true_phase));high_phase.add(wrap(r.high_center_phase_radians-true_phase));
    }
    std::cout<<std::setprecision(10)<<"joint_wave_feasibility seed="<<seed<<" loss_db="<<loss<<" mode="<<mode<<" dwells="<<dwells
        <<" helper_w=40 probe_w=40 probe_active_from_seconds=38 keyed_hf50=1 temperature_K=290 nf_db=10"
        <<" aperture_seconds=.36 relation_bias_hz="<<relation_bias<<" relation_support_max_hz="<<max_relation_support
        <<" attempted="<<attempts<<" fit_fail="<<fit_fail<<" dual_rejected="<<dual_fail<<" relation_declined="<<relation_declined<<" common="<<base_freq.n
        <<" base_frequency_rms_hz="<<base_freq.rms()<<" candidate_frequency_rms_hz="<<candidate_freq.rms()
        <<" base_frequency_max_hz="<<base_freq.max<<" candidate_frequency_max_hz="<<candidate_freq.max
        <<" base_phase_rms_deg="<<base_phase.rms()*180/std::numbers::pi<<" candidate_phase_rms_deg="<<candidate_phase.rms()*180/std::numbers::pi
        <<" high_phase_rms_deg="<<high_phase.rms()*180/std::numbers::pi<<" base_phase_max_deg="<<base_phase.max*180/std::numbers::pi
        <<" candidate_phase_max_deg="<<candidate_phase.max*180/std::numbers::pi
        <<" prior=previous_only output=diagnostic_only covariance=baseline_proxy_not_validated history_feedback=none\n";
}
}
int main(int argc,char** argv) try {
    const double loss=argc>1?std::stod(argv[1]):180;
    const unsigned seed=argc>2?static_cast<unsigned>(std::stoul(argv[2])):901;
    const unsigned dwells=argc>3?static_cast<unsigned>(std::stoul(argv[3])):12;
    const std::string mode=argc>4?argv[4]:"clean";
    const double bias=argc>5?std::stod(argv[5]):0;
    if(argc>6||dwells<4||dwells>100||!std::isfinite(bias)||std::abs(bias)>1)throw std::invalid_argument("loss seed dwells mode relation_bias_hz");
    experiment(loss,seed,dwells,mode,bias);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
