#include "datapump/helper_tone_tracker.hpp"
#include "datapump/helper_segmented_precision.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <random>
#include <string>
#include <vector>
using namespace datapump::helper;
constexpr double tau=2*std::numbers::pi;
struct Case {std::string name;double cn0=30,drift=0,phase_noise=0,offset=3000.37,loss=0;bool hops=false,mix=false,dated=false;unsigned seed=1;double duration=38;bool noise_only=false;double gap=0,gap_seconds=1.2,gap_shift=0;bool trace=false;};
int run(const Case& c){
 constexpr unsigned rate=48000;ToneTrackerConfig cfg{rate,2062.5,9000,c.phase_noise,.5,true};
 ToneTracker tracker(cfg,ToneTracker::workspace_required(cfg));
 std::mt19937 rng(c.seed);std::normal_distribution<double> normal;std::uniform_real_distribution<double> uniform(-std::numbers::pi,std::numbers::pi);
 const double amp=.6,noise=amp*.5*std::sqrt(rate/std::pow(10.,c.cn0/10));
 std::array<float,480> samples{};double phase=.4,brownian=0;unsigned hop=0;
 std::size_t polls=0,qualified=0,escapes=0,wrong=0,final_qualified=0,estimates=0,escaped_estimates=0;std::uint64_t last_date=UINT64_MAX;double first=0,last_error=0,last_u=0,maximum=0,work=0,loss_withdraw=0;double last_support=0,last_cno=0;
 for(std::uint64_t n=0;n<static_cast<std::uint64_t>(c.duration*rate);n+=samples.size()){
  const auto current_hop=static_cast<unsigned>(n/(rate*2/5));
  if(c.hops && current_hop!=hop){hop=current_hop;phase=uniform(rng);if(c.dated)tracker.begin_pilot_segment(n,2062.5,9000,true);}
  for(std::size_t i=0;i<samples.size();++i){const auto t=static_cast<double>(n+i)/rate;
   brownian+=c.phase_noise/std::sqrt(rate)*normal(rng);const auto evolving=c.gap?c.gap_shift*std::clamp((t-c.gap)/c.gap_seconds,0.,1.):0.;phase+=tau*(2062.5+c.offset+c.drift*t+evolving)/rate;
   const auto local=std::fmod(t,.4);const auto u=std::clamp((local-.236)/.008,0.,1.);const auto aux_sign=1-2*u*u*u*(10+u*(-15+6*u));
   const auto signal=!c.noise_only && (!c.gap || t<c.gap || t>=c.gap+c.gap_seconds) && (!c.loss||t<c.loss)?amp*std::cos(phase+brownian):0.;
   // Synthetic modulated interferers discriminate isolation; actual Robust mixture belongs to integration qualification.
   const auto aux=c.mix?amp/3*aux_sign*std::cos(phase+brownian-tau*400*t):0.;
   const auto data=c.mix?amp/3*(std::sin(tau*36*t)>0?1:-1)*std::cos(phase-tau*1000*t):0.;
   samples[i]=static_cast<float>(signal+aux+data+noise*normal(rng));
  }
  const auto start=std::clock();tracker.push(samples,n);work+=static_cast<double>(std::clock()-start)/CLOCKS_PER_SEC;
  const auto s=tracker.snapshot();++polls;
  if(c.loss && static_cast<double>(n+samples.size())/rate>c.loss+.1 && !s.qualified && !loss_withdraw)loss_withdraw=static_cast<double>(n+samples.size())/rate;
  if(s.qualified && (!c.loss || static_cast<double>(n+samples.size())/rate<c.loss)){
   ++qualified;if(!first)first=static_cast<double>(n+samples.size())/rate;
   const auto date=static_cast<double>(s.sample)/rate;const auto truth=2062.5+c.offset+c.drift*date+(c.gap?c.gap_shift*std::clamp((date-c.gap)/c.gap_seconds,0.,1.):0.);
   const auto error=s.frequency_hz-truth;escapes+=std::abs(error)>s.uncertainty_hz;if(s.sample!=last_date){++estimates;escaped_estimates+=std::abs(error)>s.uncertainty_hz;last_date=s.sample;if(std::abs(error)>s.uncertainty_hz)std::cout<<std::setprecision(12)<<"FREQUENCY_ESCAPE name="<<c.name<<" seed="<<c.seed<<" date="<<s.sample/static_cast<double>(rate)<<" error="<<error<<" U="<<s.uncertainty_hz<<" fallback="<<s.frequency_used_fallback<<" gap="<<s.frequency_gap_index<<" first="<<s.frequency_context_first_sample<<" end="<<s.frequency_context_end_sample<<"\n";if(c.trace)std::cout<<std::setprecision(12)<<"FREQUENCY_TRACE date="<<s.sample/static_cast<double>(rate)<<" error="<<error<<" U="<<s.uncertainty_hz<<" slope="<<s.frequency_rate_hz_per_second<<" local_error="<<s.phase_fit_frequency_hz-truth<<" local_U="<<s.phase_fit_frequency_uncertainty_hz<<" support="<<s.frequency_support_seconds<<" CN0="<<s.carrier_to_noise_db_hz<<" escaped="<<(std::abs(error)>s.uncertainty_hz)<<"\n";}wrong+=std::abs(error)>2.;
   maximum=std::max(maximum,std::abs(error));last_error=error;last_u=s.uncertainty_hz;last_support=s.frequency_support_seconds;last_cno=s.carrier_to_noise_db_hz;
   if(n>static_cast<std::uint64_t>((c.duration-1)*rate))++final_qualified;
  }
 }
 const auto s=tracker.snapshot();
 std::cout<<std::setprecision(9)<<"SEGMENTED name="<<c.name<<" seed="<<c.seed<<" CN0="<<c.cn0<<" phase_noise="<<c.phase_noise<<" drift="<<c.drift<<" random_hops="<<c.hops<<" dated="<<c.dated<<" mixture="<<c.mix<<" first="<<first<<" availability="<<static_cast<double>(qualified)/polls<<" final_available="<<final_qualified<<" final_error="<<last_error<<" final_U="<<last_u<<" max_error="<<maximum<<" escapes="<<escapes<<" independent_estimates="<<estimates<<" escaped_estimates="<<escaped_estimates<<" wrong_tone="<<wrong<<" support="<<last_support<<" aperture="<<s.coherent_aperture_seconds<<" fallback="<<s.frequency_used_fallback<<" gap="<<s.frequency_gap_index<<" aperture_cube="<<s.frequency_aperture_cube_seconds3<<" observation_age="<<s.observation_age_seconds<<" measured_CN0="<<last_cno<<" loss_withdraw="<<loss_withdraw<<" CPU="<<work<<" CPU_per_audio_second="<<work/c.duration<<" bytes="<<tracker.workspace_bytes()<<'\n';
 return (wrong || (!c.loss && !final_qualified && c.cn0>=30 && !c.noise_only) || (c.loss && s.qualified) || (c.noise_only && qualified))+(escaped_estimates?1000:0);
}
int main(int argc,char** argv){unsigned failures=0,coverage_cases=0;std::vector<Case> cases;
 for(double cn0:{20.,30.,40.,60.}){Case c{"plain"+std::to_string(static_cast<int>(cn0)),cn0};cases.push_back(c);c.name="unknown_hops"+std::to_string(static_cast<int>(cn0));c.hops=true;c.mix=true;cases.push_back(c);}
 Case drift{"drift30",30,.03,.01};drift.hops=true;drift.mix=true;cases.push_back(drift);
 Case fast_drift{"supported_drift60",60,.5};cases.push_back(fast_drift);
 Case loss{"loss40",40};loss.loss=28;loss.hops=true;cases.push_back(loss);
 if(argc>1 && std::string(argv[1])=="dated"){cases.clear();for(double cn0:{30.,40.,60.}){Case c{"dated_hops"+std::to_string(static_cast<int>(cn0)),cn0};c.hops=c.mix=c.dated=true;cases.push_back(c);}}
 if(argc>1 && std::string(argv[1])=="ensemble"){cases.clear();for(unsigned seed=1;seed<=12;++seed){Case c{"ensemble30",30};c.hops=true;c.mix=true;c.seed=seed;cases.push_back(c);}}
 if(argc>1 && std::string(argv[1])=="boundary"){cases.clear();for(bool hops:{false,true}){Case c{hops?"boundary24_hops":"boundary24_fixed",24};c.hops=hops;c.mix=true;cases.push_back(c);}}
 if(argc>1 && std::string(argv[1])=="seed2"){cases.clear();Case c{"seed2_blind",30};c.seed=2;c.hops=c.mix=true;cases.push_back(c);c.name="seed2_dated";c.dated=true;cases.push_back(c);}
 if(argc>1 && std::string(argv[1])=="trace"){cases.clear();Case c{"plain30_trace",30};c.trace=true;cases.push_back(c);}
 if(argc>1 && std::string(argv[1])=="gap"){cases.clear();for(double cn0:{24.,40.,60.})for(double shift:{0.,.6}){Case c{shift?"gap_evolving":"gap_constant",cn0};c.gap=12;c.gap_shift=shift;cases.push_back(c);}}
 if(argc>1 && std::string(argv[1])=="noise"){cases.clear();for(unsigned seed=1;seed<=12;++seed){Case c{"noise_only",30};c.seed=seed;c.noise_only=true;cases.push_back(c);}}
 for(const auto& c:cases){const auto r=run(c);failures+=r%1000;coverage_cases+=r/1000;}
 std::cout<<"SEGMENTED_RESULT failures="<<failures<<" coverage_cases_with_escapes="<<coverage_cases<<" ordinary_receiver=UNEXECUTED actual_Robust_mixture=UNEXECUTED"<<'\n';return failures?1:coverage_cases?2:0;
}
