#include "datapump/fast/codec.hpp"
#include "datapump/fast/modem.hpp"
#include "datapump/fast/preset.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <optional>
#include <string>
#include <vector>
using namespace datapump;using namespace datapump::fast;
int main(int argc,char** argv) {try {
 const std::string mode=argc>1?argv[1]:"clean";
 const auto p=resolve_snr_preset(Channel::acoustic,-10).profile;
 auto tp=p;if(mode=="clock"){tp.symbol_rate*=1.00008;tp.carrier_hz+=.03;}
 const Bytes bytes{'a','c','o','u','s','t','i','c',' ','p','r','e','s','e','t'};
 const Crypto crypto(Bytes(32,0x23));
 auto encoder=datapump::fast::testing::deterministic_encoder(p,crypto,byte_source(bytes),5583);
 StreamDecoder decoder(p,crypto);
 Transmitter tx(tp,[&](std::span<std::uint8_t> bits){return encoder.next_interval(bits);});
 std::uint64_t sample=0,first_acquired=0,completed_at=0;std::size_t intervals=0;
 Receiver rx(p,[&](std::span<const float> bits){++intervals;decoder.push_interval(bits);});
 std::array<float,4093> block{};std::vector<float> echo(800,0);std::size_t echo_at=0;
 const auto first_pilot=preamble_symbols(p)+sync_symbols+p.pilot_spacing_symbols;
 const auto fade_middle=(first_pilot+2+std::ceil(6.4/p.rolloff))*p.sample_rate/p.symbol_rate;
 const auto fade_half=5*p.sample_rate/p.symbol_rate;
 bool premature=false;
 while(!tx.finished()) {
  const auto n=tx.read(block);
  for(std::size_t k=0;k<n;++k) {
   const auto position=sample+k;
   if(mode=="fade"&&std::abs(double(position)-fade_middle)<fade_half)block[k]=0;
   if(mode=="phase"&&double(position)>fade_middle-fade_half)block[k]=-block[k];
   if(mode=="echo") {const auto original=block[k];block[k]+=.45F*echo[echo_at];echo[echo_at]=original;if(++echo_at==echo.size())echo_at=0;}
  }
  rx.push(std::span<const float>(block).first(n));sample+=n;
  if(rx.progress().acquired&&!first_acquired)first_acquired=sample;
  if(rx.progress().physical_complete){premature=true;completed_at=sample;break;}
 }
 if(!premature) {
  block.fill(0);auto tail=end_silence_samples(p);
  while(tail) {const auto n=std::min<std::uint64_t>(tail,block.size());rx.push(std::span<const float>(block).first(n));tail-=n;sample+=n;}
  completed_at=sample;
 }
 rx.finish();decoder.finish(rx.progress().physical_complete);const auto s=decoder.snapshot();
 std::cout<<"mode="<<mode<<" baud="<<p.symbol_rate<<" marker_spacing="<<p.marker_spacing_intervals<<" pilot_spacing="<<p.pilot_spacing_symbols<<" estimated_seconds="<<estimate_transmission(p,true,bytes.size()).seconds<<" fade_middle_seconds="<<fade_middle/p.sample_rate<<" acquired_seconds="<<double(first_acquired)/p.sample_rate<<" finish_seconds="<<double(completed_at)/p.sample_rate<<" premature="<<premature<<" physical_end="<<rx.progress().physical_complete<<" intervals="<<intervals<<" complete="<<s.complete<<" ldpc_failed="<<s.ldpc_failed_frames<<" evm="<<rx.progress().evm<<" clock_ppm="<<rx.progress().clock_error_ppm<<" cfo="<<rx.progress().carrier_error_hz<<" status="<<s.status<<'\n';
 return s.complete&&decoder.result()&&std::equal(bytes.begin(),bytes.end(),decoder.result()->bytes().begin(),decoder.result()->bytes().end())?0:1;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
