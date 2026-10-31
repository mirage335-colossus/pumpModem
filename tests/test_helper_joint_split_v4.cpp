// Read-only feasibility for a disjoint first-half history / second-half phase
// design. The baseline history remains authoritative; neither split estimate
// is ingested or applied to a conditioner. This is not a receiver candidate,
// certified post-selection covariance, or sample-support result. Frequency
// errors use each method's own effective date; phase errors are compared at
// the second-half center using the baseline's noisy frequency for transport.
// Both half estimates retain the existing per-quarter detection gates.
#define main helper_continuation_original_main
#include "test_helper_continuation_study.cpp"
#undef main
namespace {
struct Stats { unsigned n=0; double sum=0, max=0; void add(double e){++n;sum+=e*e;max=std::max(max,std::abs(e));} double rms()const{return n?std::sqrt(sum/n):0;} };
void run(unsigned seed,unsigned dwells){
 hp::Config c;c.seed=seed;c.expanded_schedule_key=true;c.path_loss_db=180;c.probe=hp::Probe::tone;
 ContinuationSource source(c,{});const double width=3*(31.225e6+2400*100)*1e-6;
 h::HelperMeasurementConfig mc{fs,550,2400,2400-width,2400+width,300,c.phase_noise_degrees_per_sqrt_second*std::numbers::pi/180/std::sqrt(source.base().rx_scale())};
 Chain chain("baseline",h::HelperAidedFrequencyMethod::subaperture_slope,true,true,mc,30e6,true);h::HelperDualPilotPhase dual({});
 unsigned attempted=0,first_valid=0,second_valid=0,both=0,blends=0;Stats bf,ff,bp,sp;
 constexpr std::size_t half=aperture/2;
 for(unsigned dwell=0;dwell<dwells;++dwell){
  auto d=source.generate(dwell);const double t0=d.first_seconds+settle/fs;const double t1=t0+half/fs;
  const double ta=t0+(half-1)/(2*fs),tb=t1+(half-1)/(2*fs);
  const auto previous=chain.joint.predict(1600,d.shift_hz,tb),hi=chain.joint.predict(2400,d.shift_hz,ta),lo=chain.joint.predict(550,d.shift_hz,ta);
  const auto hi_second=chain.joint.predict(2400,d.shift_hz,tb),lo_second=chain.joint.predict(550,d.shift_hz,tb);
  const double previous_input=chain.latest_joint_input;const bool suspended=chain.joint_suspended;
  chain.observe(d,false,dwell);
  if(suspended||!previous.valid||!hi.valid||!lo.valid||!hi_second.valid||!lo_second.valid||chain.joint_suspended||!chain.current_joint_accepted||!chain.receiver.phase.valid||!chain.receiver.last_aided.valid)continue;
  ++attempted;h::HelperAidedPhasePrediction pr{true,hi.frequency_hz,lo.frequency_hz,hi.stochastic_sigma_hz+hi.model_half_width_hz,lo.stochastic_sigma_hz+lo.model_half_width_hz};
  auto ac=chain.receiver.aided_config;ac.minimum_coherent_energy=8;
  const auto first=h::estimate_helper_aided_phase(std::span<const S>(d.samples).subspan(settle,half),ac,pr,d.shift_hz,t0);
  const h::HelperAidedPhasePrediction second_prior{true,hi_second.frequency_hz,lo_second.frequency_hz,
      hi_second.stochastic_sigma_hz+hi_second.model_half_width_hz,lo_second.stochastic_sigma_hz+lo_second.model_half_width_hz};
  const auto second=h::estimate_helper_aided_phase(std::span<const S>(d.samples).subspan(settle+half,half),ac,second_prior,d.shift_hz,t1);
  first_valid+=first.valid;second_valid+=second.valid;if(!first.valid||!second.valid||!second.thermal_covariance_valid)continue;++both;
  h::HelperDualPilotPhaseInput in;in.valid=true;in.time_seconds=second.center_time_seconds;in.high_phase_radians=second.high_center_phase_radians;in.low_phase_radians=second.low_center_phase_radians;
  in.thermal_covariance={{{second.pilot_thermal_covariance[2][2],second.pilot_thermal_covariance[2][3]},{second.pilot_thermal_covariance[3][2],second.pilot_thermal_covariance[3][3]}}};
  in.common_phase_sigma_radians=second.common_center_phase_sigma_radians;in.low_model_half_width_radians=.02;
  h::HelperDualPilotPhasePrediction pred;pred.valid=previous.valid;pred.previous_observations_only=true;pred.reference_preserved=previous.reference_preserved;pred.alternatives=static_cast<unsigned>(previous.alternatives);pred.time_seconds=tb;pred.latest_observation_seconds=previous_input;pred.reference_input_seconds=previous.reference_input_seconds;pred.high_minus_low_radians=previous.phase_difference_radians;pred.thermal_sigma_radians=previous.phase_difference_thermal_sigma_radians;pred.process_sigma_radians=previous.phase_difference_process_sigma_radians;pred.model_half_width_radians=previous.phase_difference_model_half_width_radians;
  const auto blended=dual.observe(in,pred);const double candidate=blended.valid&&blended.combined?blended.high_gauge_phase_radians:second.high_center_phase_radians;blends+=blended.valid&&blended.combined;
  const auto i=static_cast<std::size_t>(std::floor((tb-d.first_seconds)*fs));const double frac=(tb-d.first_seconds)*fs-i;
  const double common=std::arg(C(d.common_phasor[i]))+frac*wrap(std::arg(C(d.common_phasor[i+1]))-std::arg(C(d.common_phasor[i])));
  const double truth=wrap(tau*std::remainder(2400*source.tx_time(tb),1.)+common);
  const auto& baseline=chain.receiver.last_aided;const double baseline_phase=wrap(chain.receiver.phase.high+tau*baseline.measurement.high_frequency_hz*(tb-baseline.center_time_seconds));
  bp.add(wrap(baseline_phase-truth));sp.add(wrap(candidate-truth));
  const double truth_first=2400*source.audio_scale_at(first.high_frequency_time_seconds)+source.shift_error_hz(d.shift_hz,first.high_frequency_time_seconds);
  const double truth_full=2400*source.audio_scale_at(baseline.high_frequency_time_seconds)+source.shift_error_hz(d.shift_hz,baseline.high_frequency_time_seconds);
  ff.add(first.measurement.high_frequency_hz-truth_first);bf.add(baseline.measurement.high_frequency_hz-truth_full);
 }
 std::cout<<"joint_disjoint_split seed="<<seed<<" attempted="<<attempted<<" first_valid="<<first_valid<<" second_valid="<<second_valid<<" both="<<both<<" blended="<<blends<<" base_phase_rms_deg="<<bp.rms()*180/std::numbers::pi<<" split_phase_rms_deg="<<sp.rms()*180/std::numbers::pi<<" base_phase_max_deg="<<bp.max*180/std::numbers::pi<<" split_phase_max_deg="<<sp.max*180/std::numbers::pi<<" base_freq_rms_hz="<<bf.rms()<<" split_freq_rms_hz="<<ff.rms()<<" short_coherent_min=8 baseline_coherent_min=16 history=baseline_only probe_feedback=none\n";
}
}
int main(int argc,char**argv){try{const unsigned seed=argc>1?static_cast<unsigned>(std::stoul(argv[1])):1680000001U;const unsigned dwells=argc>2?static_cast<unsigned>(std::stoul(argv[2])):32;if(argc>3||dwells>100||dwells<16)throw std::invalid_argument("usage: [seed] [16..100 dwells]");run(seed,dwells);return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
