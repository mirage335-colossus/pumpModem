#include "datapump/fast/preset.hpp"
#include "datapump/fast/codec.hpp"
#include "datapump/fast/modem.hpp"
#include "acoustic_ofdm.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>
using namespace datapump::fast;
struct Case {const char* name;double echo=0,delay=0,ppm=0;bool cutoff=false,noise_only=false,static_echo=false;};
int main(int argc,char** argv) {
 const double expected=argc>1?std::atof(argv[1]):0;
 const auto p=resolve_snr_preset(Channel::acoustic,expected).profile;
 const auto intervals=cycle_intervals(p);std::mt19937 bits(947311);
 std::vector<std::uint8_t> source(intervals*physical_interval_bits);for(auto& b:source)b=bits()&1;
 std::size_t sent=0;Transmitter tx(p,[&](std::span<std::uint8_t> b){if(sent==source.size())return false;std::copy_n(source.begin()+sent,b.size(),b.begin());sent+=b.size();return true;});
 std::array<float,4096> buf{};std::vector<float> pcm;
 while(!tx.finished()){auto n=tx.read(buf);pcm.insert(pcm.end(),buf.begin(),buf.begin()+n);}
 const double power=std::pow(p.amplitude/4.5,2),sigma=std::sqrt(power*p.sample_rate/(2*17500*std::pow(10.,expected/10)));
 std::cout<<"profile expected="<<expected<<" qam="<<p.constellation<<" rate="<<code_rate_name(p.code_rate)<<" D="<<p.interleave_depth<<" FFT="<<p.ofdm_fft_size<<" bandwidth="<<occupied_bandwidth_hz(p)<<" intervals="<<intervals<<" seconds="<<double(pcm.size())/p.sample_rate<<" noise_sigma="<<sigma<<std::endl;
 const std::array cases{Case{"awgn"},Case{"static_echo0.7",.7,0,0,false,false,true},Case{"static_echo0.9",.9,0,0,false,false,true},Case{"echo0.3",.3},Case{"echo0.5",.5},Case{"echo0.7",.7},Case{"delay16",0,16},Case{"clock100ppm",0,0,100},Case{"silence_after20",0,0,0,true},Case{"noise_only",0,0,0,false,true}};
 for(const auto& c:cases) {
  if(argc>2&&(","+std::string(argv[2])+",").find(","+std::string(c.name)+",")==std::string::npos)continue;
  std::mt19937 random(133831);std::normal_distribution<float> noise(0,sigma);
  std::uint64_t accepted=0,bad=0,erased=0,diagblock=~0ULL;double acquired=-1,complete=-1;bool last=false;std::size_t run=0,maxrun=0,pass=0,fail=0;
  std::ofstream diagnostic("/tmp/"+std::string(argv[0]).substr(std::string(argv[0]).find_last_of('/')+1)+"-"+std::to_string(int(expected))+"-"+c.name+".csv");diagnostic<<"block,admitted,clock_ppm,delay\n";
  acoustic_ofdm::Receiver rx(p,[&](std::span<const float> b){for(float v:b){if(accepted<source.size()){erased+=v==0;bad+=v!=0&&((v>0)!=bool(source[accepted]));}++accepted;}},{},{},[&](const acoustic_ofdm::Observation& o){if(o.block==diagblock)return;diagblock=o.block;diagnostic<<o.block<<','<<o.admitted<<','<<o.clock_ppm<<','<<o.timing_correction<<'\n';last=o.admitted;if(last){++pass;run=0;}else{++fail;maxrun=std::max(maxrun,++run);}});
  auto sample=[&](double x){if(x<0||x>=pcm.size()-1)return 0.;const auto i=std::size_t(x);const auto f=x-i;return (1-f)*pcm[i]+f*pcm[i+1];};
  const auto event=std::uint64_t(20)*p.sample_rate;
  const auto signal_end=c.noise_only?std::uint64_t(18)*p.sample_rate:pcm.size();
  const auto end=signal_end+end_silence_samples(p);
  for(std::uint64_t at=0;at<end&&!rx.progress().physical_complete;) {
   const auto n=std::min<std::uint64_t>(buf.size(),end-at);
   for(std::size_t j=0;j<n;++j){const auto pos=at+j;double x=pos;if(pos>=event)x=event+(pos-event)/(1+c.ppm*1e-6)-c.delay;double v=sample(x);if((pos>=event||c.static_echo)&&c.echo)v+=c.echo*sample(x-240);if(c.noise_only||(c.cutoff&&pos>=event)||pos>=signal_end)v=0;buf[j]=v+noise(random);}
   rx.push(std::span<const float>(buf).first(n));at+=n;
   if(acquired<0&&rx.progress().acquired)acquired=double(at)/p.sample_rate;
   if(rx.progress().physical_complete)complete=double(at)/p.sample_rate;
  }
  std::cout<<"case="<<c.name<<" acquired="<<acquired<<" complete="<<complete<<" premature="<<(complete>=0&&complete<double(signal_end)/p.sample_rate&&!c.cutoff&&!c.noise_only)<<" accepted_bits="<<accepted<<" errors="<<bad<<" erased="<<erased<<" pass_blocks="<<pass<<" fail_blocks="<<fail<<" max_failed="<<maxrun<<" final_clock_ppm="<<rx.progress().clock_error_ppm<<std::endl;
 }
}
