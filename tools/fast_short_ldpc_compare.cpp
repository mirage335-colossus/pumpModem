#include "datapump/fast/ldpc.hpp"
#include "datapump/fast/codec.hpp"
#include <random>
#include <iostream>
#include <cmath>
using namespace datapump;using namespace datapump::fast;
// Reproducible ideal QPSK/AWGN comparison. This isolates inner coding; it does
// not model acoustic synchronization, outer RS, integrity or whole-file FER.
// Positive LLR means one, Es=1 per QPSK symbol and N0/2 per real coordinate.
int main(int argc,char**argv){
 const unsigned count=argc>1?std::stoul(argv[1]):256;
 if(!count||count>10000)throw Error("Comparison requires 1..10000 frames per point");
 for(unsigned n:{648U,1296U,1944U,16200U,0U})for(auto rate:{CodeRate::half,CodeRate::two_thirds,CodeRate::three_quarters}){
  if(!n&&rate==CodeRate::two_thirds)continue;
  for(double snr:{3.,4.,5.,6.}){
   std::mt19937 rng(8471);std::normal_distribution<float> normal;unsigned failures=0,errors=0,iterations=0;
   const auto bytes=n?ldpc::data_bits(rate,n)/8:rate==CodeRate::half?126U:190U;
   const double variance=1/(2*std::pow(10.,snr/10));
   for(unsigned trial=0;trial<count;++trial){
    Bytes source(bytes);for(auto&b:source)b=rng();const auto bits=n?ldpc::encode(source,rate,n):coding::encode(source,rate);
    if(n&&!ldpc::valid_codeword(bits,rate,n))throw Error("Invalid encoded word");
    std::vector<float> llr(bits.size());for(size_t i=0;i<bits.size();++i){const auto y=(bits[i]?1:-1)*std::sqrt(.5)+std::sqrt(variance)*normal(rng);llr[i]=2*std::sqrt(.5)*y/variance;errors+=(y>0)!=bits[i];}
    if(n){auto result=ldpc::decode(llr,rate,50,n);failures+=!result.converged||result.bytes!=source;iterations+=result.iterations;}
    else failures+=coding::decode(llr,bytes,rate).bytes!=source;
   }
   std::cout<<"n="<<n<<" rate="<<code_rate_name(rate)<<" EsN0="<<snr<<" failed="<<failures<<"/"<<count<<" raw_errors="<<errors<<" mean_iterations="<<double(iterations)/count<<'\n'<<std::flush;
  }
 }
}
