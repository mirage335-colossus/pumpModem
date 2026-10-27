#include "datapump/pattern_receiver.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/simulation_estimate.hpp"
#include "datapump/tuning.hpp"
#include "pattern_observation_layout.hpp"
#include "receiver_oscillator_policy.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>
using namespace datapump;
namespace {
void check(bool value,const char* text){if(!value)throw Error(text);}
}
int main(){try {
    transfer::Options options;
    auto& c=options.modem;
    c=tuning::resolve(36,8,tuning::PatternMode::auto_keystream,true,1500,10,
        modem::OuterDsssVersion::interleaved_v2).config;
    options.key=Crypto(Bytes(32,0x63));options.dsp_workspace_bytes=256*1024*1024;
    options.audio_timing_error_seconds=.07;options.clock_sync=clock_sync::parse("default");
    c.oscillator_search.emplace();auto& policy=*c.oscillator_search;
    policy.lf=policy.rf=tuning::oscillator_model(tuning::parse_oscillator_preset("gpsdo-xo"));
    policy.rf_shift_hz=3e7;policy.margin=3;policy.reference=modem::OscillatorReference::independent_audio;
    c.receiver_strategy=modem::ReceiverStrategy::automatic;c.receiver_cpu_budget=.6;
    const auto catalogue=transfer::receiver_oscillator_search(c,options);
    modem::PatternSearch search;search.hypotheses=catalogue.hypotheses;
    search.admitted_frequency_half_width_hz=catalogue.frequency.requested_half_width_hz;
    search.admitted_clock_half_width_ppm=catalogue.requested_clock_half_width_ppm;
    search.admitted_clock_cell_half_width_ppm=catalogue.clock_step_ppm/2;
    search.complex_frequency_coverage=!catalogue.frequency.limited;
    search.complex_clock_coverage=catalogue.clock_half_width_ppm>=catalogue.requested_clock_half_width_ppm;
    search.start_offset_seconds=options.search_seconds;
    search.start_uncertainty_seconds=options.search_seconds+1;
    search.search_stream_phases=true;search.prefer_streamed_templates=true;search.worker_threads=1;
    auto keyed=transfer::seeded_config(options,1800000000);
    modem::PatternReceiver receiver(keyed,options.dsp_workspace_bytes,search);
    const auto state=receiver.acquisition_state();
    check(state.supported&&!receiver.clock_windowed(),"fixture lost the ordinary native FFT receiver");
    check(search.initial_stream_symbols==4,"fixture changed ordinary initial ordinal coverage");
    const auto bound=receiver.acquisition_work_bound();
    check(bound&&bound->initial_stream_symbols==4&&bound->original_phase_groups==7&&bound->original_pair_descriptors==21,
        "fixture lost its actual original encrypted phase/index groups");
    const auto first=receiver.initial_fft_input_samples();
    const auto bin=modem::pattern_projection_bin_samples(c,catalogue.frequency.half_width_hz);
    double minimum_rate=1;
    for(const auto& h:catalogue.hypotheses)minimum_rate=std::min(minimum_rate,1+h.clock_error_ppm*1e-6);
    const auto layout=modem::detail::pattern_observation_layout(c,bin,minimum_rate);
    std::vector<float> zeros(4096);std::uint64_t fed=0;
    const auto feed=[&](std::uint64_t end) {
        while(fed<end) {
            const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(zeros.size(),end-fed));
            receiver.push(std::span(zeros).first(n));receiver.process_pending();fed+=n;
        }
    };
    feed(first+4*bin);
    check(!receiver.initial_search_complete(),"receiver discarded later initial ordinals after its first FFT");
    const auto first_scored=receiver.acquisition_state();
    feed(first+layout.hop*bin+8*bin);
    check(receiver.initial_search_complete(),"ordinary receiver did not finish all initial ordinals after the second hop");
    const auto completed=receiver.acquisition_state();
    modem::ChannelConfig channel;channel.snr_db=-14.836614351536177;
    const auto effects=modem::oscillator_effects(c);
    channel.clock_error_ppm=effects.clock_error_ppm;channel.frequency_offset_hz=effects.frequency_offset_hz;
    channel.phase_noise_degrees_per_sqrt_second=effects.phase_noise_degrees_per_sqrt_second;
    const auto one=transfer::estimate_binary(Bytes{0},options);
    const auto estimate=simulation::estimate(one,options,true,channel,{},1,false,100,0,
        simulation::ReceiverWorkMode::hardware_fallback);
    check(estimate.hardware_sustained_modeled,"ordinary rolling workload model became unavailable");
    check(estimate.timing_phase_groups*bound->initial_stream_symbols>=bound->original_phase_groups,
        "model undercounts original encrypted phase/index alternatives");
    check(estimate.hardware_cohort_lifetime_seconds>=static_cast<double>(fed)/c.sample_rate,
        "model retires original initial-ordinal work before the actual ordinary receiver scores it");
    check(estimate.hardware_sustained_receiver_instances>=static_cast<double>(fed)/c.sample_rate,
        "one-per-second rolling model omits live initial-ordinal cohort residency");
    std::cout<<"rolling lifetime PASS first_input="<<first<<" completion_input="<<fed
        <<" first_scored_start="<<first_scored.audit_start_sample<<" final_scored_start="<<completed.audit_start_sample
        <<" modeled_lifetime="<<estimate.hardware_cohort_lifetime_seconds
        <<" ratio="<<estimate.hardware_sustained_realtime_ratio<<'\n';
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
