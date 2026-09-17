#include "datapump/channel.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/pattern_receiver.hpp"
#include "datapump/transfer.hpp"
#include "datapump/tuning.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <optional>
#include <stdexcept>

using namespace datapump;
namespace {
void check(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
void sampled_case(double bandwidth,std::uint64_t seed,bool noise_only=false) {
    transfer::Options options;
    options.modem=tuning::resolve(bandwidth,-6,tuning::PatternMode::auto_pattern,false).config;
    options.timestamp=1800000000;
    options.dsp_workspace_bytes=64*1024*1024;
    const auto config=transfer::seeded_config(options,options.timestamp);
    Message message;message.kind=MessageKind::text;message.data={'a'};
    const Bytes expected{0,1,1};
    check(transfer::message_wire_bits(message,options)==expected,"weak-signal fixture changed exact a=011 wire bits");
    auto source=transfer::message_transmitter(message,options);
    modem::ChannelConfig impairment;impairment.seed=seed;
    impairment.snr_db=tuning::link_budget(tuning::parse_simulation_preset("3dBm -170dB"),
                                         bandwidth,config.sample_rate).sample_snr_db;
    // The search gets no actual channel offset, clock rate, seed, phase, bit
    // count or expected text. These are the same finite application settings.
    modem::PatternSearch search;search.expand_clock_search=true;search.worker_threads=4;
    search.start_offset_seconds=(static_cast<double>(modem::training_sample_count(config))+
        modem::pattern_pulse_padding_samples(config))/config.sample_rate;
    search.start_uncertainty_seconds=options.search_seconds+1.;
    modem::PatternReceiver receiver(config,options.dsp_workspace_bytes,search);
    check(!receiver.clock_windowed(),"weak expanded search must use joint FFT competition");
    transfer::StreamReceiver content(options,options.timestamp);
    modem::SampledSimulationChannel channel(config,impairment);
    std::array<float,2048> samples{};
    Bytes accepted;
    std::optional<std::pair<std::uint64_t,std::uint64_t>> identity;
    std::optional<transfer::Received> result;
    std::size_t completions=0,bit_polls=0;
    const auto poll=[&] {
        check(receiver.working_bytes()<=options.dsp_workspace_bytes,"weak search exceeded its DSP workspace");
        bool progress=false;
        for(auto& event:receiver.take_bursts()) {
            check(!noise_only,"noise-only wide clock search admitted a reception");
            const auto current=std::pair{event.stream_first_sample,event.stream_first_symbol};
            if(!identity)identity=current;
            check(current==*identity,"weak search duplicated one physical stream");
            check(!event.missing_slots,"weak regression introduced an unknown slot");
            accepted.insert(accepted.end(),event.bits.begin(),event.bits.end());
            check(accepted.size()<=expected.size() && std::equal(accepted.begin(),accepted.end(),expected.begin()),
                  "weak search changed, repeated or omitted an exact prefix");
            progress|=!event.bits.empty();
            completions+=event.complete;
            result=content.push(std::move(event),receiver.diagnostics());
        }
        if(progress)++bit_polls;
    };
    std::uint64_t count=0;
    if(noise_only) {
        // Exercise complete observation windows over the same approximate
        // source+absence duration, with independent channel-generated noise.
        count=source->total_samples()+modem::pattern_absence_samples(config)+config.sample_rate+
            2*modem::pattern_pulse_padding_samples(config);
    } else {
        while(const auto n=channel.read(*source,samples)) {
            receiver.push(std::span(samples).first(n));poll();
        }
        check(completions==0,"transmitter exhaustion completed reception before full observed absence");
        count=modem::pattern_absence_samples(config)+config.sample_rate+
            2*modem::pattern_pulse_padding_samples(config);
    }
    while(count) {
        const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(count,samples.size()));
        auto block=std::span(samples).first(n);channel.read_noise(block);receiver.push(block);poll();count-=n;
    }
    receiver.finish();poll();
    if(noise_only)check(accepted.empty() && completions==0,"noise created a completed message");
    else {
        check(accepted==expected && bit_polls>=2 && completions==1,"weak sampled message lost exact incremental physical reception");
        check(result && result->stream_complete && result->short_text_decoded && !result->missing_symbols &&
              result->error.empty() && result->raw_bits==expected && result->content.message.data==message.data,
              "weak sampled reception did not decode exact a after physical completion");
    }
}
}
int main() {
    try {
        for(const auto seed:{1U,2U,3U})sampled_case(1,seed);
        sampled_case(1,7919,true);
        sampled_case(100,1);
        std::cout<<"weak sampled-signal tests passed\n";return 0;
    } catch(const std::exception& error) {
        std::cerr<<"weak sampled-signal tests failed: "<<error.what()<<'\n';return 1;
    }
}
