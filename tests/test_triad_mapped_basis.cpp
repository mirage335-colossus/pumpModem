#include "triad_crypto_fixture.hpp"
#include "datapump/triad_modem_v3.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace v=datapump::triad::v3;
namespace {
constexpr long double tau=2*std::numbers::pi_v<long double>;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F work){bool rejected=false;try{work();}catch(const datapump::Error&){rejected=true;}check(rejected,"invalid mapped basis accepted");}
datapump::Crypto key(){std::array<std::uint8_t,32> bytes{};for(std::size_t i=0;i<bytes.size();++i)bytes[i]=static_cast<std::uint8_t>(17*i+3);return triad_test_key(bytes);}
v::Format format(){v::Format f;f.sample_rate=6000;f.inner_rate_hz=36;f.added_symbols=10;f.bit_seconds=32;return f;}

// Frozen operation ordering from the pre-mapped shaped_basis. This checks the
// shared-row refactor against the actual old arithmetic, including pulse edges.
void previous_uniform(v::PatternSource& source,const v::Candidate& candidate,double fs,
                      std::uint64_t first,std::size_t rows,std::span<v::Complex> output){
 const auto rate=1+candidate.clock_ppm*1e-6;const auto source_rate=v::effective_sample_rate(source.format());
 for(std::size_t row=0;row<rows;++row){const auto index=first+row;const auto time=static_cast<long double>(index)/fs;
  const auto carrier_phase=std::remainder(tau*(candidate.frequency_hz*time+.5L*candidate.frequency_drift_hz_per_second*time*time),tau);
  const auto common=std::polar(1.,static_cast<double>(carrier_phase));
  for(std::size_t tap=0;tap<candidate.channel.taps.size();++tap){const auto& path=candidate.channel.taps[tap];
   const auto receiver_coordinate=static_cast<long double>(index)-static_cast<long double>(candidate.arrival_seconds+path.delay_seconds)*fs;
   const auto coordinate=receiver_coordinate*rate*(static_cast<long double>(source_rate)/fs);
   const auto phase=std::remainder(tau*path.doppler_hz*time,tau);
   const auto value=source.sample_at(coordinate)*common*std::polar(1.,static_cast<double>(phase));
   const auto column=tap*(candidate.channel.linear_gain?2:1);output[column*rows+row]=value;
   if(candidate.channel.linear_gain){const auto gain=(static_cast<double>(time)-candidate.channel.gain_reference_seconds)/candidate.channel.gain_scale_seconds;output[(column+1)*rows+row]=value*gain;}
  }
 }
}

void uniform_and_chunks(const datapump::Crypto& crypto){
 for(unsigned mode=0;mode<3;++mode){
  auto f=format();if(mode==1){f.fhss=true;f.quiet_seconds=.02;}if(mode==2){f.burst_wait_tiles=1;f.bit_seconds=64;}
  v::PatternSource source(f,crypto,1);v::Candidate c;c.source_bit=1;c.clock_ppm=mode?127.25:0;
  c.frequency_hz=.73;c.frequency_drift_hz_per_second=.021;c.arrival_seconds=.13;
  c.channel.taps={{0,.2},{.007,-.4}};c.channel.linear_gain=true;c.channel.gain_scale_seconds=3;
  constexpr std::size_t rows=911;std::vector<double> offsets(rows);std::iota(offsets.begin(),offsets.end(),0.);
  for(auto first:{std::uint64_t{0},source.geometry().calendar_origin_sample,std::uint64_t{24001}}){
   std::vector<v::Complex> old(4*rows),plain(old.size()),mapped(old.size()),parts(old.size());
   previous_uniform(source,c,f.sample_rate,first,rows,old);v::shaped_basis(source,c,f.sample_rate,first,rows,plain);
   v::shaped_basis_mapped(source,c,f.sample_rate,first,offsets,mapped);
   check(std::memcmp(old.data(),plain.data(),old.size()*sizeof(v::Complex))==0,"legacy shaped basis arithmetic changed");
   check(std::memcmp(old.data(),mapped.data(),old.size()*sizeof(v::Complex))==0,"uniform mapped basis changed legacy samples");
   constexpr std::size_t cut=337;
   std::vector<v::Complex> a(4*cut),b(4*(rows-cut));
   std::vector<double> second(rows-cut);std::iota(second.begin(),second.end(),0.);
   v::shaped_basis_mapped(source,c,f.sample_rate,first,std::span(offsets).first(cut),a);
   v::shaped_basis_mapped(source,c,f.sample_rate,first+cut,second,b);
   for(std::size_t column=0;column<4;++column){std::copy_n(a.begin()+static_cast<std::ptrdiff_t>(column*cut),cut,parts.begin()+static_cast<std::ptrdiff_t>(column*rows));std::copy_n(b.begin()+static_cast<std::ptrdiff_t>(column*(rows-cut)),rows-cut,parts.begin()+static_cast<std::ptrdiff_t>(column*rows+cut));}
   check(std::memcmp(mapped.data(),parts.data(),mapped.size()*sizeof(v::Complex))==0,"mapped basis changed across chunks");
  }
 }
}

void nonlinear_audio_truth(const datapump::Crypto& crypto){
 auto f=format();v::PatternSource source(f,crypto,0);v::Candidate c;
 c.arrival_seconds=.17;c.clock_ppm=200.75;c.frequency_hz=1.25;c.frequency_drift_hz_per_second=.03;
 c.channel.taps={{.007,2.5},{.002,-.3}};
 constexpr std::size_t rows=701;constexpr std::uint64_t first=6007;constexpr double center=1725;
 std::vector<double> offsets(rows);for(std::size_t i=0;i<rows;++i)offsets[i]=static_cast<double>(i)+.17+.03*std::sin(.01*static_cast<double>(i));
 std::vector<v::Complex> output(2*rows),without_center(2*rows);
 v::shaped_basis_mapped(source,c,f.sample_rate,first,offsets,output,center);
 v::shaped_basis_mapped(source,c,f.sample_rate,first,offsets,without_center);
 long double energy=0,error=0,omission=0;
 const auto rate=1+c.clock_ppm*1e-6;
 for(std::size_t tap=0;tap<2;++tap)for(std::size_t i=0;i<rows;++i){
  const auto t=(static_cast<long double>(first)+offsets[i])/f.sample_rate;
  const auto u=(t-static_cast<long double>(c.arrival_seconds+c.channel.taps[tap].delay_seconds))*rate;
  // Independently compose the actual audio center at emitted time, then
  // downmix at the mapped physical observation time, not nominal output i/Fs.
  const auto emitted=source.sample_at(u*f.sample_rate)*std::polar(1.,static_cast<double>(std::remainder(tau*center*u,tau)));
  const auto mixed=emitted*std::polar(1.,static_cast<double>(std::remainder(-tau*center*t,tau)));
  const auto channel_phase=tau*((c.frequency_hz+c.channel.taps[tap].doppler_hz)*t+.5L*c.frequency_drift_hz_per_second*t*t);
  const auto expected=mixed*std::polar(1.,static_cast<double>(std::remainder(channel_phase,tau)));
  const auto n=tap*rows+i;energy+=std::norm(expected);error+=std::norm(expected-output[n]);omission+=std::norm(expected-without_center[n]);
 }
 check(energy>0&&error<1e-20L*energy,"mapped full-audio basis differs from independently emitted/mixed waveform");
 check(omission>.1L*energy,"test did not discriminate the missing audio-center correction");
 std::cout<<"mapped full-audio relative squared error="<<static_cast<double>(error/energy)<<'\n';
}

void huge_origin_and_validation(const datapump::Crypto& crypto){
 auto f=format();v::PatternSource source(f,crypto,1);v::Candidate c;
 // Integer arrival seconds permit a nonzero pattern at an observation origin
 // beyond binary64's exact integer range without allocating the long capture.
 const auto first=((std::uint64_t{1}<<54)/f.sample_rate)*f.sample_rate;
 c.arrival_seconds=static_cast<double>(first/f.sample_rate);
 std::vector<double> offsets(65);for(std::size_t i=0;i<offsets.size();++i)offsets[i]=static_cast<double>(source.geometry().calendar_origin_sample+450+i)+.125;
 std::vector<v::Complex> output(offsets.size()),reference(offsets.size());
 v::shaped_basis_mapped(source,c,f.sample_rate,first,offsets,output,1725);
 for(std::size_t i=0;i<offsets.size();++i)reference[i]=source.sample_at(offsets[i]);
 check(std::memcmp(output.data(),reference.data(),output.size()*sizeof(v::Complex))==0,"large integer mapped origin lost fractional samples or integral center phase");
 auto invalid=offsets;invalid[1]=invalid[0];rejects([&]{v::shaped_basis_mapped(source,c,f.sample_rate,first,invalid,output);});
 invalid=offsets;invalid[2]=std::numeric_limits<double>::infinity();rejects([&]{v::shaped_basis_mapped(source,c,f.sample_rate,first,invalid,output);});
 invalid=offsets;invalid[0]=-1;rejects([&]{v::shaped_basis_mapped(source,c,f.sample_rate,first,invalid,output);});
 invalid=offsets;invalid.back()=1048577;rejects([&]{v::shaped_basis_mapped(source,c,f.sample_rate,first,invalid,output);});
 rejects([&]{v::shaped_basis_mapped(source,c,f.sample_rate,UINT64_MAX-1,offsets,output);});
 rejects([&]{v::shaped_basis_mapped(source,c,f.sample_rate,first,offsets,output,std::numeric_limits<double>::quiet_NaN());});
 rejects([&]{v::shaped_basis_mapped(source,c,f.sample_rate,first,offsets,std::span(output).first(2));});
 auto invalid_c=c;invalid_c.clock_ppm=-1e6;rejects([&]{v::shaped_basis_mapped(source,invalid_c,f.sample_rate,first,offsets,output);});
}
}
int main(){try{const auto crypto=key();uniform_and_chunks(crypto);nonlinear_audio_truth(crypto);huge_origin_and_validation(crypto);std::cout<<"mapped triad basis checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
