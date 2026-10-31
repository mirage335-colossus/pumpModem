// Offline falsification of a pooled two-pilot change scan. Frequency
// coordinates are predicted from previous Helper observations; simulator
// truth is used only to generate/evaluate signals. The fitted nuisance and
// overlapping tests share samples, the threshold is uncalibrated, and the
// longest input span has neither a qualified model radius nor the conditioner
// halo. It must not be used as receiver support or correction.
#include "helper_live_probe_fixture.hpp"
#include "helper_transient_fixture.hpp"
#include "datapump/helper_aided_phase.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <complex>
#include <iostream>
#include <numbers>
#include <span>
#include <string>
#include <stdexcept>
namespace p=helper_live_probe;namespace h=datapump::helper;using C=std::complex<double>;using S=std::complex<float>;
#ifndef HELPER_GLRT_SEGMENT_SAMPLES
#define HELPER_GLRT_SEGMENT_SAMPLES 240
#endif
constexpr double fs=p::sample_rate,tau=2*std::numbers::pi;constexpr unsigned guard=480,N=8640,Q=HELPER_GLRT_SEGMENT_SAMPLES,J=N/Q;
static_assert(Q==120||Q==240,"the falsification supports 5 ms or 10 ms segments");
struct Counts{int attempts=0,base=0,flags=0,spans=0;unsigned long long kept=0,bad=0,err=0;double worst=0;};
void run(const std::string& mode,double gate){
if(!std::isfinite(gate)||gate<=0)throw std::invalid_argument("gate must be finite and positive");
Counts c;
for(unsigned seed=1680000001;seed<=1680000008;seed++)for(unsigned target=5;target<9;target++){
 p::Config cfg;cfg.seed=seed;cfg.expanded_schedule_key=true;p::Source src(cfg);
 h::HelperMeasurementConfig mc{fs,550,2400,2300,2500,300,.05*std::numbers::pi/180};h::HelperMeasurement fit(mc);
 for(unsigned d=0;d<=target;d++){
  auto dw=src.generate(d);double start=dw.first_seconds+guard/fs,center=start+(N-1)/(2*fs);auto input=std::span<const S>(dw.samples.data()+guard,N);
  if(d<target){fit.observe(input,dw.shift_hz,start);continue;}
  auto ph=fit.predict(2400,dw.shift_hz,center,N/fs),pl=fit.predict(550,dw.shift_hz,center,N/fs);
  if(!ph.valid||!pl.valid)continue;
  std::unique_ptr<helper_transient::Model> event;
  if(mode!="clean"){event=std::make_unique<helper_transient::Model>(cfg,helper_transient::named(mode,target));event->apply(dw,src,target);}
  h::HelperAidedPhaseConfig ac;ac.sample_rate=fs;h::HelperAidedPhasePrediction pred{true,ph.frequency_hz,pl.frequency_hz,ph.mean_sigma_hz+ph.aperture_process_sigma_hz,pl.mean_sigma_hz+pl.aperture_process_sigma_hz};
  c.attempts++;auto b=h::estimate_helper_aided_phase(input,ac,pred,dw.shift_hz,start);if(!b.valid)continue;c.base++;
  std::array<C,J> hi{},lo{};double sumw=0,sumsq=0;
  for(unsigned j=0;j<J;j++){
    C oh=std::polar(1.,-tau*b.measurement.high_frequency_hz*(double(j*Q)-4319.5)/fs),ol=std::polar(1.,-tau*b.measurement.low_frequency_hz*(double(j*Q)-4319.5)/fs);
    C sh=std::polar(1.,-tau*b.measurement.high_frequency_hz/fs),sl=std::polar(1.,-tau*b.measurement.low_frequency_hz/fs);double sw=0;
    for(unsigned k=0;k<Q;k++){double w=.5-.5*std::cos(tau*double(k)/double(Q-1));hi[j]+=C(input[j*Q+k])*oh*w;lo[j]+=C(input[j*Q+k])*ol*w;oh*=sh;ol*=sl;sw+=w;if(j==0)sumsq+=w*w;}
    if(j==0)sumw=sw;
    hi[j]/=sw;lo[j]/=sw;
  }
  const double effective=sumw*sumw/(fs*sumsq), vh=b.high_raw_noise_density/effective,vl=b.low_raw_noise_density/effective;
  std::array<bool,J> suspect{};
  const auto lengths=Q==120?std::array<unsigned,5>{3,4,6,12,16}:std::array<unsigned,5>{1,2,3,6,8};
  for(unsigned len: lengths)for(unsigned pos=0;pos+len<=J;pos++){
    C eh{},el{},oh{},ol{};for(unsigned j=0;j<J;j++){if(j>=pos&&j<pos+len){eh+=hi[j];el+=lo[j];}else{oh+=hi[j];ol+=lo[j];}}
    eh/=double(len);el/=double(len);oh/=double(J-len);ol/=double(J-len);
    const double n=1./len+1./(J-len);const C dh=eh-oh,dl=el-ol;
    const double wh=std::norm(oh)/(vh*n),wl=std::norm(ol)/(vl*n);if(wh<=0||wl<=0)continue;
    const C rel=(wh*dh/oh+wl*dl/ol)/(wh+wl);
    const double score=std::max({std::abs(dh)/std::sqrt(vh*n),std::abs(dl)/std::sqrt(vl*n),std::abs(rel)*std::sqrt(wh+wl)});
    if(score>gate)for(unsigned j=pos;j<pos+len;j++)suspect[j]=true;
  }
  if(std::any_of(suspect.begin(),suspect.end(),[](bool x){return x;}))c.flags++;
  unsigned first=0,last=0,begin=0;for(unsigned j=0;j<=J;j++){if(j<J&&!suspect[j])continue;if(j-begin>last-first){first=begin;last=j;}begin=j+1;}
  if(last<=first)continue;
  c.spans++;
  for(unsigned i=first*Q;i<last*Q;i++){
    const double local=(guard+double(i))/fs,date=dw.first_seconds+local;
    const double truephase=std::remainder(std::arg(C(dw.common_phasor[guard+i]))+tau*std::remainder(2400*src.tx_time(date),1.),tau);
    const double guess=std::remainder(b.high_center_phase_radians+tau*b.measurement.high_frequency_hz*(double(i)-4319.5)/fs,tau);
    const double error=std::abs(std::remainder(guess-truephase,tau));c.kept++;if(error>.2)c.err++;c.worst=std::max(c.worst,error);
    if(event&&event->disturbed(target,local))c.bad++;
  }
 }
}
std::cout<<"mode="<<mode<<" segment_samples="<<Q<<" gate="<<gate<<" attempts="<<c.attempts<<" base="<<c.base<<" flagged="<<c.flags<<" spans="<<c.spans<<" kept="<<c.kept<<" disturbed="<<c.bad<<" err_gt_11deg="<<c.err<<" worst_deg="<<c.worst*180/std::numbers::pi<<" acquisition_prediction=previous segment_frequency=current_aided no_probe=1 counts=provisional_span_only input_span_uneroded=1 calibrated=0\n";
}
int main(int argc,char**argv){try{if(argc!=3)throw std::runtime_error("mode gate");run(argv[1],std::stod(argv[2]));}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
