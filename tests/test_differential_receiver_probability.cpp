#include "datapump/channel.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/pattern_receiver.hpp"
#include "datapump/simulation_estimate.hpp"
#include "../src/pattern_differential.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <future>
#include <iostream>
#include <string>

using namespace datapump;
namespace {
void check(bool condition,const char* message) {if(!condition)throw Error(message);}
constexpr std::size_t workspace=8*1024*1024;
// Live reserves half of a one-profile workspace for peer/transmit/plot state.
constexpr std::size_t receiver_workspace=workspace/2;
constexpr unsigned captures_per_case=64;
constexpr double local_seconds=1;

struct Case {
    const char* name;
    bool keyed;
    double energy_db,diffusion;
    unsigned bits=1;
    bool compare_previous=false;
    bool shaped=false;
    double window_seconds=local_seconds;
};

transfer::Options options(const Case& fixture) {
    transfer::Options result;
    result.modem.sample_rate=64;
    result.modem.carrier_hz=fixture.shaped?10:16;
    result.modem.bandwidth_hz=32;
    result.modem.spreading_factor=8192;
    result.modem.integration_seconds=512*fixture.window_seconds;
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
    result.clock_error_ppm=0;
    result.phase_noise_degrees_per_sqrt_second=fixture.diffusion;
    // Independent from the estimator draws and from the older sampled matrix.
    result.seed=0x7389b0741ULL+std::uint64_t{1301081}*seed;
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
                           const Bytes& bits,bool noise_only=false) {
    modem::StreamingTransmitter source(modem::RawBits{bits},config,workspace);
    modem::SampledSimulationChannel transport(config,impairment);
    std::array<float,1021> block{};
    std::vector<float> samples;
    if(noise_only) {
        const auto length=source.total_samples()+modem::pattern_absence_samples(config)+config.sample_rate;
        samples.resize(static_cast<std::size_t>(length));
        transport.read_noise(samples);
        return samples;
    }
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

struct Received {Bytes bits;unsigned completions=0;bool compact=false;};
Received receive(const modem::Config& config,std::span<const float> samples,bool differential=true,
                 double window_seconds=local_seconds,std::size_t receiver_bytes=receiver_workspace) {
    modem::PatternSearch search;
    search.expand_clock_search=true;
    search.start_offset_seconds=static_cast<double>(modem::training_sample_count(config)+
        modem::pattern_pulse_padding_samples(config))/config.sample_rate;
    search.start_uncertainty_seconds=1;
    search.differential_window_seconds=differential?window_seconds:0;
    // Match the application's clock-window preference for long keyed codes.
    search.compact_clock_search=config.scramble && modem::symbol_sample_count(config)>=60ULL*config.sample_rate;
    search.worker_threads=1;
    search.chunk_bits=1;
    modem::PatternReceiver receiver(config,receiver_bytes,search);
    Received result;result.compact=receiver.clock_windowed();
    std::size_t position=0;
    const auto harvest=[&] {
        check(receiver.working_bytes()<=receiver_bytes,"differential probability capture exceeded receiver workspace");
        for(const auto& event:receiver.take_bursts()) {
            check(position>=event.end_sample,"local matches published a partially observed physical symbol");
            if(event.complete) {
                check(position>=event.end_sample+modem::pattern_absence_samples(config),
                      "differential probability capture completed before fully scored absence");
                ++result.completions;
            }
            result.bits.insert(result.bits.end(),event.bits.begin(),event.bits.end());
            result.bits.insert(result.bits.end(),event.missing_slots,modem::missing_pattern_bit);
        }
    };
    while(position<samples.size()) {
        const auto count=std::min<std::size_t>(1021,samples.size()-position);
        receiver.push(samples.subspan(position,count));position+=count;harvest();
    }
    receiver.finish();harvest();
    return result;
}

void check_matrix(std::span<const Case> cases,bool compare_previous) {
    double squared_error=0,maximum_error=0;
    unsigned improved=0,previous=0,modeled_cases=0;
    const auto begun=std::chrono::steady_clock::now();
    for(const auto& fixture:cases) {
        const auto configured=options(fixture);
        const auto prediction=simulation::estimate(transmission(configured.modem,fixture.bits),configured,true,
            channel(fixture,configured.modem,0),{},1,true,fixture.window_seconds);
        if(!prediction.one_bit_confidence_available)
            std::cerr<<fixture.name<<": probability unavailable: "<<prediction.probability_model_limit<<'\n';
        check(prediction.one_bit_confidence_available && prediction.drift_model_available &&
              !prediction.coherent_reference_only && prediction.differential_windows==512 &&
              prediction.differential_window_seconds==fixture.window_seconds,
              "sampled differential geometry lacks a matching implemented-detector probability");
        if(fixture.bits==1) {
            check(prediction.confidence_available && std::isfinite(prediction.success_probability) &&
                  prediction.success_probability>=0 && prediction.success_probability<=1,
                  "single-bit differential estimate is not an available finite probability");
        } else {
            // The original held-out 001 captures exposed an actual limitation:
            // the compact receiver keeps the earliest admitted timing lane,
            // which can have a poorer fit on subsequent bits. Multiplying
            // independent nearest-lane probabilities was overly optimistic.
            // Keep these captures and require the explicit coverage exclusion
            // until a model includes that conditional ownership mechanism.
            check(!prediction.confidence_available &&
                  prediction.probability_model_limit.find("timing ownership")!=std::string::npos,
                  "compact multi-bit differential estimate hid unmodeled timing ownership");
        }
        // Independent complete captures can run concurrently without sharing
        // receiver state or random streams. Each receiver still has one DSP
        // worker, and fixed seed positions keep counts/order reproducible.
        constexpr unsigned workers=4;
        std::array<std::future<std::pair<unsigned,unsigned>>,workers> tasks;
        for(unsigned worker=0;worker<workers;++worker)
            tasks[worker]=std::async(std::launch::async,[&,worker] {
                unsigned recovered=0,baseline=0;
                for(unsigned seed=worker;seed<captures_per_case;seed+=workers) {
                    const auto bits=fixture.bits==1?Bytes{static_cast<std::uint8_t>(seed%2)}:Bytes{0,0,1};
                    const auto samples=capture(configured.modem,channel(fixture,configured.modem,seed),bits);
                    const auto observed=receive(configured.modem,samples,true,fixture.window_seconds);
                    check(observed.compact,"sampled probability matrix changed its modeled compact engine");
                    recovered+=observed.completions==1 && observed.bits==bits;
                    if(fixture.compare_previous) {
                        const auto old=receive(configured.modem,samples,false);
                        baseline+=old.completions==1 && old.bits==bits;
                    }
                }
                return std::pair{recovered,baseline};
            });
        unsigned recovered=0,baseline=0;
        for(auto& task:tasks) {
            const auto counts=task.get();recovered+=counts.first;baseline+=counts.second;
        }
        const auto observed=static_cast<double>(recovered)/captures_per_case;
        const auto error=std::abs(observed-prediction.success_probability);
        if(prediction.confidence_available) {
            squared_error+=error*error;maximum_error=std::max(maximum_error,error);++modeled_cases;
        }
        if(fixture.compare_previous){improved+=recovered;previous+=baseline;}
        std::cout<<fixture.name<<": predicted ";
        if(prediction.confidence_available)std::cout<<prediction.success_probability;
        else std::cout<<"unavailable (timing ownership)";
        std::cout<<", sampled "<<recovered<<'/'<<captures_per_case;
        if(fixture.compare_previous)std::cout<<", previous "<<baseline<<'/'<<captures_per_case;
        std::cout<<std::endl;
        // Set before observing the holdout captures. These allow Monte Carlo
        // variation while bounding model error more tightly than the older
        // coherent/four-quarter matrix. They do not establish radio-link
        // reliability or a rare-event/false-alarm certification.
        const auto uncertainty=3*std::sqrt(prediction.success_probability*
            (1-prediction.success_probability)/captures_per_case);
        if(prediction.confidence_available)
            check(error<=.10+uncertainty,"differential prediction disagrees materially with sampled reception");
        else check(recovered>0,"unmodeled multi-bit control lost every full sampled reception");
    }
    check(modeled_cases>0,"differential matrix lost all of its probability coverage");
    const auto rms=std::sqrt(squared_error/modeled_cases);
    std::cout<<"Differential probability RMS error "<<rms<<", maximum "<<maximum_error<<", "<<modeled_cases<<" modeled cases"
             <<", "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-begun).count()<<" s\n";
    check(rms<=.07,"differential probability matrix retains excessive systematic error");
    if(compare_previous)
        check(improved>=previous+8,"sampled differential matrix lost recovery beyond the earlier detector");
}

void sampled_matrix() {
    // Predeclared holdout matrix. These use exactly the production waveform,
    // fractional-start channel and adaptive receiver; only the local receiver
    // duration is scaled to make repeated PCM captures practical. No sampled
    // trajectory, source timing, bit or noise seed is given to the estimator.
    // Each 512-second symbol has 8192 chips and 512 complete 16-chip windows.
    constexpr std::array cases{
        Case{"public stable weak",false,16,0},
        Case{"public stable transition",false,18,0},
        Case{"private stable transition",true,18,0},
        Case{"public stable strong",false,22,0},
        Case{"public slow diffusion",false,24,10},
        Case{"private slow diffusion",true,24,10},
        Case{"public differential weak",false,26,25},
        Case{"public differential transition",false,29,25},
        Case{"private differential transition",true,29,25},
        Case{"public differential strong",false,32,25},
        Case{"private differential strong",true,32,25},
        Case{"public faster diffusion",false,35,40,1,true},
        Case{"private faster diffusion",true,35,40,1,true},
        Case{"public 001 diffusion",false,29,25,3},
        Case{"private 001 diffusion",true,32,25,3},
    };
    check_matrix(cases,true);
}

void sampled_shaped_matrix() {
    // The carrier lies exactly at the shaped signal's lower supported edge.
    // Its default finite bank therefore contains the center carrier only;
    // ordinary workspace policy selects the same compact raw-PCM receiver.
    // Evaluate these separately: passing them cannot dilute the original
    // unshaped holdout's predeclared aggregate error target. Eight-second
    // windows contain 128 chips, making all local real-PCM image/cross-image
    // covariances fit the model's explicit one-percent bound. Diffusion is
    // scaled to retain the same dimensionless 25-degree local variation.
    constexpr std::array cases{
        Case{"shaped public differential transition",false,28,8.838834764831844,1,false,true,8},
        Case{"shaped private differential transition",true,28,8.838834764831844,1,false,true,8},
    };
    check_matrix(cases,false);
}

void null_controls() {
    const Case fixture{"null controls",true,35,40};
    const auto configured=options(fixture);
    for(unsigned seed=0;seed<16;++seed) {
        const Bytes bits{static_cast<std::uint8_t>(seed%2)};
        const auto noise=capture(configured.modem,channel(fixture,configured.modem,seed),bits,true);
        const auto null=receive(configured.modem,noise);
        check(null.bits.empty() && null.completions==0,"differential probability null control admitted noise");
        auto wrong=configured.modem;wrong.spreading_seed[0]^=1;
        const auto signal=capture(configured.modem,channel(fixture,configured.modem,seed),bits);
        const auto unrelated=receive(wrong,signal);
        check(unrelated.bits.empty() && unrelated.completions==0,"differential probability null control admitted a wrong key");
    }
}

void default_window_capture() {
    // Run the real 100-second default too. Carrier, chip and diffusion rates
    // are scaled together; this remains sampled PCM through the complete
    // 14.2-hour bit and one equally long observed absence. It establishes
    // implementation coverage at the default duration, not a four-capture
    // estimate of a rare failure probability.
    for(bool keyed:{false,true}) {
        Case fixture{"default 100-second windows",keyed,35,2.5};
        auto configured=options(fixture);
        configured.modem.carrier_hz=.16;
        configured.modem.bandwidth_hz=.32;
        configured.modem.integration_seconds=51200;
        configured.dsp_workspace_bytes=32*1024*1024;
        const auto window=modem::detail::differential_window_samples(
            modem::symbol_sample_count(configured.modem),modem::pattern_chip_samples(configured.modem),
            configured.modem.sample_rate,modem::PatternSearch{}.differential_window_seconds);
        check(window==6400 && modem::symbol_sample_count(configured.modem)/window==512,
              "default sampled capture missed receiver differential eligibility");
        const auto prediction=simulation::estimate(transmission(configured.modem,1),configured,true,
            channel(fixture,configured.modem,0));
        check(prediction.confidence_available && prediction.differential_windows==512 &&
              prediction.differential_window_seconds==100 && prediction.success_probability>.99,
              "default-window strong-link estimate did not model differential evidence");
        for(unsigned seed=0;seed<2;++seed) {
            const Bytes bits{static_cast<std::uint8_t>(seed)};
            const auto pcm=capture(configured.modem,channel(fixture,configured.modem,seed+211),bits);
            const auto observed=receive(configured.modem,pcm,true,100,16*1024*1024);
            check(observed.compact==keyed,"default duration coverage missed the intended FFT/compact engines");
            check(observed.completions==1 && observed.bits==bits,
                  "default 100-second differential window failed full sampled reception");
        }
    }
}
}
int main() {
    try {sampled_matrix();sampled_shaped_matrix();null_controls();default_window_capture();
        std::cout<<"differential receiver probability tests passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<"differential receiver probability tests failed: "<<error.what()<<'\n';return 1;}
}
