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
using namespace datapump;
int main() {
 constexpr unsigned rate=6120;constexpr double tau=2*std::numbers::pi,cn0=23.4,clock_scale=1.0001,offset=30;
 const auto phase_sigma=.50249378105604448*std::numbers::pi/180;
 unsigned failures=0,coverage=0;
 for(bool composite:{false,true}) {
  modem::Config c;c.sample_rate=rate;c.carrier_hz=1500;c.bandwidth_hz=36;c.spreading_factor=256;
  c.dsss_factor=10;c.outer_dsss_version=modem::OuterDsssVersion::interleaved_v2;c.integration_seconds=14.2;
  c.training_seconds=2;c.scramble=true;c.memory_limit=64ULL*1024*1024;c.data_key=Crypto{std::array<std::uint8_t,32>{}};
  helper::Options options;options.power_w=40;options.data_power_w=4;
  const auto geometry=helper::plan(options,helper::placement_band(c),rate);
  const auto pilot=geometry.frequency_hz,truth=pilot*clock_scale+offset;
  const auto data_gain=std::sqrt(.0045/modem::nominal_signal_power)/modem::outer_dsss_transmit_gain(c);
  helper::Generator source(options,helper::Plan{pilot,.3,composite?data_gain:0,40,composite?geometry.auxiliary_frequency_hz:0},1800000000,true,c.data_key);
  modem::PatternTransmitter payload(Bytes{1,0,1,1,0,0,1,0},c,1800000000,0,false);
  helper::ToneTrackerConfig config{rate,pilot,300,phase_sigma,.5,true,false};auto continuous=config;continuous.continuous_pilot=true;
  helper::ToneTracker baseline(config,helper::ToneTracker::workspace_required(config));
  helper::ToneTracker assisted(continuous,helper::ToneTracker::workspace_required(continuous));
  struct State {unsigned estimates=0,escapes=0,qualified=0;std::uint64_t last=UINT64_MAX;double first=0,error=0,u=0,cpu=0,eligibility=0;};
  std::array<State,2> state{};std::array<float,61> pcm{};std::array<std::complex<double>,61> analytic{};
  std::mt19937 random{5181};std::normal_distribution<double> normal;double phase=0,data_energy=0;
  const auto noise=.3*helper::pilot_amplitude_fraction*.5*std::sqrt(rate/std::pow(10.,cn0/10));
  for(std::uint64_t first=0;first<64ULL*rate;first+=pcm.size()) {
   const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(pcm.size(),64ULL*rate-first));
   if(payload.read_analytic(std::span(analytic).first(count))!=count)return 3;
   for(std::size_t i=0;i<count;++i) {
    const auto t=(first+i)/static_cast<double>(rate);phase+=phase_sigma/std::sqrt(rate)*normal(random);
    const auto z=source.mix_analytic(analytic[i],static_cast<long double>(t*clock_scale)+.137L,rate)*std::polar(1.,tau*offset*t+phase);
    pcm[i]=static_cast<float>(z.real()+noise*normal(random));data_energy+=std::norm(analytic[i])*data_gain*data_gain/2;
   }
   for(unsigned j=0;j<2;++j) {
    auto& tracker=j?assisted:baseline;auto& m=state[j];const auto start=std::clock();
    tracker.push(std::span(pcm).first(count),first);m.cpu+=static_cast<double>(std::clock()-start)/CLOCKS_PER_SEC;
    const auto s=tracker.snapshot();if(!s.qualified)continue;++m.qualified;
    if(!m.first)m.first=(first+count)/static_cast<double>(rate);
    if(!m.eligibility && s.uncertainty_hz<=.1)m.eligibility=(first+count)/static_cast<double>(rate);
    if(s.sample==m.last)continue;m.last=s.sample;++m.estimates;m.error=s.frequency_hz-truth;m.u=s.uncertainty_hz;
    if(std::abs(m.error)>m.u){++m.escapes;std::cout<<std::setprecision(12)<<"HF_LIKE_ESCAPE composite="<<composite<<" continuous="<<j<<" date="<<s.sample/static_cast<double>(rate)<<" error="<<m.error<<" U="<<m.u<<" coherent="<<s.frequency_continuous_phase<<'\n';}
   }
  }
  for(unsigned j=0;j<2;++j) {
   const auto& m=state[j];const auto s=(j?assisted:baseline).snapshot();failures+=!s.qualified;coverage+=m.escapes!=0;
   std::cout<<std::setprecision(10)<<"HF_LIKE_CONTINUOUS sampled_pilot_CN0="<<cn0<<" RF_budget_CN0=UNMODELED total_Helper_CN0="<<cn0-10*std::log10(.9)<<" composite="<<composite<<" continuous="<<j<<" rate="<<rate<<" SF="<<c.spreading_factor<<" pilot="<<pilot<<" aux="<<geometry.auxiliary_frequency_hz<<" helper_W=40 data_W=4 phase_sigma="<<phase_sigma<<" clock_ppm=100 CFO=30 payload_clock_resampling=UNEXECUTED data_mean_power="<<data_energy/(64*rate)<<" first="<<m.first<<" first_U_le_0p1="<<m.eligibility<<" available="<<m.qualified/(64.*rate/pcm.size())<<" error="<<m.error<<" U="<<m.u<<" estimates="<<m.estimates<<" escapes="<<m.escapes<<" coherent_aperture="<<s.coherent_aperture_seconds<<" support="<<s.frequency_support_seconds<<" age="<<s.observation_age_seconds<<" resets="<<s.continuous_phase_resets<<" model_resets="<<s.continuous_model_resets<<" measured_CN0="<<s.carrier_to_noise_db_hz<<" CPU_per_audio_second="<<m.cpu/64<<" bytes="<<(j?assisted:baseline).workspace_bytes()<<" ordinary_reception=UNEXECUTED\n";
  }
 }
 std::cout<<"HF_LIKE_RESULT functional_failures="<<failures<<" cases_with_escapes="<<coverage<<'\n';return failures?1:coverage?2:0;
}
