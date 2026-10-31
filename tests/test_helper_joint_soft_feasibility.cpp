// Exploratory waveform-level soft joint fit. No output is fed into the Helper
// receiver or historical fit. Four disjoint 90 ms complex correlations per
// pilot jointly fit the fresh common phase/frequency and the differential
// phase/frequency, with a previous-only Gaussian prior. This first-order phase
// model is a feasibility experiment, not a calibrated posterior or receiver.
#include "helper_joint_soft_candidate.hpp"
#include <cstdlib>
#define main helper_continuation_original_main
#include "test_helper_continuation_study.cpp"
#undef main

namespace {
struct Acc {unsigned n=0;double square=0,max=0;void add(double x){++n;square+=x*x;max=std::max(max,std::abs(x));}double rms()const{return n?std::sqrt(square/n):0;}};
void experiment(double loss,unsigned seed,unsigned dwells,const std::string& mode,double phase_bias,double separation_bias){
    hp::Config c;c.seed=seed;c.expanded_schedule_key=true;c.path_loss_db=loss;c.probe=hp::Probe::apsk64;
    const auto problem=impairment_named(mode);ContinuationSource source(c,problem);
    const double width=3*(31.225e6+2400*100)*1e-6;
    h::HelperMeasurementConfig mc{fs,550,2400,2400-width,2400+width,300,
        c.phase_noise_degrees_per_sqrt_second*std::numbers::pi/180/std::sqrt(source.base().rx_scale())};
    Chain chain("baseline",h::HelperAidedFrequencyMethod::subaperture_slope,true,true,mc,30e6,true);
    Acc base_freq,candidate_freq,base_phase,candidate_phase;unsigned attempts=0,declined=0,without_clock_valid=0;
    unsigned frequency_witness3=0,frequency_witness5=0,phase_witness3=0,phase_witness5=0;
    double max_box=0,max_phase_innovation=0,max_freq_innovation=0,max_radius=0;unsigned phase_outside=0;
    for(unsigned dwell=0;dwell<dwells;++dwell){
        auto d=source.generate(dwell);const double date=d.first_seconds+settle/fs+(aperture-1)/(2*fs);
        const auto previous=chain.joint.predict(1600,d.shift_hz,date),high=chain.joint.predict(2400,d.shift_hz,date),low=chain.joint.predict(550,d.shift_hz,date);
        const auto previous_latest=chain.latest_joint_input;
        const bool prior_suspended=chain.joint_suspended;
        chain.observe(d,source.missing(dwell),dwell);const auto&r=chain.receiver.last_aided;
        if(prior_suspended||!previous.valid||!high.valid||!low.valid||chain.joint_suspended||!chain.current_joint_accepted||!r.valid||!r.thermal_covariance_valid||!chain.receiver.phase.valid)continue;
        ++attempts;
        // Separate pre-fit innovation, not the posterior residual. The prior
        // is from previous dwells; sum stochastic sigmas to avoid assuming
        // independence between the predictor and current process errors.
        const double fdiff=(r.measurement.high_frequency_hz-r.measurement.low_frequency_hz)-(high.frequency_hz-low.frequency_hz-separation_bias);
        const auto difference=[](const auto& c){return std::max(0.,c[0][0]+c[1][1]-2*c[0][1]);};
        const double ft=std::sqrt(difference(r.thermal_covariance));
        const double fp=r.process_covariance_valid?std::sqrt(difference(r.process_covariance)):2*r.measurement.common_phase_frequency_sigma_hz;
        const double fprior=1850e-6*previous.audio_stochastic_sigma_ppm;
        const double fb=r.high_frequency_model_half_width_hz+r.low_frequency_model_half_width_hz+1850e-6*previous.audio_model_half_width_ppm+
            2*chain.receiver.aided_config.slope_frequency_drift_bound_hz_per_second*aperture/fs;
        const double fsig=std::max(1e-12,ft+fp+fprior);
        if(std::abs(fdiff)>fb+3*fsig)++frequency_witness3;
        if(std::abs(fdiff)>fb+5*fsig)++frequency_witness5;
        const double pdiff=wrap(r.high_center_phase_radians-r.low_center_phase_radians-previous.phase_difference_radians-phase_bias);
        const double psig=std::sqrt(std::max(0.,r.thermal_covariance[2][2]))+previous.phase_difference_stochastic_sigma_radians;
        const double pbound=previous.phase_difference_model_half_width_radians+.02;
        if(std::abs(pdiff)>pbound+3*psig)++phase_witness3;
        if(std::abs(pdiff)>pbound+5*psig)++phase_witness5;
        // Preserve the baseline on an independently witnessed contradiction.
        if(std::abs(fdiff)>fb+5*fsig||std::abs(pdiff)>pbound+5*psig){++declined;continue;}
        const auto coordinate=chain.clock_at(r.center_time_seconds,d.shift_hz);
        if(!coordinate.valid||!coordinate.reference_preserved){++declined;continue;}
        if(std::getenv("HELPER_JOINT_TRACE")&&dwell==dwells-1)
            std::cerr<<"prior_date="<<previous.time_seconds<<" center="<<r.center_time_seconds<<" latest="<<previous_latest
                <<" baseline_sigma="<<chain.receiver.phase.high_sigma<<" time_radius="<<coordinate.source_half_width_seconds
                <<" rvalid="<<r.valid<<" cov="<<r.thermal_covariance_valid<<" dates="<<r.frequency_dates_valid
                <<" prior_valid="<<previous.valid<<" alternatives="<<previous.alternatives<<'\n';
        auto biased_previous=previous;biased_previous.phase_difference_radians+=phase_bias;
        const auto candidate_samples=std::span<const S>(d.samples).subspan(settle,aperture);
        const double phase_q=c.phase_noise_degrees_per_sqrt_second*std::numbers::pi/180/std::sqrt(source.base().rx_scale());
        // Diagnostic only: test the candidate's phase gate in isolation. Zero
        // time radius is not a valid combined correction uncertainty and its
        // result never enters the receiver or reported supported observations.
        if(helper_joint_soft_candidate::estimate(candidate_samples,high.frequency_hz,low.frequency_hz+separation_bias,
            biased_previous,r,previous_latest,chain.receiver.phase.high_sigma,0,
            chain.receiver.aided_config.slope_frequency_drift_bound_hz_per_second,phase_q).valid)++without_clock_valid;
        const auto fit=helper_joint_soft_candidate::estimate(candidate_samples,high.frequency_hz,low.frequency_hz+separation_bias,
            biased_previous,r,previous_latest,chain.receiver.phase.high_sigma,coordinate.source_half_width_seconds,
            chain.receiver.aided_config.slope_frequency_drift_bound_hz_per_second,phase_q);
        if(!fit.valid){++declined;continue;}
        const auto i=settle+(aperture-1)/2;const double common=std::arg(C(d.common_phasor[i]))+.5*wrap(std::arg(C(d.common_phasor[i+1]))-std::arg(C(d.common_phasor[i])));
        const double phase=wrap(tau*std::remainder(2400*source.tx_time(date),1.)+common),frequency=2400*source.audio_scale_at(date)+source.shift_error_hz(d.shift_hz,date);
        const double baseline_frequency=2400*source.audio_scale_at(r.high_frequency_time_seconds)+source.shift_error_hz(d.shift_hz,r.high_frequency_time_seconds);
        base_freq.add(r.measurement.high_frequency_hz-baseline_frequency);candidate_freq.add(fit.frequency-frequency);
        base_phase.add(wrap(chain.receiver.phase.high-phase));candidate_phase.add(wrap(fit.phase-phase));
        if(std::abs(wrap(fit.phase-phase))>fit.conditional_phase_radius)++phase_outside;
        max_radius=std::max(max_radius,fit.conditional_phase_radius);
        max_box=std::max(max_box,fit.phase_box_radius);max_phase_innovation=std::max(max_phase_innovation,std::abs(fit.phase_innovation));max_freq_innovation=std::max(max_freq_innovation,std::abs(fit.frequency_innovation));
    }
    std::cout<<std::setprecision(10)<<"joint_soft_feasibility seed="<<seed<<" mode="<<mode<<" loss_db="<<loss<<" dwells="<<dwells
        <<" helper_w=40 probe_w=40 schedule=expanded_keyed_hf50 aperture_seconds=.36 segments=4 segment_seconds=.09 full_hann=1 shared_covariance=1 prior=previous_only"
        <<" phase_bias_radians="<<phase_bias<<" separation_bias_hz="<<separation_bias<<" attempted="<<attempts<<" declined="<<declined<<" common="<<base_freq.n
        <<" diagnostic_without_clock_valid="<<without_clock_valid
        <<" frequency_witness3="<<frequency_witness3<<" frequency_witness5="<<frequency_witness5<<" phase_witness3="<<phase_witness3<<" phase_witness5="<<phase_witness5
        <<" baseline_frequency_rms_hz="<<base_freq.rms()<<" candidate_frequency_rms_hz="<<candidate_freq.rms()<<" baseline_frequency_max_hz="<<base_freq.max<<" candidate_frequency_max_hz="<<candidate_freq.max
        <<" baseline_phase_rms_deg="<<base_phase.rms()*180/std::numbers::pi<<" candidate_phase_rms_deg="<<candidate_phase.rms()*180/std::numbers::pi
        <<" baseline_phase_max_deg="<<base_phase.max*180/std::numbers::pi<<" candidate_phase_max_deg="<<candidate_phase.max*180/std::numbers::pi
        <<" model_box_phase_max_deg="<<max_box*180/std::numbers::pi<<" phase_innovation_max="<<max_phase_innovation<<" frequency_innovation_max="<<max_freq_innovation
        <<" candidate_phase_radius_max_deg="<<max_radius*180/std::numbers::pi<<" candidate_phase_outside_radius="<<phase_outside
        <<" output=diagnostic_only candidate_covariance=unvalidated history_feedback=none\n";
}
}
int main(int argc,char**argv) try{
    const double loss=argc>1?std::stod(argv[1]):180;const unsigned seed=argc>2?static_cast<unsigned>(std::stoul(argv[2])):901;
    const unsigned dwells=argc>3?static_cast<unsigned>(std::stoul(argv[3])):12;const std::string mode=argc>4?argv[4]:"clean";
    const double pb=argc>5?std::stod(argv[5]):0,sb=argc>6?std::stod(argv[6]):0;
    if(argc>7||dwells<4||dwells>100||!std::isfinite(pb)||!std::isfinite(sb)||std::abs(pb)>2||std::abs(sb)>1)throw std::invalid_argument("loss seed dwells mode phase_bias separation_bias");
    experiment(loss,seed,dwells,mode,pb,sb);return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
