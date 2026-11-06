#include "datapump/continuous_clock.hpp"
#include "datapump/resampler.hpp"
#include <iostream>
#include <fstream>
#include <iterator>
#include <vector>
#include <cstring>
#include <algorithm>
#include <iomanip>
int main(int argc,char**argv) {
 if(argc!=5) return 1;
 const bool capture=std::string(argv[1])=="rx", long_events=std::string(argv[2])=="events-long", events=long_events||std::string(argv[2])=="events";
 constexpr unsigned fs=6000; constexpr long double epoch=1793850000;
 datapump::audio::ContinuousClockOptions opts; opts.maximum_phase_correction=std::stod(argv[3])*1e-6;
 std::ofstream map; if(std::string(argv[4])!="-") map.open(argv[4],std::ios::binary);
 std::vector<char> raw((std::istreambuf_iterator<char>(std::cin)),{}); if(raw.size()%4)return 2;
 std::vector<float> input(raw.size()/4);std::memcpy(input.data(),raw.data(),raw.size());raw.clear();
 datapump::audio::Resampler r(fs,fs,true); datapump::audio::ContinuousClock c(fs,capture,opts);
 std::uint64_t mapped=0; long double lastcoord=0;
 if(map.is_open())r.set_output_transform([&](float x,long double t,std::uint32_t){double coordinate=static_cast<double>(t*fs);map.write(reinterpret_cast<const char*>(&coordinate),8);++mapped;lastcoord=t*fs;return x;});
 std::vector<float> out(300);std::size_t consumed=0,produced=0;double peakrate=0,peakerror=0;std::uint64_t steps=0;
 while(!r.finished()) {
  const long double native=capture?r.source_seconds()*fs:produced;
  const long double logical=capture?static_cast<long double>(produced)/fs:r.source_seconds();
  const long double time=native/fs;long double offset=0;
  const long double event_time=long_events?std::fmod(time,180.L):time;
  if(events) { if(!capture) {if(event_time>=45)offset+=.1L;if(event_time>=55)offset+=2;if(event_time>=75)offset-=2;if(event_time>=85)offset-=.1L;}
    else {if(event_time>=48)offset-=.1L;if(event_time>=60)offset-=2;if(event_time>=80)offset+=2;if(event_time>=95)offset+=.1L;} }
  if(long_events)offset+=capture?-.04L*std::floor(time/150.L):.05L*std::floor(time/120.L);
  c.observe({native,epoch+time+offset,1000+time,.00001,.000001,datapump::audio::TimingQuality::estimated,1});
  auto q=c.update(native,logical,r.rate_correction());r.set_rate_correction(q.target_correction,opts.maximum_slew_per_second);
  peakrate=std::max(peakrate,std::abs(q.applied_correction));peakerror=std::max(peakerror,std::abs(q.phase_error_seconds));steps=q.clock_steps;
  auto progress=r.process(std::span(input).subspan(consumed),out,true);consumed+=progress.consumed;produced+=progress.produced;
  std::cout.write(reinterpret_cast<const char*>(out.data()),static_cast<std::streamsize>(progress.produced*4));
  if(!progress.consumed&&!progress.produced&&!r.finished())return 3;
 }
 std::cerr<<std::setprecision(17)<<"{\"capture\":"<<(capture?"true":"false")<<",\"events\":"<<(events?"true":"false")<<",\"phase_cap_ppm\":"<<opts.maximum_phase_correction*1e6<<",\"input_samples\":"<<input.size()<<",\"output_samples\":"<<produced<<",\"mapped_samples\":"<<mapped<<",\"final_map_advance_seconds\":"<<(mapped?std::to_string(static_cast<double>((lastcoord-(produced-1))/fs)):"null")<<",\"peak_applied_ppm\":"<<peakrate*1e6<<",\"peak_error_seconds\":"<<peakerror<<",\"steps\":"<<steps<<"}\n";
}
