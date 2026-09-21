#include "datapump/fast/preset.hpp"
#include "datapump/fast/codec.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
using namespace datapump::fast;
int main(){
 unsigned failures=0;
 for(auto c:{Channel::wire,Channel::acoustic,Channel::ssb,Channel::fm}){
  const auto nominal=default_expected_snr(c);
  double last=std::numeric_limits<double>::infinity();
  for(int step=0;step<=400;++step){
   const auto snr=nominal-.1*step;
   try{
    const auto preset=resolve_snr_preset(c,snr);const auto& p=preset.profile;validate(p);
    const auto bps=estimate_transmission(p,false,50000000).source_bps;
    if(bps>last*(1+1e-9)){std::cerr<<"NONMONOTONE "<<channel_name(c)<<" "<<snr<<" "<<last<<" -> "<<bps<<'\n';++failures;}
    last=bps;
    if(!(p.symbol_rate>=1&&occupied_bandwidth_hz(p)<=preset.reference_bandwidth_hz+1e-6)) {std::cerr<<"BAND/RATE "<<channel_name(c)<<" "<<snr<<'\n';++failures;}
    if(step==0&&profile_id(p)!=profile_id(profile(c))) {std::cerr<<"DEFAULT "<<channel_name(c)<<'\n';++failures;}
   }catch(const std::exception& e){std::cerr<<"ERROR "<<channel_name(c)<<" "<<snr<<" "<<e.what()<<'\n';++failures;}
  }
  for(double snr:expected_snr_options(c)){
   const auto preset=resolve_snr_preset(c,snr);const auto& p=preset.profile;
   std::cout<<channel_name(c)<<','<<snr<<','<<(p.acoustic_ofdm?"ofdm":"sc")<<','<<p.constellation<<','<<code_rate_name(p.code_rate)<<','<<p.symbol_rate<<','<<occupied_bandwidth_hz(p)<<','<<p.interleave_depth<<','<<estimate_transmission(p,false,50000000).source_bps<<'\n';
   std::set<std::string> ids;
   for(const auto& option:symbol_rate_options(p)){
    if(!ids.insert(option.id).second){std::cerr<<"DUPLICATE "<<option.id<<'\n';++failures;}
    const auto q=apply_symbol_rate_option(p,option.id);validate(q);
    if(option.id!="auto"&&symbol_rate_option_id(q)!=option.id){std::cerr<<"BADID "<<option.id<<'\n';++failures;}
   }
  }
  for(double bad:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),nominal+.001,nominal-40.001}){
   bool rejected=false;try{(void)resolve_snr_preset(c,bad);}catch(const std::exception&){rejected=true;}
   if(!rejected){std::cerr<<"BADINPUT "<<channel_name(c)<<'\n';++failures;}
  }
 }
 std::cerr<<"failures="<<failures<<'\n';return failures?1:0;
}
