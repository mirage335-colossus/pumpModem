// Offline falsification of a differential-pilot segment witness. It uses the
// real standalone chain and earlier noisy Helper history; neither simulator
// truth nor the absent probe selects the candidate. Candidate support is a
// provisional input span without conditioner halo or qualified selection
// covariance. This is not a receiver correction or a calibrated test.
#define main helper_study_original_main
#include "test_helper_continuation_study.cpp"
#undef main
#include <iostream>
int main(int argc,char**argv){
 const std::string mode=argc>1?argv[1]:"clean"; const double threshold=argc>2?std::stod(argv[2]):2.;
 if(argc>3||!std::isfinite(threshold)||threshold<=0)throw std::invalid_argument("mode [positive finite threshold]");
 unsigned n=0, flagged=0,bad_detect=0;double b2=0,c2=0,bmax=0,cmax=0;std::uint64_t selected=0,affected=0;
 for(unsigned seed=1680000001;seed<=1680000008;++seed){hp::Config cfg;cfg.seed=seed;cfg.expanded_schedule_key=true; ContinuationSource source(cfg,{});
  const double width=3*(31.225e6+2400*100)*1e-6;h::HelperMeasurementConfig mc{fs,550,2400,2400-width,2400+width,300,cfg.phase_noise_degrees_per_sqrt_second*std::numbers::pi/180/std::sqrt(source.base().rx_scale())};
  Chain chain("a",h::HelperAidedFrequencyMethod::subaperture_slope,true,true,mc,30e6,true);
  for(unsigned d=0;d<38;++d){auto data=source.generate(d);std::optional<helper_transient::Model> model;auto before=data.common_phasor;if((d==20||d==25||d==30||d==35)&&mode!="clean"){auto profile=helper_transient::named(mode=="center48"||mode=="center80"||mode=="reverse48"?"high-add48":mode,d);if(mode=="center48"||mode=="center80"||mode=="reverse48"){profile.onset_seconds=.16;profile.duration_seconds=mode=="center80"?.08:.048;if(mode=="reverse48")profile.phase_radians=-std::numbers::pi/2;}model.emplace(cfg,profile);model->apply(data,source.base(),d);}
   chain.observe(data,false,d); auto &a=chain.receiver.last_aided;if(!(d==20||d==25||d==30||d==35)||!a.valid||!a.thermal_covariance_valid||!chain.receiver.phase.valid||!chain.joint_state.valid)continue;
   constexpr std::size_t nseg=8,sz=aperture/nseg;std::array<C,nseg> hi{},lo{};const double fhi=a.measurement.high_frequency_hz,flo=a.measurement.low_frequency_hz;
   for(std::size_t j=0;j<nseg;++j){C oh=std::polar(1.,-tau*fhi*((j*sz-4319.5)/fs)),ol=std::polar(1.,-tau*flo*((j*sz-4319.5)/fs));const C sh=std::polar(1.,-tau*fhi/fs),sl=std::polar(1.,-tau*flo/fs);double wsum=0;for(std::size_t k=0;k<sz;++k){const double w=.5-.5*std::cos(tau*k/(sz-1));C x(data.samples[settle+j*sz+k]);hi[j]+=w*x*oh;lo[j]+=w*x*ol;oh*=sh;ol*=sl;wsum+=w;}hi[j]/=wsum;lo[j]/=wsum;}
   C mean{};for(std::size_t j=0;j<nseg;++j)mean+=std::polar(1.,std::arg(hi[j]*std::conj(lo[j])));const double reference=std::arg(mean);
   const double sigma=std::sqrt(8.)*std::hypot(a.high_center_phase_sigma_radians,a.low_center_phase_sigma_radians);
   std::array<bool,nseg> reject{};for(std::size_t j=0;j<nseg;++j)reject[j]=std::abs(wrap(std::arg(hi[j]*std::conj(lo[j]))-reference))>threshold*sigma+.02;
   bool any=std::any_of(reject.begin(),reject.end(),[](bool v){return v;});if(any)++flagged;
   std::size_t first=0,end=0,start=0;for(std::size_t j=0;j<=nseg;++j){if(j<nseg&&!reject[j])continue;if(j-start>end-first){first=start;end=j;}start=j+1;}
   double candidate=chain.receiver.phase.high, date=chain.receiver.phase.date;
   if(any&&end>first){C z{};for(std::size_t j=first;j<end;++j)z+=hi[j];date=data.first_seconds+(settle+(first+end)*sz/2.-.5)/fs;candidate=wrap(std::arg(z)+tau*fhi*(date-(data.first_seconds+(settle+4319.5)/fs)));}
   const double time=(date-data.first_seconds)*fs;std::size_t i=static_cast<std::size_t>(std::floor(time));double frac=time-i;double cp=std::arg(C(before[i]))+frac*wrap(std::arg(C(before[i+1]))-std::arg(C(before[i])));const double truephase=wrap(cp+tau*std::remainder(2400*source.tx_time(date),1.));
   const double bdate=chain.receiver.phase.date;const double bi=(bdate-data.first_seconds)*fs;const std::size_t ib=static_cast<std::size_t>(std::floor(bi));double cpb=std::arg(C(before[ib]))+(bi-ib)*wrap(std::arg(C(before[ib+1]))-std::arg(C(before[ib])));const double bt=wrap(cpb+tau*std::remainder(2400*source.tx_time(bdate),1.));const double be=wrap(chain.receiver.phase.high-bt),ce=wrap(candidate-truephase);b2+=be*be;c2+=ce*ce;bmax=std::max(bmax,std::abs(be));cmax=std::max(cmax,std::abs(ce));++n;
   if(any&&end>first){selected+=(end-first)*sz;if(model)for(std::size_t i=first*sz;i<end*sz;++i)if(model->disturbed(d,(settle+double(i))/fs))++affected;if(std::abs(ce)>.2)++bad_detect;}
  }
 }
 std::cout<<mode<<" threshold="<<threshold<<" common="<<n<<" flags="<<flagged<<" base_rms_deg="<<(n?sqrt(b2/n)*180/std::numbers::pi:0)<<" cand_rms_deg="<<(n?sqrt(c2/n)*180/std::numbers::pi:0)<<" base_max_deg="<<bmax*180/std::numbers::pi<<" cand_max_deg="<<cmax*180/std::numbers::pi<<" flagged_anchor_error_gt_11deg="<<bad_detect<<" selected_input_samples="<<selected<<" evaluator_affected_input_samples="<<affected<<" count_scope=flagged_dwells_only phase_reference=counterfactual_at_each_anchor input_span_uneroded=1 calibrated=0"<<'\n';
}
