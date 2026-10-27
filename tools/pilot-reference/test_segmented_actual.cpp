#include "datapump/helper_tone_tracker.hpp"
#include "datapump/helper_carrier.hpp"
#include "datapump/pattern_code.hpp"
#include <array>
#include <cmath>
#include <complex>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <random>
#include <string>
#include <vector>
using namespace datapump;
int main(int argc,char** argv){constexpr unsigned rate=48000;constexpr double tau=2*std::numbers::pi,pilot=2450,offset=3000.37;
 unsigned failures=0,coverage_cases=0;std::vector<double> levels{30.,40.,60.};unsigned seeds=1;
 if(argc>1 && std::string(argv[1])=="boundary")levels={24.};
 if(argc>1 && std::string(argv[1])=="gates")levels={24.,30.,40.};
 if(argc>1 && std::string(argv[1])=="ensemble"){levels={30.};seeds=12;}
 for(unsigned seed=1;seed<=seeds;++seed)for(double cn0:levels)for(bool mixture:{false,true})for(bool random_phase:{false,true}) {
  helper::ToneTrackerConfig tc{rate,pilot,9000,.01,.5,true};helper::ToneTracker tracker(tc,helper::ToneTracker::workspace_required(tc));
  modem::Config c;c.sample_rate=rate;c.carrier_hz=1500;c.bandwidth_hz=36;c.dsss_factor=10;c.outer_dsss_version=modem::OuterDsssVersion::interleaved_v2;c.spreading_factor=64;c.integration_seconds=14.2;c.training_seconds=2;c.scramble=true;c.memory_limit=64ULL*1024*1024;
  Crypto key{std::array<std::uint8_t,32>{}};c.data_key=key;
  modem::PatternTransmitter payload(Bytes{1,0,1,1,0,0,1,0},c,1800000000,0,false);
  helper::Options o;o.power_w=40;o.data_power_w=4;
  const auto gain=std::sqrt(.0045/modem::nominal_signal_power)/modem::outer_dsss_transmit_gain(c);
  helper::Generator source(o,helper::Plan{pilot,.3,mixture?gain:0,40,mixture?pilot-400:0},1800000000,true,key);
  std::array<float,480> pcm{};std::array<std::complex<double>,480> analytic{};
  std::mt19937 rng{122+seed};std::normal_distribution<double> normal;std::uniform_real_distribution<double> uniform(-std::numbers::pi,std::numbers::pi);
  const auto noise=.3*helper::pilot_amplitude_fraction*.5*std::sqrt(rate/std::pow(10.,cn0/10));
  std::array<double,128> hop_phases{};hop_phases[0]=.4;std::uint64_t phase_last_date=UINT64_MAX;unsigned phase_fits=0,phase_escapes=0;double phase_max_error=0,phase_last_U=0;
  double hop_phase=.4,first=0,last_error=0,last_u=0,maximum_error=0,data_energy=0,tracker_cpu=0;unsigned qualified=0,escapes=0,wrong=0,frequency_estimates=0,frequency_escaped_estimates=0;std::uint64_t last_frequency_date=UINT64_MAX;const double source_alignment=.137;
  for(std::uint64_t n=0;n<38ULL*rate;n+=pcm.size()) {
   if(n%(rate*2/5)==0){if(n && random_phase)hop_phase=uniform(rng);hop_phases[n/(rate*2/5)]=hop_phase;}
   const auto produced=payload.read_analytic(analytic);if(produced!=analytic.size()){++failures;break;}
   for(std::size_t i=0;i<pcm.size();++i){const auto t=static_cast<double>(n+i)/rate;data_energy+=std::norm(analytic[i])*gain*gain/2;
    const auto z=source.mix_analytic(analytic[i],static_cast<long double>(t)+source_alignment,rate)*std::polar(1.,tau*offset*t+hop_phase);
    pcm[i]=static_cast<float>(z.real()+noise*normal(rng));}
   const auto started=std::clock();tracker.push(pcm,n);tracker_cpu+=static_cast<double>(std::clock()-started)/CLOCKS_PER_SEC;const auto s=tracker.snapshot();
   if(s.phase_fit_qualified && s.phase_fit_end_sample!=phase_last_date){
    phase_last_date=s.phase_fit_end_sample;++phase_fits;bool escaped=false;
    const auto date=static_cast<double>(s.phase_fit_sample)+s.phase_fit_sample_fraction;
    for(auto q=s.phase_fit_first_sample+512;q+512<s.phase_fit_end_sample;q+=rate/50){
     const auto t=static_cast<double>(q)/rate,dt=(static_cast<double>(q)-date)/rate;
     const auto predicted=s.phase_fit_cycles+s.phase_fit_frequency_hz*dt;
     const auto truth=pilot*source_alignment+(pilot+offset)*t+hop_phases[q/(rate*2/5)]/tau;
     const auto error=std::abs(std::remainder(predicted-truth,1.));phase_max_error=std::max(phase_max_error,error);
     const auto u=s.phase_fit_uncertainty_cycles+s.phase_fit_frequency_uncertainty_hz*std::abs(dt);
     escaped|=error>u;
    }
    phase_escapes+=escaped;phase_last_U=s.phase_fit_uncertainty_cycles;
   }
   if(s.qualified){++qualified;if(!first)first=static_cast<double>(n+pcm.size())/rate;
    const auto error=s.frequency_hz-pilot-offset;escapes+=std::abs(error)>s.uncertainty_hz;if(last_frequency_date!=s.sample){++frequency_estimates;frequency_escaped_estimates+=std::abs(error)>s.uncertainty_hz;if(std::abs(error)>s.uncertainty_hz)std::cout<<std::setprecision(12)<<"ACTUAL_FREQUENCY_ESCAPE seed="<<seed<<" CN0="<<cn0<<" mixture="<<mixture<<" random_phase="<<random_phase<<" date="<<s.sample/static_cast<double>(rate)<<" error="<<error<<" U="<<s.uncertainty_hz<<" fallback="<<s.frequency_used_fallback<<" gap="<<s.frequency_gap_index<<" first="<<s.frequency_context_first_sample<<" end="<<s.frequency_context_end_sample<<"\n";last_frequency_date=s.sample;}wrong+=std::abs(error)>2;maximum_error=std::max(maximum_error,std::abs(error));last_error=error;last_u=s.uncertainty_hz;}
  }
  const auto s=tracker.snapshot();failures+=!s.qualified||wrong;coverage_cases+=frequency_escaped_estimates||phase_escapes;
  std::cout<<std::setprecision(9)<<"ACTUAL_SEGMENTED seed="<<seed<<" pilot_CN0="<<cn0<<" total_Helper_CN0="<<cn0-10*std::log10(.9)<<" actual_aux_Robust="<<mixture<<" independent_hop_phase="<<random_phase<<" alignment="<<source_alignment<<" data_mean_power="<<data_energy/(38*rate)<<" first="<<first<<" available="<<qualified/3800.<<" final_error="<<last_error<<" final_U="<<last_u<<" maximum_error="<<maximum_error<<" escaped_polls="<<escapes<<" frequency_estimates="<<frequency_estimates<<" frequency_escaped_estimates="<<frequency_escaped_estimates<<" wrong_tone="<<wrong<<" final_CN0_measured="<<s.carrier_to_noise_db_hz<<" coherent="<<s.coherent_aperture_seconds<<" fallback="<<s.frequency_used_fallback<<" gap="<<s.frequency_gap_index<<" aperture_cube="<<s.frequency_aperture_cube_seconds3<<" support="<<s.frequency_support_seconds<<" age="<<s.observation_age_seconds<<" phase_fits="<<phase_fits<<" phase_fit_escapes="<<phase_escapes<<" phase_max_error_cycles="<<phase_max_error<<" phase_last_U_cycles="<<phase_last_U<<" tracker_CPU="<<tracker_cpu<<" tracker_CPU_per_audio_second="<<tracker_cpu/38<<" bytes="<<tracker.workspace_bytes()<<" ordinary_reception=UNEXECUTED\n";
 }
 std::cout<<"ACTUAL_SEGMENTED_RESULT failures="<<failures<<" coverage_cases_with_escapes="<<coverage_cases<<'\n';return failures?1:coverage_cases?2:0;
}
