// Standalone conditioner-throughput diagnostic; no audio device is accessed.
// The deterministic waveform and fresh per-hop phase/frequency anchors are
// preset for cost and numerical A/B comparison. They are not estimated by a
// Helper tracker, do not demonstrate qualified corrections, and do not measure
// total receiver throughput. Output/date arrays and support checks are included
// in the measured loop; signal and keyed FHSS schedule generation are excluded.
// The program processes the entire requested input then drains the resampler.
#include "datapump/helper_stream_conditioner.hpp"
#include "helper_measurement_hops.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>
using H=datapump::helper::HelperStreamConditioner;
using S=std::complex<float>;
constexpr double tau=2*std::numbers::pi;
void need(bool x,const char*m){if(!x)throw std::runtime_error(m);}
double cpu(){return static_cast<double>(std::clock())/CLOCKS_PER_SEC;}
struct R {std::vector<S> y;std::vector<long double> date;std::vector<unsigned char> support;std::size_t consumed=0,produced=0,workspace=0;double sec=0;};
R run(bool recur,unsigned hops,const std::vector<S>& input,const std::vector<double>& rf,const std::vector<double>& phases){
 constexpr unsigned fs=24000,n=9600;R r;const std::size_t capacity=static_cast<std::size_t>(hops)*n+2048;r.y.resize(capacity);r.date.resize(capacity);r.support.resize(capacity);
 H::Config cfg{fs,0,0,0,0};cfg.use_recursive_oscillator=recur;const double t=cpu();H h(cfg,{1.00005,rf[0]*.8e-6,.001});r.workspace=h.workspace_bytes();
 std::array<S,256> out{};std::array<long double,256> dates{};const std::array<std::size_t,5> chunks{91,223,17,251,73};std::size_t serial=0;
 auto process=[&](std::span<const S>x,std::uint64_t start,bool end){auto p=h.process(x,out,start,end,dates);need(p.consumed||p.produced||h.finished(),"conditioner stalled");need(r.produced+p.produced<=capacity,"result bound");
  for(std::size_t j=0;j<p.produced;++j){r.y[r.produced+j]=out[j];r.date[r.produced+j]=dates[j];r.support[r.produced+j]=static_cast<unsigned char>(h.phase_supported_at(dates[j])&&h.timing_supported_at(dates[j],p.output_first_sample+j));}
  r.produced+=p.produced;return p;};
 for(unsigned hop=0;hop<hops;++hop){const auto first=std::uint64_t(hop)*n;const double audio=1.00005+static_cast<double>(hop)*.000000001;h.update({audio,rf[hop]*.8e-6,.001});
  H::LocalPhase phase{static_cast<long double>(first+4800),phases[hop],first+480,first+9120,first+n};need(h.set_phase(phase),"phase rejected");std::size_t pos=0;
  while(pos<n){auto size=std::min(chunks[serial++%chunks.size()],static_cast<std::size_t>(n)-pos);auto p=process(std::span<const S>(input.data()+pos,size),first+pos,false);pos+=p.consumed;r.consumed+=p.consumed;}}
 while(!h.finished()){auto p=process({},static_cast<std::uint64_t>(hops)*n,true);need(p.consumed==0,"drain consumed");}
 r.sec=cpu()-t;r.y.resize(r.produced);r.date.resize(r.produced);r.support.resize(r.produced);return r;
}
int main(int argc,char**argv){try{const unsigned hops=argc>1?static_cast<unsigned>(std::stoul(argv[1])):100;need(hops>0&&hops<=100,"hops bound");
 constexpr unsigned fs=24000,n=9600;std::vector<S> in(n);for(unsigned i=0;i<n;++i){const double p=tau*std::remainder(1600.*i/fs,1.);in[i]={static_cast<float>(std::cos(p)),static_cast<float>(std::sin(p))};}
 const auto schedule=helper_measurement_hops::hf50(1710000001,true);std::vector<double> rf(hops),ph(hops);datapump::Crypto key(helper_measurement_hops::fixture_key(1710000001,true));
 auto bytes=key.stream(datapump::StreamPurpose::Fhss,1720000000,1'000'000,static_cast<std::size_t>(hops)*4,datapump::StreamDomain::FhssCycleV2);
 for(unsigned i=0;i<hops;++i){rf[i]=schedule.frequency(i);std::uint32_t x=0;for(unsigned j=0;j<4;++j)x=(x<<8)|bytes[i*4+j];ph[i]=tau*(static_cast<double>(x)/4294967296.);}
 const R a=run(false,hops,in,rf,ph),b=run(true,hops,in,rf,ph);need(a.consumed==static_cast<std::size_t>(hops)*n&&a.consumed==b.consumed&&a.produced==b.produced,"duration/output mismatch");
 double max_error=0,max_phase=0;long double max_date=0;std::size_t support_mismatch=0;double square=0;
 for(std::size_t i=0;i<a.produced;++i){const auto z=std::complex<double>(a.y[i]),w=std::complex<double>(b.y[i]);const double e=std::abs(z-w);max_error=std::max(max_error,e);square+=e*e;if(std::abs(z)>1e-5&&std::abs(w)>1e-5)max_phase=std::max(max_phase,std::abs(std::arg(w*std::conj(z))));max_date=std::max(max_date,std::abs(a.date[i]-b.date[i]));support_mismatch+=a.support[i]!=b.support[i];}
 need(max_date==0&&support_mismatch==0&&max_error<1e-6,"scalar/recursive mismatch");
 std::cout<<std::setprecision(10)<<"helper_conditioner_full_stream hops="<<hops<<" input_seconds="<<double(hops)*.4<<" input_samples="<<a.consumed<<" output_samples="<<a.produced<<" drain=completed schedule=keyed_hf50 seed=1710000001 tracking=none calibration=preset_no_probe "
  <<"scalar_process_cpu_seconds="<<a.sec<<" recursive_process_cpu_seconds="<<b.sec<<" scalar_workspace_bytes="<<a.workspace<<" recursive_workspace_bytes="<<b.workspace
  <<" max_complex_difference="<<max_error<<" rms_complex_difference="<<std::sqrt(square/a.produced)<<" max_phase_difference_radians="<<max_phase<<" max_source_date_difference_samples="<<static_cast<double>(max_date)<<" support_mismatches="<<support_mismatch<<"\n";return 0;
}catch(const std::exception&e){std::cerr<<"helper_conditioner_full_stream: "<<e.what()<<'\n';return 1;}}
