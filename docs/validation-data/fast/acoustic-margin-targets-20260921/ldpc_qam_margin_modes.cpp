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
using namespace datapump; using namespace datapump::fast;
int main(int argc,char**argv) {
 if(argc!=6)return 2;
 const unsigned order=std::stoul(argv[5]); if(order!=4&&order!=16)throw Error("Experiment supports only QAM4 or QAM16");
 const unsigned bps=std::countr_zero(order);
 const std::string name=argv[1]; const auto rate=parse_code_rate(name);
 const double db=std::stod(argv[2]),n0=std::pow(10.,-db/10),sigma=std::sqrt(n0/2);
 const unsigned cycles=std::stoul(argv[3]);const auto seed=std::stoull(argv[4]);
 auto p=capacity_profile();p.channel=Channel::acoustic;p.acoustic_ofdm=true;p.constellation=order;p.code_rate=rate;p.interleave_depth=8;
 const auto k=ldpc::data_bits(rate);const auto points=square_qam_constellation(order);
 std::vector<std::size_t> rotations(ldpc::coded_bits);for(std::size_t c=0;c<rotations.size();++c)rotations[c]=datapump::fast::testing::capacity_interleave_rotation(p,c);
 std::mt19937_64 rng(seed);std::normal_distribution<double> normal;
 unsigned failed=0,nonconverged=0,undetected=0;std::uint64_t wrong=0,raw=0,iterations=0;double loss=0;
 const auto start=std::chrono::steady_clock::now();
 for(unsigned cycle=0;cycle<cycles;++cycle) {
  std::array<Bytes,8> source;Bytes bits(8*ldpc::coded_bits);
  for(unsigned frame=0;frame<8;++frame) {
   source[frame].resize(k/8);for(auto&b:source[frame])b=static_cast<std::uint8_t>(rng());
   const auto coded=ldpc::interleave(ldpc::encode(source[frame],rate));
   for(std::size_t c=0;c<coded.size();++c)bits[c*8+(frame+rotations[c])%8]=coded[c];
  }
  const auto whitening=datapump::fast::testing::capacity_whitening_mask(bits.size(),cycle);
  for(std::size_t b=0;b<bits.size();++b)bits[b]^=whitening[b];
  std::vector<float> soft(bits.size());std::array<double,4> metric{};
  for(std::size_t s=0;s<bits.size();s+=bps) {
   unsigned label=0;for(unsigned j=0;j<bps;++j)label=(label<<1)|bits[s+j];
   const auto received=points[label]+std::complex<double>(sigma*normal(rng),sigma*normal(rng));
   square_qam_soft_demodulate(order,received,std::span(metric).first(bps));
   for(unsigned j=0;j<bps;++j) {
    const double llr=std::clamp(metric[j]/n0,-24.,24.);soft[s+j]=static_cast<float>(llr);
    raw+=(llr>0)!=bool(bits[s+j]); const auto x=bits[s+j]?-llr:llr;loss+=std::max(0.,x)+std::log1p(std::exp(-std::abs(x)));
    if(whitening[s+j])soft[s+j]=-soft[s+j];
   }
  }
  std::vector<float> frame_soft(ldpc::coded_bits);
  for(unsigned frame=0;frame<8;++frame) {
   for(std::size_t c=0;c<frame_soft.size();++c)frame_soft[c]=soft[c*8+(frame+rotations[c])%8];
   const auto decoded=ldpc::decode(ldpc::deinterleave(frame_soft),rate);
   std::uint64_t errors=0;for(std::size_t b=0;b<source[frame].size();++b)errors+=std::popcount(unsigned(source[frame][b]^decoded.bytes[b]));
   wrong+=errors;failed+=errors!=0;nonconverged+=!decoded.converged;undetected+=decoded.converged&&errors;iterations+=decoded.iterations;
  }
 }
 const auto frames=8*cycles;const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
 std::cout<<std::setprecision(10)<<name<<","<<order<<","<<db<<","<<frames<<","<<seed<<","<<failed<<","<<nonconverged<<","<<undetected<<","<<wrong<<","<<double(raw)/(frames*ldpc::coded_bits)<<","<<1-loss/(std::log(2.)*frames*ldpc::coded_bits)<<","<<double(iterations)/frames<<","<<seconds<<"\n";
}
