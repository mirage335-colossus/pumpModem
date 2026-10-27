#include "datapump/helper_tone_tracker.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <random>
#include <string>
#include <vector>
using namespace datapump::helper;
constexpr unsigned rate=48000;
constexpr double tau=2*std::numbers::pi,expected=2450,offset=3000.37;
struct Case {double cn0=30,drift=0,drift_start=0,sigma=0,duration=64,loss=60;unsigned seed=1;std::string name;bool ramps=true,noiseless=false;};
struct Metrics {std::size_t polls=0,qualified=0,coherent=0,estimates=0,escapes=0,wrong=0,imprecise2=0,dated=0,dated_escapes=0;std::uint64_t date=UINT64_MAX,preloss_resets=0,model_resets=0;double first=0,error=0,u=0,max_error=0,max_ratio=0,max_aperture=0,cpu=0,withdraw=0,age=0,support=0;};
double truth(const Case& c,double t){return expected+offset+c.drift*std::max(0.,t-c.drift_start);}
void observe(ToneTracker& tracker,Metrics& m,const Case& c,std::uint64_t end) {
 const auto s=tracker.snapshot();++m.polls;m.coherent+=s.qualified && s.frequency_continuous_phase;
 if(s.continuous_model_resets!=m.model_resets){m.model_resets=s.continuous_model_resets;std::cout<<"CONTINUOUS_MODEL_RESET name="<<c.name<<" seed="<<c.seed<<" CN0="<<c.cn0<<" date="<<s.continuous_last_model_reset_sample/static_cast<double>(rate)<<" count="<<m.model_resets<<'\n';}
 m.max_aperture=std::max(m.max_aperture,s.coherent_aperture_seconds);
 if(end/static_cast<double>(rate)>c.loss+.1 && !s.qualified && !m.withdraw)m.withdraw=end/static_cast<double>(rate);
 if(!s.qualified || end/static_cast<double>(rate)>=c.loss)return;
 ++m.qualified;if(!m.first)m.first=end/static_cast<double>(rate);
 if(s.sample==m.date)return;
 m.date=s.sample;++m.estimates;const auto t=s.sample/static_cast<double>(rate);
 const auto error=s.frequency_hz-truth(c,t);const auto escaped=std::abs(error)>s.uncertainty_hz;
 m.escapes+=escaped;m.wrong+=std::abs(error)>100.;m.imprecise2+=std::abs(error)>2.;m.error=error;m.u=s.uncertainty_hz;
 m.preloss_resets=s.continuous_phase_resets;m.age=s.observation_age_seconds;m.support=s.frequency_support_seconds;
 m.max_error=std::max(m.max_error,std::abs(error));m.max_ratio=std::max(m.max_ratio,std::abs(error)/std::max(s.uncertainty_hz,1e-30));
 if(escaped)std::cout<<std::setprecision(12)<<"CONTINUOUS_ESCAPE name="<<c.name<<" seed="<<c.seed<<" CN0="<<c.cn0<<" coherent="<<s.frequency_continuous_phase<<" date="<<t<<" error="<<error<<" U="<<s.uncertainty_hz<<" first="<<s.first_sample<<" resets="<<s.continuous_phase_resets<<'\n';
 if(s.sample>=rate*2/5) {
  const auto q=s.sample-rate*2/5;const auto old=s.frequency_at(q);
  if(old.valid){++m.dated;const auto e=old.frequency_hz-truth(c,q/static_cast<double>(rate));m.dated_escapes+=std::abs(e)>old.uncertainty_hz;}
 }
}
int run(const Case& c) {
 ToneTrackerConfig base{rate,expected,9000,c.sigma,.5,true,false};auto assisted=base;assisted.continuous_pilot=true;
 ToneTracker a(base,ToneTracker::workspace_required(base)),b(assisted,ToneTracker::workspace_required(assisted));
 Metrics ma,mb;std::mt19937 rng(c.seed);std::normal_distribution<double> normal;
 const auto noise=.6*.5*std::sqrt(rate/std::pow(10.,c.cn0/10));double phase=.37,brownian=0;std::array<float,480> pcm{};
 for(std::uint64_t first=0;first<static_cast<std::uint64_t>(c.duration*rate);first+=pcm.size()) {
  for(std::size_t i=0;i<pcm.size();++i) {
   const auto t=(first+i)/static_cast<double>(rate),local=std::fmod(t,.4);
   phase+=tau*truth(c,t)/rate;brownian+=c.sigma/std::sqrt(rate)*normal(rng);
   const auto ramp=c.ramps?std::min({1.,local/.004,(.4-local)/.004}):1.;
   const auto amplitude=t<c.loss && !(t>=12 && t<12.08)?.6*.5*(1-std::cos(std::numbers::pi*std::clamp(ramp,0.,1.))):0.;
   const auto white=normal(rng);pcm[i]=static_cast<float>(amplitude*std::cos(phase+brownian)+(c.noiseless?0:noise*white));
  }
  auto start=std::clock();a.push(pcm,first);ma.cpu+=static_cast<double>(std::clock()-start)/CLOCKS_PER_SEC;
  start=std::clock();b.push(pcm,first);mb.cpu+=static_cast<double>(std::clock()-start)/CLOCKS_PER_SEC;
  observe(a,ma,c,first+pcm.size());observe(b,mb,c,first+pcm.size());
 }
 for(const auto entry:std::array{std::pair{&a,&ma},std::pair{&b,&mb}}) {
  const auto s=entry.first->snapshot();const auto& m=*entry.second;
  std::cout<<std::setprecision(10)<<"CONTINUOUS name="<<c.name<<" seed="<<c.seed<<" CN0="<<c.cn0<<" sigma="<<c.sigma<<" drift="<<c.drift<<" drift_start="<<c.drift_start<<" ramps="<<c.ramps<<" noiseless="<<c.noiseless<<" continuous="<<(entry.first==&b)<<" first="<<m.first<<" availability="<<m.qualified/static_cast<double>(m.polls)<<" coherent_availability="<<m.coherent/static_cast<double>(m.polls)<<" error="<<m.error<<" U="<<m.u<<" max_error="<<m.max_error<<" max_ratio="<<m.max_ratio<<" estimates="<<m.estimates<<" escapes="<<m.escapes<<" gross_error_100hz="<<m.wrong<<" imprecise_2hz="<<m.imprecise2<<" dated="<<m.dated<<" dated_escapes="<<m.dated_escapes<<" max_aperture="<<m.max_aperture<<" preloss_resets="<<m.preloss_resets<<" resets="<<s.continuous_phase_resets<<" last_support="<<m.support<<" last_age="<<m.age<<" withdraw="<<m.withdraw<<" final_qualified="<<s.qualified<<" CPU_per_audio_second="<<m.cpu/c.duration<<" bytes="<<entry.first->workspace_bytes()<<" alternate_tone_selection=UNTESTED\n";
 }
 return ma.wrong||mb.wrong||a.snapshot().qualified||b.snapshot().qualified?1:ma.escapes||mb.escapes||ma.dated_escapes||mb.dated_escapes?2:0;
}
int main(int argc,char** argv) {
 std::vector<Case> cases;for(double cn0:{30.,40.})for(unsigned seed=1;seed<=6;++seed) {
  cases.push_back({cn0,0,0,0,64,60,seed,"constant"});
  cases.push_back({cn0,.03,1.6,0,64,60,seed,"drift_start"});
  cases.push_back({cn0,0,0,.03,64,60,seed,"phase_diffusion"});
 }
 cases.push_back({60,.5,0,0,64,60,7,"strong_drift"});cases.push_back({20,0,0,0,24,20,8,"boundary"});
 cases.push_back({40,0,0,.01,164,160,9,"numeric_window"});
 if(argc>1 && std::string(argv[1])=="quick")cases={{30,0,0,0,16,14,1,"quick30"},{40,.03,1.6,.03,16,14,1,"quick40_drift_diffusion"}};
 if(argc>1 && std::string(argv[1])=="model")cases={{40,.03,0,0,64,60,1,"drift_t0"},{40,.03,30,0,64,60,1,"drift_late30"}};
 if(argc>1 && std::string(argv[1])=="ramps")cases={{60,.03,0,0,16,14,1,"deterministic_ramp",true,true},{60,.03,0,0,16,14,1,"deterministic_no_ramp",false,true}};
 unsigned functional=0,coverage=0;for(const auto& c:cases){const auto code=run(c);functional+=code==1;coverage+=code==2;}
 std::cout<<"CONTINUOUS_RESULT functional_failures="<<functional<<" cases_with_interval_escapes="<<coverage<<'\n';return functional?1:coverage?2:0;
}
