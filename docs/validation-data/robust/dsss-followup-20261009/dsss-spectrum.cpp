#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <iostream>
#include <numbers>
#include <vector>
using namespace datapump;
void fft(std::vector<std::complex<double>>& x) {
 for(size_t i=1,j=0;i<x.size();++i){size_t b=x.size()/2;for(;j&b;b/=2)j^=b;j^=b;if(i<j)std::swap(x[i],x[j]);}
 for(size_t l=2;l<=x.size();l*=2){auto w0=std::polar(1.,-2*std::numbers::pi/l);for(size_t j=0;j<x.size();j+=l){std::complex<double>w=1.;for(size_t k=0;k<l/2;++k){auto a=x[j+k],b=w*x[j+k+l/2];x[j+k]=a+b;x[j+k+l/2]=a-b;w*=w0;}}}
}
int main(){
 std::cout<<"factor,partial,shaped,sample_rate,chip_samples,symbol_samples,samples,rrc_width_hz,peak_pcm,outside_rrc_fraction,outside_mid_fraction,outside_wide_fraction,bandwidth99_hz,bandwidth999_hz\n";
 for(auto f:{1u,10u,100u,1000u})for(bool partial:{false,true}){ const bool shaped=true;
  modem::Config c;c.scramble=true;c.dsss_factor=f;c.sample_rate=12000;c.carrier_hz=1500;c.bandwidth_hz=3600./f;c.spreading_factor=64;c.pulse_shaping=shaped;
  for(size_t i=0;i<32;++i){c.spreading_seed[i]=3*i+7;c.dsss_seed[i]=13*i+29;}
  auto chip=modem::pattern_chip_samples(c);c.integration_seconds=(64.L*f*chip+(partial?chip/2:0))/c.sample_rate;
  modem::PatternTransmitter tx(Bytes{0,0,1},c,1789312671,0,false);
  std::vector<float> pcm(tx.total_samples());size_t done=0;while(done<pcm.size())done+=tx.read(std::span(pcm).subspan(done,std::min<size_t>(503,pcm.size()-done)));
  size_t n=1;while(n<pcm.size())n*=2;std::vector<std::complex<double>> bins(n);double peak=0;
  for(size_t i=0;i<pcm.size();++i){bins[i]=pcm[i];peak=std::max(peak,std::abs(double(pcm[i])));}fft(bins);
  double total=0,out=0,mid=0,wide=0;double bw=1.25*c.sample_rate/chip;
  std::vector<std::pair<double,double>> powers;powers.reserve(n/2+1);
  for(size_t k=0;k<=n/2;++k){double hz=double(k)*c.sample_rate/n,power=std::norm(bins[k])*(k&&k<n/2?2:1);total+=power;
   if(std::abs(hz-c.carrier_hz)>bw/2)out+=power;if(hz<300||hz>2700)mid+=power;if(hz<100||hz>2900)wide+=power;
   powers.emplace_back(std::abs(hz-c.carrier_hz),power);
  }
  std::sort(powers.begin(),powers.end());double sum=0,b99=0,b999=0;for(auto [offset,power]:powers){sum+=power;if(!b99&&sum>=.99*total)b99=2*offset;if(sum>=.999*total){b999=2*offset;break;}}
  std::cout<<f<<','<<partial<<','<<shaped<<','<<c.sample_rate<<','<<chip<<','<<modem::symbol_sample_count(c)<<','<<pcm.size()<<','<<bw<<','<<peak<<','<<out/total<<','<<mid/total<<','<<wide/total<<','<<b99<<','<<b999<<'\n';
 }
}
