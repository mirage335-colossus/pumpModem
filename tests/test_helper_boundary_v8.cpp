// Receiver-only bounded change-point feasibility. Actual Helper waveform,
// independent RF hop phase, and explicit thermal noise come from the fixture.
// Predictions below are fixed conditional oscillator assumptions, not truth
// inputs. This experiment supplies no corrections to any receiver.
#include "helper_live_probe_fixture.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace hp=helper_live_probe;
using C=std::complex<double>;
constexpr double fs=hp::sample_rate,tau=hp::tau;
constexpr int half=1440, grid=33, stride=12; // +-60ms, +-8ms in .5ms steps
using Prefix=std::array<C,2*half+1>;
double correlation_cpu=0,search_cpu=0;
struct Boundary{ std::array<Prefix,4> prefix{}; };
std::vector<Boundary> observations(hp::Config c,int count,bool noise_only) {
 hp::Source src(c); auto prev=src.generate(0); std::vector<Boundary> ret;
 hp::h::Options options;options.power_w=c.helper_w;options.data_power_w=1;options.callsign=c.callsign;
 hp::h::Generator gen(options,hp::h::Plan{2400,.4,0,c.helper_w,950,550,true},c.epoch,true);
 auto remove_signal=[&](hp::Dwell& d){if(!noise_only)return;for(unsigned i=0;i<hp::dwell_samples;++i){const double t=d.first_seconds+double(i)/fs; const auto helper=gen.mix_analytic({},src.tx_time(t),static_cast<unsigned>(fs),40.L,0.L)/(.4*hp::h::pilot_amplitude_fraction);const C z=C(d.samples[i])-helper*C(d.common_phasor[i]);d.samples[i]={float(z.real()),float(z.imag())};}};
 remove_signal(prev);
 for(int j=1;j<=count;++j){auto next=src.generate(j);remove_signal(next);Boundary o; const auto work=std::clock();
  for(int i=0;i<2*half;++i){const int n=i-half;const auto z=n<0?prev.samples[hp::dwell_samples+n]:next.samples[n];
   for(int side=0;side<2;++side)for(int p=0;p<2;++p){
    const double rf=side?next.shift_hz:prev.shift_hz;
    // Fixed local prediction, with receiver-supplied schedule RF; the caller
    // supplies 50 ppm audio rate and 0.8 ppm RF as conditional assumptions.
    const double f=(p?550.:2400.)*1.00005+rf*.8e-6;
    o.prefix[side*2+p][i+1]=o.prefix[side*2+p][i]+C(z)*std::polar(1.,-tau*f*n/fs);
   }
  }correlation_cpu+=double(std::clock()-work)/CLOCKS_PER_SEC;ret.push_back(std::move(o));prev=std::move(next);
 }return ret;
}
std::pair<double,double> estimate(const std::vector<Boundary>& obs,double center){
 std::array<double,grid> score{};
 for(const auto& b:obs)for(int k=0;k<grid;++k){const int i=half+int(std::llround(center*fs))+(k-16)*stride;
   // Profile independent gains per pilot and per side. Cumulative overlapping
   // apertures are evaluated as competing hypotheses, not independent data.
   // Ignore the 4ms ramp on both sides, plus 2ms leakage/retune margin.
   constexpr int guard=144; const int left=i-guard,right=i+guard;
   for(int p=0;p<2;++p){const auto a=b.prefix[p][left], z=b.prefix[2+p][2*half]-b.prefix[2+p][right];
    score[k]+=std::norm(a)/left+std::norm(z)/(2*half-right);}
 }
 const auto i=std::max_element(score.begin(),score.end())-score.begin();return {center+(static_cast<double>(i)-16)*.0005,score[i]-score[16]};
}
int main(int argc,char**argv){try{const int count=argc>1?std::stoi(argv[1]):5;const int seeds=argc>2?std::stoi(argv[2]):8;
 if(count<1||count>20||seeds<1||seeds>32)throw std::invalid_argument("bounded count/seeds");
 for(double loss:{180.,160.})for(double arrival:{.00021,.00321,.0078})for(bool noise_only:{false,true}){
  double square=0,bias=0,maxerr=0,base=0; correlation_cpu=0;search_cpu=0; unsigned qualified=0;const auto start=std::clock();
  for(int s=0;s<seeds;++s){hp::Config c;c.path_loss_db=loss;c.arrival_offset_seconds=arrival;c.seed=1724100001+s;c.expanded_schedule_key=true;c.noise_seed=1734100001+s;
   const auto x=observations(c,count,noise_only);const auto tick=std::clock();const auto [est,gain]=estimate(x,.004);search_cpu+=double(std::clock()-tick)/CLOCKS_PER_SEC;
   const double high=hp::high_pilot_fraction(c);const double n0=10*std::log10(1.380649e-23*c.temperature_kelvin*1000)+c.noise_figure_db;
   const double cn0=10*std::log10(c.helper_w*1000*high)-loss-n0;const double noise_power=fs/std::pow(10.,cn0/10);
   if(gain/noise_power>12)++qualified;
   // Clock scale and offset are evaluator-only here. Each boundary's true
   // receiver date changes slightly with the TX/RX sample clock ratio.
   const double mid=(count+1)*.2;const double truth=arrival-(1.000037/0.999987-1)*mid;
   const double e=est-truth;square+=e*e;bias+=e;maxerr=std::max(maxerr,std::abs(e));base+=(truth-.004)*(truth-.004);
  }
  std::cout<<std::setprecision(8)<<"boundary_v8 mode=two_pilot_independent_complex_gain count="<<count<<" seeds="<<seeds<<" loss_db="<<loss<<" arrival_ms="<<arrival*1000<<" prior_center_ms=4 noise_only="<<noise_only<<" heuristic_gain_gt12_count="<<qualified<<" rms_error_ms="<<1000*std::sqrt(square/seeds)<<" nominal_rms_ms="<<1000*std::sqrt(base/seeds)<<" bias_ms="<<1000*bias/seeds<<" max_error_ms="<<1000*maxerr<<" correlation_cpu_seconds="<<correlation_cpu<<" search_cpu_seconds="<<search_cpu<<" process_cpu_seconds="<<double(std::clock()-start)/CLOCKS_PER_SEC<<"\n";
 }
 return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
