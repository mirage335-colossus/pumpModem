#include "pilot_coarse.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <vector>

namespace pr=pilot_reference;
namespace {
constexpr std::uint32_t fs=24000;
constexpr double expected=2450.3125,amplitude=.012,duration=38;
struct Case {const char* name;double cn0=30,slope=0,sigma=.01,offset=.58,half_width=1.74,phase=.287,loss=0;std::uint64_t origin=1;};
double quantile(std::vector<double> x,unsigned percentile){if(x.empty())return 0;std::sort(x.begin(),x.end());return x[std::min(x.size()-1,x.size()*percentile/100)];}
double cpu(){return static_cast<double>(std::clock())/CLOCKS_PER_SEC;}
void require(bool condition,const char* message){if(!condition){std::cout.flush();throw std::runtime_error(message);}}
void run_case(const Case& c){
 pr::Config config;config.rate=fs;config.phase_sigma=c.sigma;config.supported_slew_hz_per_second=.03;
 pr::CoarseFFT discovery(fs,expected,c.half_width,c.origin);
 std::unique_ptr<pr::Tracker> tracker;
 const auto noise_sigma=amplitude*.5*std::sqrt(fs/std::pow(10.,c.cn0/10));
 std::mt19937 nr{12397},br{84723};std::normal_distribution<double> normal;
 std::array<float,733> pcm{};std::array<double,16384> truth_phase{};
 std::size_t blocks=0,qualified=0,anchors=0,escapes=0,domains=0,wrong_lifts=0,dates=0,lost_qualified=0,independent=0,useful_steered=0;
 double first_qualified=0,longest=0,run_start=0,maximum_error=0,maximum_ref_age=0,first_lost_withdraw=0,phase_bias=0,work_cpu=0;
 double latest_completed_frontier=0;bool latest_completed_valid=false;
 bool phase_saved=false;std::vector<double> errors,uncertainties,supports;double last_u=0,last_error=0,bootstrap=0;
 const auto trend=[&](double t){return expected+c.offset+c.slope*t;};
 const auto seconds=[&](std::uint64_t sample){return pr::elapsed(sample,c.origin,fs);};
 const auto on_block=[&](const pr::BlockResult& b){
  ++blocks;latest_completed_frontier=seconds(b.end);latest_completed_valid=b.estimate.valid;const auto center=(seconds(b.first)+seconds(b.end-1))*.5;
  maximum_ref_age=std::max(maximum_ref_age,b.reference_age);
  dates+=b.reference_captured_end>b.first||b.reference_sample>=b.reference_captured_end||b.estimate.end>b.end;
  const auto actual_coarse=std::max(std::abs(trend(seconds(b.first))-b.coarse_hz),std::abs(trend(seconds(b.end-1))-b.coarse_hz));
  const auto actual_fine=std::max(std::abs(trend(seconds(b.first))-b.nco_hz),std::abs(trend(seconds(b.end-1))-b.nco_hz));
  domains+=actual_coarse>b.coarse_R+1e-10||actual_fine>b.residual_bound+1e-10;
  if(b.steering){const auto date=seconds(tracker->origin)+b.past.seconds;
   dates+=b.past.end>b.first;domains+=std::abs(b.past.hz-trend(date))>b.past.uncertainty;}
  if(b.accepted){++anchors;if(tracker->point_count==1)phase_saved=false;
   const auto i=static_cast<std::size_t>(b.first-c.origin)+(b.end-b.first-1)/2;
   const auto fraction=.5*((b.end-b.first-1)%2);
   const auto true_phase=truth_phase[i%truth_phase.size()]*(1-fraction)+truth_phase[(i+1)%truth_phase.size()]*fraction;
   const auto relative=b.phase-(true_phase/pr::tau-tracker->seed_hz*b.seconds);
   if(!phase_saved){phase_bias=relative;phase_saved=true;}
   wrong_lifts+=std::abs(relative-phase_bias)>.5;
  }
  if(c.loss&&seconds(b.first)>=c.loss){lost_qualified+=b.estimate.valid;if(!b.estimate.valid&&!first_lost_withdraw)first_lost_withdraw=seconds(b.end);}
  if(!b.estimate.valid){run_start=0;return;}
  ++qualified;if(!first_qualified)first_qualified=seconds(b.end);if(!run_start)run_start=seconds(b.end);longest=std::max(longest,seconds(b.end)-run_start);
  const auto& e=b.estimate;const auto date=seconds(tracker->origin)+e.seconds;
  const auto error=e.hz-trend(date);const auto contained=std::abs(error)<=e.uncertainty;
  if(b.steering&&e.uncertainty<.1&&(!c.loss||seconds(b.end)<=c.loss))++useful_steered;
  escapes+=!contained;maximum_error=std::max(maximum_error,std::abs(error));errors.push_back(std::abs(error));uncertainties.push_back(e.uncertainty);supports.push_back(e.support);last_u=e.uncertainty;last_error=error;
  std::cout<<"PILOT_FIT name="<<c.name<<" date="<<date<<" support_first="<<seconds(e.first)<<" support_end="<<seconds(e.end)<<" captured_frontier="<<seconds(b.end)<<" age="<<seconds(b.end)-date<<" error="<<error<<" U="<<e.uncertainty<<" span="<<e.support<<" count="<<e.count<<" steering="<<b.steering<<" coarse_radius="<<b.coarse_R<<" aperture_radius="<<b.residual_bound<<'\n';
  require(contained&&domains==0&&dates==0&&wrong_lifts==0,"pilot interval/domain/lift/date escape");
 };
 double brownian=0;
 for(std::uint64_t offset=0;offset<static_cast<std::uint64_t>(duration*fs);offset+=pcm.size()){
  const auto count=std::min<std::size_t>(pcm.size(),duration*fs-offset);
  for(std::size_t i=0;i<count;++i){const auto sample=offset+i;const auto t=static_cast<double>(sample)/fs;
   brownian+=std::sqrt(2.)*c.sigma/std::sqrt(static_cast<double>(fs))*normal(br);
   const auto phase=c.phase+pr::tau*((expected+c.offset)*t+.5*c.slope*t*t)+brownian;
   truth_phase[sample%truth_phase.size()]=phase;
   pcm[i]=static_cast<float>((!c.loss||t<c.loss?amplitude*std::cos(phase):0)+noise_sigma*normal(nr));
  }
  const auto start_cpu=cpu();std::size_t cursor=0;
  while(cursor<count){const auto first=c.origin+offset+cursor;const auto consumed=discovery.push(std::span(pcm).subspan(cursor,count-cursor),first);
   if(tracker)tracker->push(std::span(pcm).subspan(cursor,consumed),first,on_block);cursor+=consumed;
   if(discovery.finished){const auto seed=discovery.result;
    if(seed.valid){const auto seed_date=seconds(seed.center)+seed.fraction/fs;
     require(std::abs(seed.hz-trend(seed_date))<=seed.uncertainty,"independent FFT center interval escape");
     ++independent;if(!tracker){tracker=std::make_unique<pr::Tracker>(config,seed.hz,seed.uncertainty,seed.noise_psd,seed.end);bootstrap=seconds(seed.end);}
     tracker->observe_reference({true,seed.center,seed.end,seed.hz,seed.uncertainty,seed.noise_psd,seed.fraction});}
    discovery.restart();
   }
  }
  work_cpu+=cpu()-start_cpu;
 }
 require(tracker!=nullptr,"pilot discovery never qualified");
 require(useful_steered>0,"pilot never produced a useful qualified steered output");
 if(!c.loss)require(latest_completed_valid&&last_u<.1&&duration-latest_completed_frontier<=pr::period+1./fs,"pilot final completed block invalid, stale or imprecise");
 require(domains==0&&escapes==0&&wrong_lifts==0&&dates==0&&lost_qualified==0,"pilot correctness/loss failure");
 if(c.loss)require(first_lost_withdraw&&first_lost_withdraw-c.loss<=.8+1./fs,"pilot loss withdrawal too late");
 std::cout<<"PILOT_SUMMARY name="<<c.name<<" input_CN0_dBHz="<<c.cn0<<" trend_slope="<<c.slope<<" Brownian_sigma="<<c.sigma<<" initial_offset="<<c.offset<<" origin="<<c.origin<<" blocks="<<blocks<<" qualified="<<qualified<<" fraction="<<static_cast<double>(qualified)/std::max<std::size_t>(1,blocks)<<" discovery_seconds="<<bootstrap<<" first_qualified_frontier="<<first_qualified<<" longest_qualified_span="<<longest<<" U_median="<<quantile(uncertainties,50)<<" U_p90="<<quantile(uncertainties,90)<<" final_U="<<last_u<<" error_median="<<quantile(errors,50)<<" error_p90="<<quantile(errors,90)<<" max_error="<<maximum_error<<" final_error="<<last_error<<" max_support="<<quantile(supports,100)<<" anchors="<<anchors<<" useful_qualified_steered="<<useful_steered<<" latest_completed_frontier="<<latest_completed_frontier<<" latest_completed_valid="<<latest_completed_valid<<" steered_blocks="<<tracker->steered_blocks<<" branch_disagreements="<<tracker->branch_disagreements<<" ambiguities="<<tracker->ambiguities<<" independent_updates="<<independent<<" FFTs="<<discovery.transforms<<" FFT_butterflies="<<discovery.butterflies<<" FFT_buffer_bytes="<<discovery.workspace_bytes()<<" coarse_object_bytes="<<sizeof(pr::CoarseFFT)<<" numeric_capacity="<<pr::capacity<<" tracker_bytes="<<sizeof(pr::Tracker)<<" component_total_bytes="<<(discovery.workspace_bytes()+sizeof(pr::CoarseFFT)+sizeof(pr::Tracker))<<" maximum_reference_age="<<maximum_ref_age<<" loss_withdraw_frontier="<<first_lost_withdraw<<" interval_escapes="<<escapes<<" domain_escapes="<<domains<<" wrong_lifts="<<wrong_lifts<<" date_errors="<<dates<<" lost_qualified="<<lost_qualified<<" prototype_CPU_seconds="<<work_cpu<<" CPU_per_audio_second="<<work_cpu/duration<<" ordinary_receiver=UNEXECUTED\n";
}
}
int main(){std::cout<<std::setprecision(12);unsigned failures=0;std::vector<Case> cases;
 for(double cn0:{20.,30.,40.}){cases.push_back({cn0==20?"constant20":cn0==30?"constant30":"constant40",cn0});Case d{cn0==20?"linear20":cn0==30?"linear30":"linear40",cn0};d.slope=.03;cases.push_back(d);}
 Case negative{"negative30"};negative.slope=-.03;cases.push_back(negative);
 Case large{"large_origin30"};large.origin=(std::uint64_t{1}<<60)+123;large.phase=1.137;cases.push_back(large);
 Case wide{"wide_capture30"};wide.offset=3000.58;wide.half_width=9000;cases.push_back(wide);
 Case loss{"loss30"};loss.loss=26;cases.push_back(loss);
 Case coherent{"coherent30"};coherent.sigma=0;cases.push_back(coherent);
 for(const auto& c:cases)try{run_case(c);}catch(const std::exception& e){++failures;std::cout<<"PILOT_FAIL name="<<c.name<<" reason="<<e.what()<<'\n';}
 std::cout<<"PILOT_RESULT cases="<<cases.size()<<" failures="<<failures<<" statistical_coverage=UNEXECUTED application_integration=UNEXECUTED ordinary_receiver=UNEXECUTED\n";return failures?1:0;
}
