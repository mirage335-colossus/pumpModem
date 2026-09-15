#include "datapump/modem.hpp"
#include "datapump/streaming_modem.hpp"
#include "datapump/pattern_pulse.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>
using namespace datapump;
namespace m=datapump::modem;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class F>void rejects(F action,const char* message){try{action();}catch(const Error&){return;}throw std::runtime_error(message);}
int main(){try {
    m::Config config;config.spreading_factor=16;
    require(config.pattern_symbols && config.constellation_bits==1 && m::preamble(config).empty(),"default modem retained fixed APSK preamble");
    for(const std::size_t count:{1,3,5,7,31}) {
        const Bytes bytes(count,0xa7);const auto samples=m::modulate(bytes,config);
        require(m::payload_symbol_count(count,config)==count*8,"byte stream added symbol padding");
        require(samples.size()==m::training_sample_count(config)+2*m::pattern_pulse_padding_samples(config)+m::suppression_sample_count(config)+count*8*m::symbol_sample_count(config),"byte modulation sample count mismatch");
        require(m::waveform_sample_count(count,config)==samples.size(),"waveform estimate mismatch");
    }
    const Bytes status{1,0,1};const auto wave=m::modulate_status(status,config);
    require(wave.size()==m::training_sample_count(config)+2*m::pattern_pulse_padding_samples(config)+m::suppression_sample_count(config)+status.size()*m::symbol_sample_count(config),"raw waveform length differs from exact bits and physical overhead");
    require(m::detect_status(wave,status,config)>.99,"status waveform reference mismatch");
    m::ChannelConfig channel;channel.clock_error_ppm=0;channel.phase_noise_degrees_per_sqrt_second=0;channel.snr_db=15;channel.seed=731;
    require(m::simulate(wave,config,channel)==m::simulate(wave,config,channel),"sampled simulation is nondeterministic");
    auto invalid=config;invalid.pattern_symbols=false;invalid.constellation_bits=4;
    rejects([&]{m::modulate(Bytes(33),invalid);},"legacy APSK waveform accepted");
    for(const auto bits:{2U,3U,5U,6U}){invalid=config;invalid.constellation_bits=bits;rejects([&]{m::validate(invalid);},"multi-bit padded symbol config accepted");}
    invalid=config;invalid.spreading_mode=m::SpreadingMode::tone;invalid.data_key.emplace(Bytes(32,7));
    rejects([&]{m::validate(invalid);},"direct modem accepted encrypted tone");
    invalid.data_key.reset();invalid.dsss=true;rejects([&]{m::validate(invalid);},"direct modem accepted DSSS tone");
    invalid.dsss=false;invalid.scramble=true;rejects([&]{m::validate(invalid);},"direct modem accepted scrambled tone");
    invalid=config;invalid.bandwidth_hz=std::numeric_limits<double>::quiet_NaN();rejects([&]{m::validate(invalid);},"NaN config accepted");
    invalid=config;invalid.training_seconds=6;rejects([&]{m::validate(invalid);},"nonstandard training accepted");
    invalid=config;invalid.spreading_factor=16385;rejects([&]{m::validate(invalid);},"oversized spreading accepted");
    auto centered=config;centered.bandwidth_hz=3600;centered.sample_rate=14400;centered.carrier_hz=1500;
    m::validate(centered);
    require(m::pattern_pulse_enabled(centered),"centered audio must use the same shaping eligibility as the transmitter");
    for(const auto carrier:{1125.,6075.}) {centered.carrier_hz=carrier;m::validate(centered);}
    for(const auto carrier:{1124.99,6075.01,-1.,std::numeric_limits<double>::quiet_NaN()}) {
        centered.carrier_hz=carrier;rejects([&]{m::validate(centered);},"RRC support crossed DC or Nyquist");
    }
    centered.carrier_hz=1500;centered.pulse_shaping=false;
    rejects([&]{m::validate(centered);},"rectangular waveform used a shaped passband allowance");
    centered.pulse_shaping=true;centered.spreading_factor=15;
    rejects([&]{m::validate(centered);},"fewer than sixteen complete chips used a shaped passband allowance");
    centered.spreading_factor=16;centered.sample_rate=15000;
    rejects([&]{m::validate(centered);},"partial final chip bypassed the shaping duration requirement");
    centered.sample_rate=14400;centered.integration_seconds=16./1800.;m::validate(centered);
    centered.integration_seconds=15./1800.;
    rejects([&]{m::validate(centered);},"short explicit integration used a shaped passband allowance");
    invalid=config;invalid.memory_limit=1024;rejects([&]{m::modulate(Bytes(32),invalid);},"waveform allocation budget ignored");
    std::stringstream wav(std::ios::in|std::ios::out|std::ios::binary);m::write_wav(wav,wave,config.sample_rate);const auto restored=m::read_wav(wav);
    require(restored.sample_rate==config.sample_rate && restored.samples.size()==wave.size(),"WAV metadata mismatch");
    for(std::size_t i=0;i<wave.size();++i)require(std::abs(wave[i]-restored.samples[i])<.00004,"WAV PCM16 quantization");
    std::stringstream low_rate(std::ios::in|std::ios::out|std::ios::binary);m::write_wav(low_rate,std::array<float,4>{.1F,-.2F,.3F,-.4F},4800);
    require(m::read_wav(low_rate).sample_rate==4800,"internal clock WAV rejected");
    std::stringstream short_wav("RIFF",std::ios::in|std::ios::binary);rejects([&]{m::read_wav(short_wav);},"truncated WAV accepted");
    std::stringstream limited(wav.str(),std::ios::in|std::ios::binary);rejects([&]{m::read_wav(limited,32);},"WAV allocation budget ignored");
    auto corrupt=wav.str();corrupt[40]=static_cast<char>(0xff);corrupt[41]=static_cast<char>(0xff);corrupt[42]=static_cast<char>(0xff);corrupt[43]=static_cast<char>(0x7f);
    std::stringstream huge(corrupt,std::ios::in|std::ios::binary);rejects([&]{m::read_wav(huge);},"oversized WAV data chunk accepted");
    std::stop_source stopped;stopped.request_stop();rejects([&]{m::modulate(Bytes(1),config,stopped.get_token());},"cancelled modulation completed");
    config.integration_seconds=3600;config.memory_limit=1024*1024*1024;
    m::StreamingTransmitter source(m::RawBits{Bytes{0,1}},config);std::stop_source during;
    std::jthread cancel([&]{std::this_thread::sleep_for(std::chrono::milliseconds(5));during.request_stop();});
    std::array<float,4096> buffer{};const auto before=std::chrono::steady_clock::now();
    rejects([&]{while(source.read(buffer,during.get_token())){}},"streaming modulation ignored cancellation");
    require(std::chrono::steady_clock::now()-before<std::chrono::seconds(2),"cancellation waited for a complete symbol");
    std::cout<<"modem tests passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
