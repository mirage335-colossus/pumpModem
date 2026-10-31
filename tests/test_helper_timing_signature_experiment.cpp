// Default-off research waveform: 1350 Hz timing pilot at 10% Helper energy.
// Ideal frequency coordinates and exact old reference are diagnostic assumptions;
// the probe remains unmodeled interference and never selects a branch.
#define main helper_continuation_included_main
#include "test_helper_continuation_study.cpp"
#undef main
namespace {
struct Output {double score=0,relative_power_error=0,power_ratio=0,tone_share=0; unsigned pre=0,post=0;
 double baseline_phase=0,variant_phase=0,baseline_width=0,variant_width=0;unsigned baseline_valid=0,variant_valid=0;
 double baseline_evm=0,variant_evm=0;unsigned baseline_support=0,variant_support=0,common_symbols=0,baseline_slips=0,variant_slips=0;bool common_reference=false;};
Output trial(unsigned seed,bool jump,hp::Probe probe,double extra_phase) {
 hp::Config cfg;cfg.seed=seed;cfg.path_loss_db=180;cfg.probe=probe;
 ContinuationSource src(cfg,{},false,false,36.8,0,jump?1./1850:0);
 h::Options o;o.power_w=cfg.helper_w;o.data_power_w=1;o.callsign=cfg.callsign;
 h::Generator gen(o,h::Plan{2400,.4,0,cfg.helper_w,950,550,true},cfg.epoch,true);
 const double width=3*(31.225e6+2400*100)*1e-6;
 h::HelperMeasurementConfig mc{fs,550,2400,2400-width,2400+width,300,
  cfg.phase_noise_degrees_per_sqrt_second*std::numbers::pi/180/std::sqrt(src.base().rx_scale())};
 using M=h::HelperAidedFrequencyMethod;
 Chain baseline("baseline",M::subaperture_slope,true,true,mc,30e6,true,false,h::HelperJointEstimator::FrequencyStepModel::common_hz,false,true);
 Chain variant("signature",M::subaperture_slope,true,true,mc,30e6,true,false,h::HelperJointEstimator::FrequencyStepModel::common_hz,false,true);
 constexpr double fraction=.1,f=1350,scale=.9486832980505138;
 C pre{},post{};unsigned np=0,nq=0;double power_error=0,energy0=0,energy1=0,tone_energy=0;
 // Fixed transmitter-side coefficient: baseline mean total power is 1/high_fraction
 // in these units, and the linear 4 ms dwell-edge gate has mean square 74/75.
 // No receiver sample time, path, noise, or probe selects this coefficient.
 const double amp=std::sqrt(fraction/(src.base().high_fraction()*(74./75.)));
 for(unsigned d=0;d<100;++d){
  auto dw=src.generate(d);auto modified=dw;
  std::vector<C> original(hp::dwell_samples),signature(hp::dwell_samples);
  double total=0,unit=0;
  for(unsigned i=0;i<hp::dwell_samples;++i){
   const double t=dw.first_seconds+double(i)/fs,tx=src.tx_time(t);
   original[i]=gen.mix_analytic({},tx,hp::sample_rate,40.L,0.L)/(.4*h::pilot_amplitude_fraction);
   const double edge=std::min(std::fmod(std::max(tx,0.),.4),.4-std::fmod(std::max(tx,0.),.4));
   const double gate=std::clamp(edge/.004,0.,1.);
   signature[i]=gate*std::polar(1.,tau*std::remainder(f*tx,1.));
   total+=std::norm(original[i]);unit+=std::norm(signature[i]);
  }
  double newpower=0;for(unsigned i=0;i<hp::dwell_samples;++i)newpower+=std::norm(scale*original[i]+amp*signature[i]);
  power_error=std::max(power_error,std::abs(newpower/total-1));
  energy0+=total;energy1+=newpower;tone_energy+=amp*amp*unit;
  const double center=dw.first_seconds+.2, clock=src.audio_scale_at(center),shift=src.shift_error_hz(dw.shift_hz,center);
  C high{},sig{};
  for(unsigned i=0;i<hp::dwell_samples;++i){
   const double t=dw.first_seconds+double(i)/fs;
   C s=dw.samples[i];const C common=dw.common_phasor[i];
   // Differential channel phase is an imposed propagation impairment, not an input to the receiver.
   const double p=d>=95?extra_phase:0;
   s+=((scale-1)*original[i]+amp*signature[i]*std::polar(1.,p))*common;
   modified.samples[i]=S(s);
   if(i>=480&&i<9120){const double w=.5-.5*std::cos(tau*double(i-480)/8639.);
    high+=w*s*std::polar(1.,-tau*(2400*clock+shift)*(t-center));
    sig+=w*s*std::polar(1.,-tau*(f*clock+shift)*(t-center));}
  }
  // The actual 32 s one-cycle jump lies inside this 10 s receiver dropout.
  const bool missing=d>=70&&d<95;
  baseline.observe(dw,missing,d);baseline.evaluate(src,dw,d);baseline.condition(dw,missing,d,src);
  variant.observe(modified,missing,d);variant.evaluate(src,modified,d);variant.condition(modified,missing,d,src);
  if((d>=60&&d<70)||(d>=95&&d<100)){
   const C relative=high*std::conj(sig)*std::polar(1.,-tau*std::remainder((2400-f)*clock*center,1.));
   if(d<70){pre+=relative/std::abs(relative);++np;}else{post+=relative/std::abs(relative);++nq;}
  }
 }
 const double angle=std::arg(post*std::conj(pre));
 const double expected=tau*(2400-f)/1850.;
 const double normal=std::abs(std::remainder(angle,tau));
 const double changed=std::abs(std::remainder(angle-expected,tau));
 const bool same=baseline.stream.initialized&&variant.stream.initialized&&std::abs(baseline.stream.reference_input-variant.stream.reference_input)<1e-12&&std::abs(baseline.stream.gauge-variant.stream.gauge)<1e-12;
 const auto e0=probe==hp::Probe::apsk64&&same?evm_metrics(baseline.stream.trace,src.base(),baseline.stream.gauge,&variant.stream.trace):Evm{};
 const auto e1=probe==hp::Probe::apsk64&&same?evm_metrics(variant.stream.trace,src.base(),variant.stream.gauge,&baseline.stream.trace):Evm{};
 return {normal-changed,power_error,energy1/energy0,tone_energy/energy0,np,nq,baseline.current_phase.rms(),variant.current_phase.rms(),
  baseline.frozen.valid?baseline.frozen.half_width:0,variant.frozen.valid?variant.frozen.half_width:0,baseline.receiver.valid,variant.receiver.valid,
  e0.clean,e1.clean,baseline.stream.anchors,variant.stream.anchors,e0.symbols,baseline.stream.cycle_slips,variant.stream.cycle_slips,same};
}
}
int main(int argc,char**argv)try{
 if(argc!=5)throw std::invalid_argument("SEED COUNT tone|apsk DIFFERENTIAL_RAD");
 const unsigned first=std::stoul(argv[1]),count=std::stoul(argv[2]);const std::string kind=argv[3];
 if(!count||count>32||(kind!="tone"&&kind!="apsk"))throw std::invalid_argument("bad arguments");
 const auto probe=kind=="tone"?hp::Probe::tone:hp::Probe::apsk64;const double phase=std::stod(argv[4]);
 unsigned fp=0,fn=0;double maxpower=0;
 for(unsigned i=0;i<count;++i){auto a=trial(first+i,false,probe,phase),b=trial(first+i,true,probe,phase);
  fp+=a.score>0;fn+=b.score<=0;maxpower=std::max({maxpower,a.relative_power_error,b.relative_power_error});
  std::cout<<"signature_trial seed="<<first+i<<" nojump_score="<<a.score<<" jump_score="<<b.score<<" before="<<a.pre<<" after="<<a.post
   <<" baseline_phase_rms_deg="<<a.baseline_phase*180/std::numbers::pi<<" signature_phase_rms_deg="<<a.variant_phase*180/std::numbers::pi
   <<" baseline_frequency36_halfwidth_hz="<<a.baseline_width<<" signature_frequency36_halfwidth_hz="<<a.variant_width
   <<" baseline_frequency36_available="<<(a.baseline_width>0)<<" signature_frequency36_available="<<(a.variant_width>0)
   <<" baseline_valid="<<a.baseline_valid<<" signature_valid="<<a.variant_valid
   <<" baseline_clean_apsk_evm="<<a.baseline_evm<<" signature_clean_apsk_evm="<<a.variant_evm
   <<" baseline_anchors="<<a.baseline_support<<" signature_anchors="<<a.variant_support<<" common_symbols="<<a.common_symbols<<" common_reference="<<a.common_reference
   <<" nojump_baseline_slips="<<a.baseline_slips<<" nojump_signature_slips="<<a.variant_slips
   <<" jump_baseline_slips="<<b.baseline_slips<<" jump_signature_slips="<<b.variant_slips
   <<" mean_power_ratio="<<a.power_ratio<<" timing_tone_energy_fraction="<<a.tone_share<<'\n';}
 std::cout<<"signature_summary n="<<count<<" false_jump="<<fp<<" missed_jump="<<fn<<" extra_phase_rad="<<phase<<" max_dwell_power_error="<<maxpower<<" probe="<<kind<<" prior=oracle_frequency_clock_reference candidate_count=2 pre_dwells=10 post_dwells=5 missing_dwells=70_to_94 timing_tone_hz=1350 target_tone_share=0.1 no_probe_model=1"<<'\n';
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
