#include "datapump/fast/preset.hpp"
#include "datapump/fast/codec.hpp"
#include "datapump/fast/modem.hpp"
#include <cmath>
#include <iomanip>
#include <iostream>
namespace datapump::fast{SnrPreset capped_resolve_snr_preset(Channel,double);}
using namespace datapump::fast;
double gap(const Profile& p){return p.acoustic_ofdm?(total_interval_symbols(p,cycle_intervals(p))+1.)*(p.ofdm_fft_size+p.ofdm_prefix_samples)/p.sample_rate:0;}
int main(){
 std::cout<<"snr,old_waveform,old_qam,old_ldpc,old_depth,new_waveform,new_qam,new_ldpc,new_depth,old_gap_s,new_gap_s,old_50MB_bps,new_50MB_bps,throughput_loss_percent,new_bandwidth_hz,new_60B_s\n";
 double last=1e100;
 for(double snr:expected_snr_options(Channel::acoustic)){
 const auto old=resolve_snr_preset(Channel::acoustic,snr).profile,p=capped_resolve_snr_preset(Channel::acoustic,snr).profile;
 const auto oe=estimate_transmission(old,false,50000000),ne=estimate_transmission(p,false,50000000),small=estimate_transmission(p,false,60);
 std::cout<<std::setprecision(12)<<snr<<","<<(old.acoustic_ofdm?"OFDM":"SC")<<","<<old.constellation<<","<<code_rate_name(old.code_rate)<<","<<old.interleave_depth<<","<<(p.acoustic_ofdm?"OFDM":"SC")<<","<<p.constellation<<","<<code_rate_name(p.code_rate)<<","<<p.interleave_depth<<","<<gap(old)<<","<<gap(p)<<","<<oe.source_bps<<","<<ne.source_bps<<","<<100*(1-ne.source_bps/oe.source_bps)<<","<<occupied_bandwidth_hz(p)<<","<<small.seconds<<"\n";
 if(ne.source_bps>last+1e-6)std::cerr<<"MENU NONMONOTONIC at "<<snr<<"\n";
 last=ne.source_bps;
 }
 double previous=1e100;
 for(int tenth=130;tenth>=-270;--tenth){const auto snr=tenth/10.; const auto p=capped_resolve_snr_preset(Channel::acoustic,snr).profile;const auto rate=estimate_transmission(p,false,50000000).source_bps;
 if(rate>previous+1e-6)std::cerr<<"CONTINUOUS NONMONOTONIC at "<<snr<<" prev="<<previous<<" rate="<<rate<<" depth="<<p.interleave_depth<<"\n";previous=rate;}
}
