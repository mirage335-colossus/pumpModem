#include "datapump/fast/ldpc.hpp"
#include "datapump/fast/modem.hpp"
#include "datapump/fast/codec.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <random>
using namespace datapump;using namespace datapump::fast;
std::complex<double> point(unsigned order,unsigned label) {
 const unsigned ab=std::countr_zero(order)/2,side=1U<<ab;
 auto i=label>>ab,q=label&(side-1);
 for(unsigned s=1;s<ab;s<<=1){i^=i>>s;q^=q>>s;}
 const double scale=std::sqrt(2.*(order-1)/3.);
 return {(2.*i+1-side)/scale,(2.*q+1-side)/scale};
}
int main(int argc,char**argv){try{
 if(argc!=6)return 2;
 const auto rate=parse_code_rate(argv[1]);const unsigned order=std::stoul(argv[2]);
 const double db=std::stod(argv[3]),n0=std::pow(10.,-db/10),sigma=std::sqrt(n0/2);
 const unsigned frames=std::stoul(argv[4]);const auto seed=std::stoull(argv[5]);
 auto p=capacity_profile();p.constellation=order;p.code_rate=rate;p.interleave_depth=frames;validate(p);
 const unsigned bps=std::countr_zero(order);const auto k=ldpc::data_bits(rate);
 std::vector<Bytes> source(frames);Bytes bits(cycle_intervals(p)*physical_interval_bits);
 std::vector<unsigned> rotations(ldpc::coded_bits);
 for(std::size_t c=0;c<rotations.size();++c)rotations[c]=fast::testing::capacity_interleave_rotation(p,c);
 std::mt19937_64 rng(seed);std::normal_distribution<double> normal;
 const auto start=std::chrono::steady_clock::now();
 for(unsigned f=0;f<frames;++f){
  source[f].resize(k/8);for(auto& b:source[f])b=static_cast<std::uint8_t>(rng());
  const auto coded=ldpc::interleave(ldpc::encode(source[f],rate));
  for(std::size_t c=0;c<coded.size();++c)bits[c*frames+(f+rotations[c])%frames]=coded[c];
 }
 const auto whitening=fast::testing::capacity_whitening_mask(bits.size(),1);
 for(std::size_t b=0;b<bits.size();++b)bits[b]^=whitening[b];
 std::vector<float> soft(bits.size());std::array<double,22> metric{};
 std::uint64_t raw=0;double loss=0;
 for(std::size_t interval=0;interval<bits.size();interval+=physical_interval_bits){
  const auto end=interval+physical_interval_bits;
  for(std::size_t s=interval;s<end;s+=bps){
   unsigned label=0;for(unsigned j=0;j<bps;++j)label=(label<<1)|(s+j<end?bits[s+j]:0);
   const auto observed=point(order,label)+std::complex<double>(sigma*normal(rng),sigma*normal(rng));
   square_qam_soft_demodulate(order,observed,std::span(metric).first(bps));
   for(unsigned j=0;j<bps&&s+j<end;++j){
    const auto llr=std::clamp(metric[j]/n0,-24.,24.);soft[s+j]=static_cast<float>(llr);
    raw+=(llr>0)!=bool(bits[s+j]);const auto x=bits[s+j]?-llr:llr;
    loss+=std::max(0.,x)+std::log1p(std::exp(-std::abs(x)));
    if(whitening[s+j])soft[s+j]=-soft[s+j];
   }
  }
 }
 std::vector<float> fs(ldpc::coded_bits);unsigned failed=0,nonconverged=0,undetected=0;std::uint64_t wrong=0,iterations=0;
 for(unsigned f=0;f<frames;++f){
  for(std::size_t c=0;c<fs.size();++c)fs[c]=soft[c*frames+(f+rotations[c])%frames];
  const auto decoded=ldpc::decode(ldpc::deinterleave(fs),rate);
  std::uint64_t errors=0;for(std::size_t b=0;b<source[f].size();++b)errors+=std::popcount(unsigned(source[f][b]^decoded.bytes[b]));
  wrong+=errors;failed+=errors!=0;nonconverged+=!decoded.converged;undetected+=decoded.converged&&errors;iterations+=decoded.iterations;
 }
 std::cout<<std::setprecision(10)<<code_rate_name(rate)<<','<<order<<','<<db<<','<<frames<<','<<seed<<','<<failed<<','<<nonconverged<<','<<undetected<<','<<wrong<<','<<double(raw)/bits.size()<<','<<1-loss/(std::log(2.)*bits.size())<<','<<double(iterations)/frames<<','<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<'\n';
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
