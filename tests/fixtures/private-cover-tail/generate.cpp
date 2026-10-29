#include "datapump/channel.hpp"
#include "datapump/transfer.hpp"
#include "datapump/tuning.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <vector>
using namespace datapump;
namespace {
void check(bool value,const std::string& why) {if(!value)throw Error(why);}
transfer::Options options() {
    transfer::Options o;o.timestamp=1792888486;o.content_limit=1024*1024;
    o.dsp_workspace_bytes=256*1024*1024;
    auto& c=o.modem;c.sample_rate=1024;c.carrier_hz=256;c.bandwidth_hz=100;
    c.spreading_factor=64;c.pulse_shaping=false;c.receiver_strategy=modem::ReceiverStrategy::complex_only;
    std::array<std::uint8_t,32> secret{};
    for(std::size_t i=0;i<secret.size();++i)secret[i]=static_cast<std::uint8_t>(i*11+7);
    o.key.emplace(secret);
    c.oscillator_search=modem::OscillatorSearchConfig{
        tuning::oscillator_model(tuning::parse_oscillator_preset("crystal")),
        tuning::oscillator_model(tuning::parse_oscillator_preset("ic-7100")),0,3};
    c.oscillator_search->reference=modem::OscillatorReference::independent_audio;
    while(modem::oscillator_pattern_search(c).hypotheses.size()<512 && c.oscillator_search->rf_shift_hz<1e8)
        c.oscillator_search->rf_shift_hz+=10000;
    return o;
}
struct Capture {std::vector<float> mixed,without_helper;Bytes wire;std::uint64_t first=0;};
Capture cover_capture(const transfer::Options& o) {
    const auto config=transfer::seeded_config(o,o.timestamp);
    const auto training=modem::training_sample_count(config);
    modem::ChannelConfig effects;effects.snr_db=40;effects.clock_error_ppm=0;
    effects.frequency_offset_hz=0;effects.phase_noise_degrees_per_sqrt_second=0;effects.seed=5181;
    helper::Options helper;helper.power_w=1500;helper.data_power_w=4;helper.callsign.clear();
    modem::SampledSimulationChannel calibration(o.modem,effects,helper);
    calibration.set_helper_time_key(o.key);calibration.begin_burst();
    auto initial=transfer::binary_transmitter(Bytes{0,0,1},o);std::array<float,1> sample{};
    calibration.read(*initial,sample);
    const auto idle=8031ULL-training-static_cast<std::uint64_t>(std::ceil(calibration.startup_offset_samples()));
    modem::SampledSimulationChannel mixed(o.modem,effects,helper),only(o.modem,effects,helper),noise(o.modem,effects,helper);
    mixed.set_helper_time_key(o.key);only.set_helper_time_key(o.key);
    auto source=transfer::binary_transmitter(Bytes{0,0,1},o),suppressed=transfer::binary_transmitter(Bytes{0,0,1},o);
    Capture out;std::array<float,163> a{},b{},n{};
    for(std::uint64_t at=0;at<idle;) {
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(a.size(),idle-at));
        mixed.read_noise(std::span(a).first(count));only.read_noise(std::span(b).first(count));noise.read_noise(std::span(n).first(count));
        check(std::equal(a.begin(),a.begin()+count,b.begin()) && std::equal(a.begin(),a.begin()+count,n.begin()),
              "physical cover control changed its initial noise");
        out.mixed.insert(out.mixed.end(),a.begin(),a.begin()+count);
        out.without_helper.insert(out.without_helper.end(),a.begin(),a.begin()+count);at+=count;
    }
    mixed.begin_burst(false);only.begin_burst(true);noise.begin_burst(false);
    bool ended=false,extended=false;
    const auto tail=modem::pattern_absence_capture_samples(config)+config.sample_rate+128;
    while(out.mixed.size()<24000) {
        const auto capacity=std::min(a.size(),24000-out.mixed.size());std::size_t count=0;
        if(!ended) {
            count=mixed.read(*source,std::span(a).first(capacity));
            check(only.read(*suppressed,std::span(b).first(capacity))==count,"matched source durations changed");
            if(!count)ended=true;
            else {
                check(mixed.payload_begin_sample()==only.payload_begin_sample() &&
                      mixed.payload_end_sample()==only.payload_end_sample(),"matched source coordinates changed");
                out.first=*mixed.payload_begin_sample()+training;
                if(mixed.payload_finished() && !extended) {
                    mixed.extend_helper_tail(static_cast<double>(tail)/config.sample_rate);
                    only.extend_helper_tail(static_cast<double>(tail)/config.sample_rate);extended=true;
                }
            }
        }
        if(ended){count=capacity;mixed.read_noise(std::span(a).first(count));only.read_noise(std::span(b).first(count));}
        noise.read_noise(std::span(n).first(count));
        for(std::size_t i=0;i<count;++i) {
            out.mixed.push_back(a[i]);
            out.without_helper.push_back(static_cast<float>(static_cast<double>(a[i])-b[i]+n[i]));
        }
    }
    out.wire=source->transmit_trace().wire_bits;
    check(out.first==8031 && out.wire==Bytes({1,0,0}),"physical cover fixture changed its source");
    return out;
}

}
int main(int argc,char** argv)try {
 if(argc!=2)throw Error("output path required");
 const auto o=options();const auto capture=cover_capture(o);
 std::ofstream output(argv[1],std::ios::binary);
 if(!output)throw Error("cannot create output");
 output.write("DPCOVER1",8);
 const auto little=[&](std::uint64_t value,unsigned count) {
  for(unsigned i=0;i<count;++i)output.put(static_cast<char>(value>>(8*i)));
 };
 little(capture.mixed.size(),4);little(capture.first,8);little(capture.wire.size(),4);
 for(const auto bit:capture.wire)output.put(static_cast<char>(bit));
 for(const auto* samples:{&capture.mixed,&capture.without_helper}) {
  for(const float value:*samples) {
   static_assert(sizeof(float)==4);
   const auto bits=std::bit_cast<std::uint32_t>(value);
   const std::array<char,4> bytes{static_cast<char>(bits),static_cast<char>(bits>>8),static_cast<char>(bits>>16),static_cast<char>(bits>>24)};
   output.write(bytes.data(),bytes.size());
  }
 }
 if(!output)throw Error("capture write failed");
 std::cout<<"mixed="<<capture.mixed.size()<<" without_helper="<<capture.without_helper.size()<<" first="<<capture.first<<" wire=";
 for(auto bit:capture.wire)std::cout<<unsigned(bit);
 std::cout<<'\n';
 return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
