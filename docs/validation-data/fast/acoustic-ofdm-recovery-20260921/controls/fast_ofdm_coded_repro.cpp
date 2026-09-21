#include "datapump/fast/preset.hpp"
#include "datapump/fast/codec.hpp"
#include "datapump/fast/modem.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>
using namespace datapump;
using namespace datapump::fast;
int main(int argc,char**argv){try{
 double expected=argc>1?std::atof(argv[1]):0,echo=argc>2?std::atof(argv[2]):.5;
 auto p=resolve_snr_preset(Channel::acoustic,expected).profile;
 const auto old_estimate=estimate_transmission(p,true,50000000);
 if(argc>3)p.interleave_depth=std::atoi(argv[3]);
 const auto new_estimate=estimate_transmission(p,true,50000000);
 std::cout<<"estimate old_bps="<<old_estimate.source_bps<<" new_bps="<<new_estimate.source_bps<<" throughput_loss_percent="<<100*(1-new_estimate.source_bps/old_estimate.source_bps)<<std::endl;
 Bytes original(argc>4?std::atoi(argv[4]):60);for(std::size_t i=0;i<original.size();++i)original[i]=i*19;original.back()=0;
 Crypto key(Bytes(32,0x37));auto encoder=datapump::fast::testing::deterministic_encoder(p,key,byte_source(original),947311);
 Bytes source,interval(physical_interval_bits);while(encoder.next_interval(interval))source.insert(source.end(),interval.begin(),interval.end());
 std::size_t sent=0;Transmitter tx(p,[&](std::span<std::uint8_t>b){if(sent==source.size())return false;std::copy_n(source.begin()+sent,b.size(),b.begin());sent+=b.size();return true;});
 std::vector<float> pcm;std::array<float,4096> buf{};while(!tx.finished()){auto n=tx.read(buf);pcm.insert(pcm.end(),buf.begin(),buf.begin()+n);}
 const auto fs=p.sample_rate;const double sigma=std::sqrt(std::pow(p.amplitude/4.5,2)*fs/(2*17500*std::pow(10.,expected/10)));
 std::mt19937 random(133831);std::normal_distribution<float>noise(0,sigma);
 StreamDecoder decoder(p,key);std::uint64_t delivered=0,errors=0,erased=0;
 Receiver rx(p,[&](std::span<const float>b){for(auto v:b){if(delivered<source.size()){errors+=v!=0&&((v>0)!=bool(source[delivered]));erased+=v==0;}++delivered;}decoder.push_interval(b);});
 double acquired=-1,finished=-1;auto end=pcm.size()+end_silence_samples(p);
 for(std::size_t at=0;at<end&&!rx.progress().physical_complete;){const auto n=std::min<std::size_t>(buf.size(),end-at);for(std::size_t j=0;j<n;++j){const auto pos=at+j;double v=pos<pcm.size()?pcm[pos]:0;if(pos>=20*fs&&pos>=240&&pos-240<pcm.size())v+=echo*pcm[pos-240];buf[j]=v+noise(random);}rx.push(std::span<const float>(buf).first(n));at+=n;if(acquired<0&&rx.progress().acquired)acquired=double(at)/fs;if(rx.progress().physical_complete)finished=double(at)/fs;}
 decoder.finish(rx.progress().physical_complete);const auto s=decoder.snapshot();bool exact=decoder.result()&&std::equal(original.begin(),original.end(),decoder.result()->bytes().begin(),decoder.result()->bytes().end());
 std::cout<<"expected="<<expected<<" echo="<<echo<<" qam="<<p.constellation<<" code="<<code_rate_name(p.code_rate)<<" D="<<p.interleave_depth<<" TX_seconds="<<double(pcm.size())/fs<<" source_intervals="<<source.size()/physical_interval_bits<<" acquired="<<acquired<<" completed="<<finished<<" premature="<<(finished>=0&&finished<double(pcm.size())/fs)<<" RX_intervals="<<delivered/physical_interval_bits<<" raw_errors="<<errors<<" raw_erasures="<<erased<<" exact="<<exact<<" ldpc_frames="<<s.ldpc_frames<<" ldpc_failed="<<s.ldpc_failed_frames<<" failed_cycles="<<s.failed_cycles<<" status="<<s.status<<std::endl;
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
