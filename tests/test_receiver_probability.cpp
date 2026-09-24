#include "datapump/channel.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/pattern_receiver.hpp"
#include "datapump/simulation_estimate.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <string>

using namespace datapump;
namespace {
void check(bool condition,const char* message) {if(!condition)throw Error(message);}
constexpr std::size_t workspace=8*1024*1024;
constexpr unsigned captures_per_case=64;

struct Case {
    const char* name;
    bool keyed;
    double rate,energy_db,diffusion;
    double clock_ppm=0,frequency_hz=0;
    bool compare_coherent=false;
    unsigned bit_count=1;
    bool shaped=false;
};
transfer::Options options(const Case& fixture) {
    transfer::Options result;
    result.modem.sample_rate=128;
    result.modem.carrier_hz=32;
    result.modem.bandwidth_hz=fixture.rate;
    result.modem.integration_seconds=16;
    result.modem.pulse_shaping=fixture.shaped;
    result.modem.scramble=fixture.keyed;
    result.timestamp=1800000041;
    result.modem.stream_epoch=result.timestamp;
    for(std::size_t i=0;i<result.modem.spreading_seed.size();++i)
        result.modem.spreading_seed[i]=static_cast<std::uint8_t>(13*i+29);
    result.search_seconds=0;
    result.dsp_workspace_bytes=workspace;
    return result;
}
modem::ChannelConfig channel(const Case& fixture,const modem::Config& config,unsigned seed) {
    modem::ChannelConfig result;
    result.snr_db=fixture.energy_db-10*std::log10(modem::symbol_sample_count(config)/2.);
    result.phase_noise_degrees_per_sqrt_second=fixture.diffusion;
    result.clock_error_ppm=fixture.clock_ppm;
    result.frequency_offset_hz=fixture.frequency_hz;
    result.seed=7919+std::uint64_t{104729}*seed;
    return result;
}
transfer::Estimate transmission(const modem::Config& config,unsigned bits) {
    transfer::Estimate result;
    result.wire_bits=bits;
    result.total_seconds=static_cast<double>(modem::training_sample_count(config)+
        2*modem::pattern_pulse_padding_samples(config)+bits*modem::symbol_sample_count(config)+
        modem::suppression_sample_count(config))/config.sample_rate;
    return result;
}
std::vector<float> capture(const modem::Config& config,const modem::ChannelConfig& impairment,
                           const Bytes& bits) {
    // Exercise the production sampled channel, including seeded fractional
    // startup, interpolation, ongoing phase diffusion and suppression noise.
    // No exact timing or phase information reaches the receiver.
    modem::StreamingTransmitter source(modem::RawBits{bits},config,workspace);
    modem::SampledSimulationChannel transport(config,impairment);
    std::array<float,257> block{};
    std::vector<float> samples;
    while(const auto count=transport.read(source,block))
        samples.insert(samples.end(),block.begin(),block.begin()+static_cast<std::ptrdiff_t>(count));
    auto trailing=modem::pattern_absence_samples(config)+config.sample_rate+
        2*modem::pattern_pulse_padding_samples(config);
    while(trailing) {
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(trailing,block.size()));
        transport.read_noise(std::span(block).first(count));
        samples.insert(samples.end(),block.begin(),block.begin()+static_cast<std::ptrdiff_t>(count));
        trailing-=count;
    }
    return samples;
}
bool receive(const modem::Config& config,std::span<const float> samples,const Bytes& expected,bool drift=true) {
    modem::PatternSearch search;
    search.drift_tolerant=drift;
    search.expand_clock_search=true;
    search.start_offset_seconds=static_cast<double>(modem::training_sample_count(config)+
        modem::pattern_pulse_padding_samples(config))/config.sample_rate;
    search.start_uncertainty_seconds=1;
    search.worker_threads=1;
    search.chunk_bits=1;
    modem::PatternReceiver receiver(config,workspace,search);
    check(receiver.drift_tolerant()==drift,"sampled probability fixture did not exercise the requested detector");
    Bytes observed;
    std::size_t completions=0,position=0;
    const auto harvest=[&] {
        check(receiver.working_bytes()<=workspace,"sampled probability validation exceeded receiver workspace");
        for(const auto& event:receiver.take_bursts()) {
            check(position>=event.end_sample,"receiver exposed evidence before observing its complete symbol");
            if(event.complete) {
                check(position>=event.end_sample+modem::pattern_absence_samples(config),
                      "sampled probability validation completed without a whole absent symbol");
                ++completions;
            }
            observed.insert(observed.end(),event.bits.begin(),event.bits.end());
            observed.insert(observed.end(),event.missing_slots,modem::missing_pattern_bit);
        }
    };
    while(position<samples.size()) {
        const auto count=std::min<std::size_t>(257,samples.size()-position);
        receiver.push(samples.subspan(position,count));position+=count;harvest();
    }
    receiver.finish();harvest();
    return completions==1 && observed==expected;
}

void sampled_estimates() {
    // The short PCM clocks make repeated physical captures cheap. Phase
    // diffusion is deliberately increased to span the dimensionless phase
    // changes of long symbols. These are synthetic model checks, not measured
    // oscillator specifications or evidence about any particular radio link.
    constexpr std::array cases{
        Case{"64-chip public stable",false,8,18,0},
        Case{"64-chip private stable",true,8,18,0},
        Case{"public weak",false,32,16,0},
        Case{"public transition",false,32,18,0},
        Case{"private transition",true,32,18,0},
        Case{"public strong",false,32,22,0},
        Case{"private strong",true,32,22,0},
        Case{"public mild drift",false,32,20,30},
        Case{"private mild drift",true,32,20,30},
        Case{"public drift transition",false,32,22,60},
        Case{"private drift transition",true,32,22,60},
        Case{"public drift recovery",false,32,26,60,0,0,true},
        Case{"private drift recovery",true,32,26,60,0,0,true},
        Case{"public faster drift",false,32,26,90},
        Case{"public clock and drift",false,32,22,30,100,.007},
        Case{"private clock and drift",true,32,22,30,-100,-.007},
        Case{"public 001 mild drift",false,32,20,30,0,0,false,3},
        Case{"private 001 drift recovery",true,32,26,60,0,0,false,3},
        Case{"shaped public mild drift",false,32,20,30,0,0,false,1,true},
        Case{"shaped private drift recovery",true,32,26,60,0,0,false,1,true},
    };
    double squared_error=0,maximum_error=0;
    unsigned section_recoveries=0,coherent_recoveries=0;
    const auto begun=std::chrono::steady_clock::now();
    for(const auto& fixture:cases) {
        const auto configured=options(fixture);
        const auto prediction=simulation::estimate(transmission(configured.modem,fixture.bit_count),configured,true,
            channel(fixture,configured.modem,0));
        check(prediction.confidence_available && prediction.drift_model_available && !prediction.coherent_reference_only,
              "sampled probability fixture lacks the complete implemented-detector estimate");
        check(std::isfinite(prediction.success_probability) && prediction.success_probability>=0 &&
              prediction.success_probability<=1,"sampled success estimate is not a finite probability");
        unsigned recovered=0,baseline=0;
        for(unsigned seed=0;seed<captures_per_case;++seed) {
            const auto bits=fixture.bit_count==1?Bytes{static_cast<std::uint8_t>(seed%2)}:Bytes{0,0,1};
            const auto samples=capture(configured.modem,channel(fixture,configured.modem,seed),bits);
            recovered+=receive(configured.modem,samples,bits);
            if(fixture.compare_coherent)baseline+=receive(configured.modem,samples,bits,false);
        }
        const auto observed=static_cast<double>(recovered)/captures_per_case;
        const auto error=std::abs(observed-prediction.success_probability);
        squared_error+=error*error;maximum_error=std::max(maximum_error,error);
        if(fixture.compare_coherent){section_recoveries+=recovered;coherent_recoveries+=baseline;}
        std::cout<<fixture.name<<": predicted "<<prediction.success_probability
                 <<", sampled "<<recovered<<'/'<<captures_per_case;
        if(fixture.compare_coherent)std::cout<<", coherent "<<baseline<<'/'<<captures_per_case;
        std::cout<<'\n';
        // Deliberately allow both finite-capture variation and remaining
        // engineering-model error. This catches gross optimism/pessimism;
        // it does not certify a calibrated probability or a false-alarm rate.
        const auto uncertainty=3*std::sqrt(prediction.success_probability*
            (1-prediction.success_probability)/captures_per_case);
        check(error<=.15+uncertainty,"success estimate disagrees materially with independent sampled reception");
    }
    const auto rms_error=std::sqrt(squared_error/cases.size());
    std::cout<<"Sampled probability RMS error "<<rms_error<<", maximum "<<maximum_error
             <<", "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-begun).count()<<" s\n";
    check(rms_error<=.18,"sampled probability matrix retains a material systematic model error");
    check(section_recoveries>=coherent_recoveries+8,
          "independent sampled phase trajectories no longer demonstrate the implemented reception improvement");
}
}
int main() {
    try {sampled_estimates();std::cout<<"receiver probability tests passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<"receiver probability tests failed: "<<error.what()<<'\n';return 1;}
}
