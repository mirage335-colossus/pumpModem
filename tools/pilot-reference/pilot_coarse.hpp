#pragma once
#include "pilot_reference.hpp"
#include <algorithm>
#include <array>
#include <complex>
#include <vector>
namespace pilot_reference {
struct SeedResult {
 bool valid=false;std::uint64_t first=0,end=0,center=0;double fraction=0;
 double hz=0,uncertainty=0,noise_psd=0,peak_noise=0,peak_competitor=0;
 double low=0,high=0,bin_width=0;std::size_t bin=0,size=0;
};
// Nonoverlapping chronological apertures, each one small radix2 transform.
// A single raw buffer is overwritten, never searched/replayed historically. The whole-bin interval
// is conditional engineering geometry, NOT a confidence probability.
class CoarseFFT {
 std::uint32_t fs_;double low_,high_;std::uint64_t first_,next_;std::size_t used_=0;
 std::vector<std::complex<float>> values_;
public:
 SeedResult result;bool finished=false;std::size_t transforms=0,butterflies=0;
 CoarseFFT(std::uint32_t fs,double expected,double half_width,std::uint64_t first):fs_(fs),low_(std::max(200.,expected-half_width)),high_(std::min(fs/2.-200.,expected+half_width)),first_(first),next_(first){
  const bool supported_rate=fs==12000||fs==24000||fs==48000||fs==96000||fs==192000;
  if(!supported_rate || !std::isfinite(expected) || !(half_width>=0) || !std::isfinite(half_width) || !(low_<=high_))
   throw std::runtime_error("unsupported pilot FFT rate or guarded positive capture");
  std::size_t n=1;while(n<fs*2.7)n*=2;values_.resize(n);
 }
 std::size_t workspace_bytes()const{return values_.size()*sizeof(values_[0]);}
 void restart(){if(!finished)throw std::runtime_error("unfinished current FFT aperture");first_=next_;used_=0;finished=false;result={};}
 std::size_t push(std::span<const float> pcm,std::uint64_t first){
  if(first!=next_||finished||pcm.size()>std::numeric_limits<std::uint64_t>::max()-next_)throw std::runtime_error("pilot FFT noncontiguous or repeated input");
  const auto count=std::min(pcm.size(),values_.size()-used_);for(std::size_t i=0;i<count;++i)values_[used_++]={pcm[i],0};next_+=count;
  if(used_==values_.size())finish();return count;
 }
private:
 void finish(){
  const auto n=values_.size();double square_sum=0;
  for(std::size_t i=0;i<n;++i){const auto w=.5-.5*std::cos(tau*i/(n-1));values_[i]*=static_cast<float>(w);square_sum+=w*w;}
  // Same bounded iterative radix2 construction used by the current helper's
  // pilot FFT, enlarged only for this single~2.7s experimental aperture.
  for(std::size_t i=1,j=0;i<n;++i){auto bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(values_[i],values_[j]);}
  for(std::size_t length=2;length<=n;length*=2){const auto step=std::polar(1.F,static_cast<float>(-tau/length));
   for(std::size_t first=0;first<n;first+=length){std::complex<float> w{1,0};for(std::size_t i=0;i<length/2;++i){const auto u=values_[first+i],v=values_[first+i+length/2]*w;values_[first+i]=u+v;values_[first+i+length/2]=u-v;w*=step;}}}
  result.first=first_;result.end=next_;result.center=first_+(n-1)/2;result.fraction=.5*((n-1)%2);result.size=n;result.bin_width=static_cast<double>(fs_)/n;result.low=low_;result.high=high_;
  const auto lo=std::max<std::size_t>(1,std::ceil(low_/result.bin_width)),hi=std::min<std::size_t>(n/2-1,std::floor(high_/result.bin_width));
  std::size_t peak=lo;double peak_power=0;for(auto i=lo;i<=hi;++i){const auto p=static_cast<double>(std::norm(values_[i]));if(p>peak_power){peak_power=p;peak=i;}}
  std::array<double,128> background{};const auto physical_lo=static_cast<std::size_t>(std::ceil(200/result.bin_width)),physical_hi=static_cast<std::size_t>(std::floor((fs_/2.-200)/result.bin_width));
  for(std::size_t i=0;i<background.size();++i){const auto bin=physical_lo+(physical_hi-physical_lo)*(2*i+1)/(2*background.size());background[i]=std::norm(values_[bin]);}
  std::nth_element(background.begin(),background.begin()+background.size()/2,background.end());const auto mean_noise=background[background.size()/2]/std::log(2.);
  double competitor=0;for(auto i=lo;i<=hi;++i)if(i+3<peak||i>peak+3)competitor=std::max(competitor,static_cast<double>(std::norm(values_[i])));
  result.bin=peak;result.hz=peak*result.bin_width;result.uncertainty=result.bin_width;
  result.noise_psd=2*mean_noise/(square_sum*fs_);result.peak_noise=peak_power/std::max(mean_noise,1e-30);result.peak_competitor=peak_power/std::max(competitor,1e-30);
  // Only correct-lobe qualification is attempted: strong central peak over
  // measured background AND independently competing, separate spectral lobes.
  result.valid=mean_noise>0&&result.peak_noise>=100&&result.peak_competitor>=4;finished=true;++transforms;for(std::size_t k=n;k>1;k/=2)butterflies+=n/2;
 }
};
}
