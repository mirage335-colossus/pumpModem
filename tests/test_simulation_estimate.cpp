#include "datapump/pattern_code.hpp"
#include "datapump/simulation_estimate.hpp"
#include "datapump/boundary_sync.hpp"
#include "datapump/channel.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/pattern_receiver.hpp"
#include "datapump/pattern_search.hpp"
#include "../src/receiver_probability.hpp"
#include "../src/estimate_cancellation.hpp"
#include <atomic>
#include <thread>
#include "../src/pattern_correlator_batch.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

using namespace datapump;
namespace {
void check(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
void near(double actual,double expected,const char* message) {
    check(std::abs(actual-expected)<=1e-9*std::max(1.,std::abs(expected)),message);
}
transfer::Estimate wire(std::size_t bits,const modem::Config& config) {
    transfer::Estimate value;value.wire_bits=bits;value.coded_bytes=(bits+7)/8;
    value.total_seconds=static_cast<double>(modem::training_sample_count(config)+
        2*modem::pattern_pulse_padding_samples(config)+modem::suppression_sample_count(config))/config.sample_rate+
        static_cast<double>(bits)*modem::symbol_sample_count(config)/config.sample_rate;
    return value;
}
modem::ChannelConfig clean_channel() {
    modem::ChannelConfig value;value.clock_error_ppm=0;value.phase_noise_degrees_per_sqrt_second=0;
    return value;
}

void advisory_cancellation() {
    using simulation::detail::ReceiverProbabilityParameters;
    using simulation::detail::receiver_probability;
    std::stop_source stopped;stopped.request_stop();
    const auto cancelled=[&](const auto& call) {
        bool caught=false;try {call();}catch(const estimate_detail::Cancelled&){caught=true;}
        check(caught,"cancelled advisory must abort explicitly, never return partial probability");
    };
    transfer::Options options;const auto draft=wire(1,options.modem);
    cancelled([&]{simulation::estimate(draft,options,true,clean_channel(),{},1,true,100,4096,
        simulation::ReceiverWorkMode::sampled_simulation,{},stopped.get_token());});
    ReceiverProbabilityParameters p;p.signal_energy=30;p.noise_dimensions=128;
    const auto completed=receiver_probability(p);
    cancelled([&]{receiver_probability(p,stopped.get_token());}); // Including a populated cache.
    std::stop_source live;
    const auto repeated=receiver_probability(p,live.get_token());
    check(completed.trials==4096&&repeated.trials==completed.trials&&
        completed.acquired_correct==repeated.acquired_correct&&completed.acquired_wrong==repeated.acquired_wrong&&
        completed.retained_correct==repeated.retained_correct&&completed.retained_wrong==repeated.retained_wrong&&
        completed.acquired_correct_lower==repeated.acquired_correct_lower&&
        completed.acquired_correct_upper==repeated.acquired_correct_upper,
        "a live cancellation token changed completed trials or probability statistics");
    p.differential_windows=4096;p.differential_window_seconds=1;p.seconds=4096;
    p.differential_weights.assign(4096,1./4096);p.differential_correlations.resize(4096);
    p.noise_dimensions=p.coherent_dimensions=p.section_dimensions=65536;
    p.frequency_step_hz=.25/4096;p.frequency_bin_min=-8;p.frequency_bin_max=8;
    std::atomic<bool> entered=false,aborted=false,returned=false;
    std::jthread worker([&](std::stop_token stop) {
        entered=true;
        try {receiver_probability(p,stop);returned=true;}catch(const estimate_detail::Cancelled&){aborted=true;}
    });
    while(!entered)std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto begin=std::chrono::steady_clock::now();worker.request_stop();worker.join();
    check(aborted&&!returned,"mid-calculation cancellation returned or cached a partial result");
    check(std::chrono::steady_clock::now()-begin<std::chrono::seconds(2),
        "probability calculation did not promptly honor cancellation");
    // The shared cache lock must also be interruptible, even if another caller owns it.
    execution::Mutex mutex;std::unique_lock held(mutex);
    entered=false;aborted=false;
    std::jthread waiter([&](std::stop_token stop) {
        entered=true;std::unique_lock pending(mutex,std::defer_lock);
        try {estimate_detail::lock(pending,stop);}catch(const estimate_detail::Cancelled&){aborted=true;}
    });
    while(!entered)std::this_thread::yield();
    waiter.request_stop();waiter.join();
    check(aborted,"waiting for an advisory cache lock ignored cancellation");
}

void probability_and_framing() {
    transfer::Options options;auto channel=clean_channel();
    auto value=wire(3,options.modem);
    channel.snr_db=-12;
    const auto weak=simulation::estimate(value,options,true,channel);
    channel.snr_db=12;
    const auto strong=simulation::estimate(value,options,true,channel);
    check(weak.confidence_available && weak.profile_matches,"matching nonempty draft must have an estimate");
    check(weak.success_probability>=0 && strong.success_probability<=1 &&
          strong.success_probability>weak.success_probability,"stronger signal must improve modeled success");
    channel.snr_db=-8;
    const auto short_result=simulation::estimate(value,options,true,channel);
    const auto long_result=simulation::estimate(wire(100,options.modem),options,true,channel);
    check(long_result.success_probability<short_result.success_probability,"full raw draft success must account for every bit");
    const auto raw_with_fec=simulation::estimate(value,options,true,channel);
    options.fec=FecMode::off;
    const auto raw_without_fec=simulation::estimate(value,options,true,channel);
    near(raw_with_fec.success_probability,raw_without_fec.success_probability,"saved FEC must never protect raw/short bits");
    value=wire(boundary_sync::marker_bits+boundary_sync::interval_bits,options.modem);
    value.coded_bytes=stream_interval_bytes;
    const auto unprotected=simulation::estimate(value,options,false,channel);
    options.fec=FecMode::rs60;
    const auto protected_result=simulation::estimate(value,options,false,channel);
    check(protected_result.success_probability>unprotected.success_probability,"fixed interval parity should improve modeled correction");
    auto mismatched=options.modem;mismatched.spreading_factor*=2;
    const auto no_profile=simulation::estimate(value,options,false,channel,std::span(&mismatched,1));
    check(!no_profile.profile_matches && !no_profile.confidence_available && no_profile.success_probability==0,
          "incompatible RX profile cannot show successful reception");
    check(!simulation::estimate(wire(0,options.modem),options,true,channel).confidence_available,
          "empty draft must not claim successful reception");
}
void workload_and_impairments() {
    transfer::Options options;auto channel=clean_channel();channel.snr_db=-4;
    const auto value=wire(3,options.modem);
    const auto base=simulation::estimate(value,options,true,channel);
    const auto repeat=simulation::estimate(value,options,true,channel);
    near(base.cpu_seconds,repeat.cpu_seconds,"reference CPU estimate must be deterministic");
    near(base.gpu_seconds,repeat.gpu_seconds,"reference GPU estimate must be deterministic");
    check(base.cpu_seconds>0 && base.gpu_seconds>0 && base.gpu_hypothetical,"GPU estimate must remain a hypothetical positive projection");
    const auto longer=simulation::estimate(wire(100,options.modem),options,true,channel);
    check(longer.cpu_seconds>base.cpu_seconds && longer.gpu_seconds>base.gpu_seconds,"sampled draft length must increase both time estimates");
    auto other=options.modem;other.spreading_factor*=2;
    const std::array profiles{options.modem,other};
    const auto bank=simulation::estimate(value,options,true,channel,profiles);
    check(bank.receiver_profiles==2 && bank.cpu_seconds>base.cpu_seconds,"each receive profile adds search work");
    const auto keys=simulation::estimate(value,options,true,channel,{},3);
    check(keys.cpu_seconds>base.cpu_seconds,"additional receive keys add search work");
    near(keys.success_probability,base.success_probability,"unrelated key banks must not change per-receiver admission confidence");
    near(bank.success_probability,base.success_probability,"unrelated waveform profiles must not change matching receiver confidence");
    const auto nominal_db=channel.snr_db+10*std::log10(modem::symbol_sample_count(options.modem)/2.)-3;
    near(base.modeled_symbol_snr_db,nominal_db,"sample SNR must use Fs/2 noise bandwidth and implementation margin");
    channel.phase_noise_degrees_per_sqrt_second=180;
    const auto impaired=simulation::estimate(value,options,true,channel);
    check(impaired.modeled_symbol_snr_db<base.modeled_symbol_snr_db &&
          impaired.success_probability<base.success_probability,"phase diffusion must reduce model confidence");
}
void receiver_cpu_budget() {
    transfer::Options options;auto channel=clean_channel();
    const auto draft=wire(3,options.modem);
    const auto base=simulation::estimate(draft,options,true,channel);
    const auto repeat=simulation::estimate(draft,options,true,channel);
    check(base.receiver_cpu_seconds>0 && base.receiver_cpu_seconds<base.cpu_seconds,
          "receiver workload must exclude the synthetic channel while retaining positive receive work");
    near(base.receiver_cpu_seconds,repeat.receiver_cpu_seconds,
         "receiver CPU estimate must be deterministic");
    const auto synthetic_channel=base.cpu_seconds-base.receiver_cpu_seconds;
    const auto longer=simulation::estimate(wire(100,options.modem),options,true,channel);
    check(longer.receiver_cpu_seconds>base.receiver_cpu_seconds,
          "a longer reception must increase the receiver-only workload");
    near((longer.cpu_seconds-longer.receiver_cpu_seconds)/synthetic_channel,
         longer.simulated_seconds/base.simulated_seconds,
         "excluded synthetic-channel work must scale with generated audio duration");
    auto other=options.modem;other.spreading_factor*=2;
    const std::array profiles{options.modem,other};
    const auto bank=simulation::estimate(draft,options,true,channel,profiles);
    const auto keys=simulation::estimate(draft,options,true,channel,{},3);
    for(const auto* larger:{&bank,&keys}) {
        check(larger->receiver_cpu_seconds>base.receiver_cpu_seconds,
              "additional receive profiles or keys must increase receiver-only CPU work");
        near(larger->cpu_seconds-larger->receiver_cpu_seconds,synthetic_channel,
             "additional receiver banks must not add another synthetic channel");
    }
    check(base.simulated_seconds>draft.total_seconds &&
          std::isfinite(base.receiver_cpu_seconds/base.simulated_seconds),
          "CPU pace must include observed-absence audio and have a finite workload ratio");
}
void received_processing_budget() {
    transfer::Options options;const auto channel=clean_channel();
    const auto check_components=[](const simulation::Estimate& value) {
        check(std::isfinite(value.payload_processing_seconds)&&value.payload_processing_seconds>=0&&
              std::isfinite(value.mitigation_seconds)&&value.mitigation_seconds>=0&&
              value.mitigation_seconds<=value.payload_processing_seconds,
              "mitigation work must be a finite nonnegative subset of received payload processing");
        check(value.cpu_seconds>=value.payload_processing_seconds&&
              value.receiver_cpu_seconds>=value.payload_processing_seconds&&
              value.gpu_seconds>=value.payload_processing_seconds,
              "all execution projections must retain the CPU payload-processing allowance");
    };
    const auto draft=wire(3,options.modem);
    const auto base=simulation::estimate(draft,options,true,channel,{},1,false);
    check_components(base);
    check(base.payload_processing_seconds>0&&base.mitigation_seconds>0,
          "eligible raw reception must include bounded interpretation and its mitigation work");
    auto same_bits=draft;same_bits.content_bytes=transfer::short_message_bytes;
    const auto different_source=simulation::estimate(same_bits,options,true,channel,{},1,false);
    near(different_source.payload_processing_seconds,base.payload_processing_seconds,
         "raw interpretation must depend on received bits, not transmitter source-byte count");
    near(different_source.mitigation_seconds,base.mitigation_seconds,
         "identical raw and dictionary wire bits must receive the same mitigation allowance");
    const auto empty=simulation::estimate(wire(0,options.modem),options,true,channel,{},1,false);
    near(empty.payload_processing_seconds,0,"empty reception must not invent payload processing");
    near(empty.mitigation_seconds,0,"empty reception must not invent payload mitigations");
    auto other=options.modem;other.spreading_factor*=2;
    const auto unmatched=simulation::estimate(draft,options,true,channel,std::span(&other,1),1,false);
    near(unmatched.payload_processing_seconds,0,"unmatched receiver must not decode a desired payload");
    near(unmatched.mitigation_seconds,0,"unmatched receiver must not invent desired payload mitigations");
    const std::array profiles{options.modem,other};
    const auto bank=simulation::estimate(draft,options,true,channel,profiles,1,false);
    const auto keys=simulation::estimate(draft,options,true,channel,{},3,false);
    for(const auto* value:{&bank,&keys}) {
        check_components(*value);
        near(value->payload_processing_seconds,base.payload_processing_seconds,
             "unrelated search banks must not duplicate received payload processing");
        near(value->mitigation_seconds,base.mitigation_seconds,
             "unrelated search banks must not duplicate received payload mitigations");
    }
    // Explicit bits and dictionary text share receiver interpretation. Saved
    // interval settings do not add interval work to either raw representation.
    for(const auto count:{std::size_t{1},transfer::short_message_bits,transfer::short_message_bits+1,std::size_t{8192}}) {
        const auto raw=wire(count,options.modem);
        const auto reference=simulation::estimate(raw,options,true,channel,{},1,false);
        check_components(reference);
        check(reference.mitigation_seconds>0,
              "raw views retain a validation boundary even beyond short dictionary eligibility");
        for(const auto fec:{FecMode::off,FecMode::rs20,FecMode::rs60})for(const bool compression:{false,true}) {
            auto saved=options;saved.fec=fec;saved.compression=compression;saved.key.emplace(Bytes(32,0x37));
            const auto changed=simulation::estimate(raw,saved,true,channel,{},1,false);
            near(changed.payload_processing_seconds,reference.payload_processing_seconds,
                 "saved interval FEC, compression and authentication must not add work to raw bits");
            near(changed.mitigation_seconds,reference.mitigation_seconds,
                 "saved interval settings must not add mitigation overhead to raw bits");
        }
    }

    auto interval=wire(boundary_sync::marker_bits+boundary_sync::interval_bits,options.modem);
    interval.coded_bytes=stream_interval_bytes;interval.content_bytes=32;
    options.compression=false;options.fec=FecMode::off;
    const auto plain=simulation::estimate(interval,options,false,channel,{},1,false);
    options.fec=FecMode::rs20;
    const auto light=simulation::estimate(interval,options,false,channel,{},1,false);
    options.fec=FecMode::rs60;
    const auto corrected=simulation::estimate(interval,options,false,channel,{},1,false);
    check(plain.payload_processing_seconds<light.payload_processing_seconds&&
          light.payload_processing_seconds<corrected.payload_processing_seconds,
          "interval processing must account for the configured parity workload");
    auto multiple=wire(4*(boundary_sync::marker_bits+boundary_sync::interval_bits),options.modem);
    multiple.coded_bytes=4*stream_interval_bytes;multiple.content_bytes=interval.content_bytes;
    const auto more_intervals=simulation::estimate(multiple,options,false,channel,{},1,false);
    check(more_intervals.payload_processing_seconds>corrected.payload_processing_seconds&&
          more_intervals.mitigation_seconds>corrected.mitigation_seconds,
          "more fixed intervals must increase processing and mitigation allowances");
    options.key.emplace(Bytes(32,0x37));
    const auto authenticated=simulation::estimate(interval,options,false,channel,{},1,false);
    check(authenticated.payload_processing_seconds>corrected.payload_processing_seconds,
          "keyed intervals must account for authentication work");
    options.key.reset();
    interval.content_bytes=4096;
    const auto uncompressed=simulation::estimate(interval,options,false,channel);
    options.compression=true;
    const auto compressed=simulation::estimate(interval,options,false,channel);
    auto expanded=interval;expanded.content_bytes*=2;
    const auto more_source=simulation::estimate(expanded,options,false,channel,{},1,false);
    auto literal_options=options;literal_options.compression=false;
    const auto more_literal=simulation::estimate(expanded,literal_options,false,channel,{},1,false);
    check(more_source.payload_processing_seconds>compressed.payload_processing_seconds,
          "decompression processing must scale with the estimated original source size");
    check(more_source.payload_processing_seconds-more_literal.payload_processing_seconds>
          compressed.payload_processing_seconds-uncompressed.payload_processing_seconds&&
          more_source.mitigation_seconds>compressed.mitigation_seconds,
          "decoder and mitigation work must scale with expanded source independently of the shared copy cost");
    for(const auto* value:{&plain,&light,&corrected,&more_intervals,&authenticated,&uncompressed,&compressed,&more_source})
        check_components(*value);
    const auto processing_delta=compressed.payload_processing_seconds-uncompressed.payload_processing_seconds;
    check(processing_delta>0,"compressed source interpretation must add decoder work");
    near(compressed.cpu_seconds-uncompressed.cpu_seconds,processing_delta,
         "CPU projection must charge source processing exactly once");
    near(compressed.receiver_cpu_seconds-uncompressed.receiver_cpu_seconds,processing_delta,
         "receiver headroom must include the same source processing allowance");
    near(compressed.gpu_seconds-uncompressed.gpu_seconds,processing_delta,
         "hypothetical GPU projection must retain CPU source processing without acceleration");
    near(compressed.cpu_seconds-compressed.receiver_cpu_seconds,
         uncompressed.cpu_seconds-uncompressed.receiver_cpu_seconds,
         "payload processing must not be charged to synthetic channel generation");
    near(compressed.tracking_seconds,uncompressed.tracking_seconds,
         "payload mitigation allowance must not rescale unchanged modem tracking");
    near(compressed.simulated_seconds,uncompressed.simulated_seconds,
         "CPU mitigation allowance must not alter represented audio duration");
    near(compressed.success_probability,uncompressed.success_probability,
         "CPU mitigation allowance must not alter the conditional receive probability");
}
void whole_symbol_phase_coherence() {
    transfer::Options options;
    options.modem=tuning::resolve(1,0,tuning::PatternMode::auto_pattern,false,1500).config;
    options.modem.integration_seconds=86400;
    options.dsp_workspace_bytes=std::size_t{16}*1024*1024*1024;
    auto channel=clean_channel();
    channel.snr_db=30-10*std::log10(static_cast<double>(modem::symbol_sample_count(options.modem))/2.);
    const auto clean=simulation::estimate(wire(1,options.modem),options,true,channel);
    near(clean.phase_coherence_loss_db,0,"zero phase diffusion must have zero coherent loss");
    // For Brownian phase whose correlation falls to 1/e across the whole
    // symbol, the integrated power fraction is independently 2/e.
    channel.phase_noise_degrees_per_sqrt_second=180/std::numbers::pi*std::sqrt(2./86400);
    const auto one_exponent=simulation::estimate(wire(1,options.modem),options,true,channel);
    near(one_exponent.phase_coherence_loss_db,10*std::log10(std::exp(1.)/2),
         "whole-symbol phase loss must agree with the independent 2/e reference");
    near(clean.modeled_symbol_snr_db-one_exponent.modeled_symbol_snr_db,one_exponent.phase_coherence_loss_db,
         "exposed phase penalty must be the same loss already used by receive confidence");
    for(const auto rate:{.01,.02,.1,1.}) {
        options.modem=tuning::resolve(rate,0,tuning::PatternMode::auto_pattern,false,1500).config;
        options.modem.integration_seconds=86400;
        const auto cadence=simulation::estimate(wire(1,options.modem),options,true,channel);
        near(cadence.phase_coherence_loss_db,one_exponent.phase_coherence_loss_db,
             "known chip reversals across 0.01 Hz cannot reset whole-symbol phase drift");
    }
    options.modem.integration_seconds=3000000;
    channel.snr_db=30-10*std::log10(static_cast<double>(modem::symbol_sample_count(options.modem))/2.);
    channel.phase_noise_degrees_per_sqrt_second=0;
    const auto long_clean=simulation::estimate(wire(1,options.modem),options,true,channel);
    double previous_loss=0;
    for(const auto diffusion:{.005,.05,.5}) {
        channel.phase_noise_degrees_per_sqrt_second=diffusion;
        const auto gpsdo=simulation::estimate(wire(1,options.modem),options,true,channel);
        check(!gpsdo.confidence_available&&gpsdo.differential_windows>=256&&
              gpsdo.phase_coherence_loss_db>previous_loss,
              "GPSDO phase models must retain progressively greater loss over a long coherent symbol");
        previous_loss=gpsdo.phase_coherence_loss_db;
        if(diffusion==.5)check(gpsdo.phase_coherence_loss_db>17&&
                gpsdo.modeled_symbol_snr_db<long_clean.modeled_symbol_snr_db,
                "GPSDO XO phase drift must still reduce coherent energy when differential confidence is unavailable");
    }
    options.dsp_workspace_bytes=1024;
    const auto unsupported=simulation::estimate(wire(1,options.modem),options,true,channel);
    check(!unsupported.confidence_available&&unsupported.phase_coherence_loss_db>17,
          "phase loss should remain visible when insufficient RAM prevents a numeric receive estimate");
}

void partial_compact_probability() {
    transfer::Options options;
    options.modem=tuning::resolve(.1,-38,tuning::PatternMode::auto_keystream,true,.05).config;
    options.timestamp=1800000000;options.search_seconds=6;
    options.dsp_workspace_bytes=std::size_t{512}*1024*1024;
    options.modem.scramble=true;
    for(std::size_t i=0;i<options.modem.spreading_seed.size();++i)
        options.modem.spreading_seed[i]=static_cast<std::uint8_t>(37*i+11);
    options.modem.stream_epoch=options.timestamp;
    modem::OscillatorSearchConfig oscillator;
    oscillator.lf=oscillator.rf=tuning::oscillator_model(tuning::parse_oscillator_preset("gpsdo-xo"));
    options.modem.oscillator_search=oscillator;
    auto channel=clean_channel();
    channel.phase_noise_degrees_per_sqrt_second=oscillator.lf.phase_noise_degrees_per_sqrt_second;
    channel.snr_db=36.020599913279625-200-(-164)-10*std::log10(options.modem.sample_rate/2.);
    check(options.modem.sample_rate==64&&modem::pattern_chip_samples(options.modem)==1280&&
          modem::symbol_sample_count(options.modem)==25478859,
          "partial probability reproduction resolved a different physical waveform");
    const auto value=simulation::estimate(wire(1,options.modem),options,true,channel,{},1,true,100,256);
    check(value.differential_windows==1244&&value.differential_window_seconds==320&&
          value.receiver_workspace_supported&&value.one_bit_confidence_available&&value.confidence_available&&
          value.differential_model_available&&value.probability_trials==256&&value.probability_interval_available&&
          value.pulse_segment_projection_modeled&&value.pulse_projection_modeled,
          "bounded compact partial windows must retain the complete detector probability model");
    check(std::isfinite(value.success_probability)&&value.success_probability>=0&&value.success_probability<=1&&
          std::isfinite(value.modeled_symbol_snr_db),"partial model produced an invalid probability or energy diagnostic");
    const auto draft=simulation::estimate(wire(3,options.modem),options,true,channel,{},1,true,100,256);
    check(!draft.confidence_available&&draft.one_bit_confidence_available&&
          draft.probability_model_limit.find("timing ownership")!=std::string::npos,
          "partial covariance support must not invent compact continuation timing ownership");
    std::cout<<"partial compact probability "<<value.success_probability<<", 256 trials, modeled SNR "
             <<value.modeled_symbol_snr_db<<" dB\n";
    auto boundary=options;
    boundary.modem=tuning::resolve(.1,-37.1545,tuning::PatternMode::auto_keystream,true,.05).config;
    boundary.modem.oscillator_search=oscillator;boundary.modem.stream_epoch=options.timestamp;
    boundary.modem.spreading_seed=options.modem.spreading_seed;
    const auto edge=simulation::estimate(wire(1,boundary.modem),boundary,true,channel,{},1,true,100,256);
    check(modem::symbol_sample_count(boundary.modem)%(4*modem::pattern_chip_samples(boundary.modem))!=0&&
          edge.confidence_available&&edge.one_bit_confidence_available&&edge.pulse_segment_projection_modeled,
          "the automatic integration boundary must retain its short final atom and complete-symbol model");
    boundary.modem=tuning::resolve(.1,-37,tuning::PatternMode::auto_keystream,true,.05).config;
    boundary.modem.oscillator_search=oscillator;boundary.modem.stream_epoch=options.timestamp;
    boundary.modem.spreading_seed=options.modem.spreading_seed;
    const auto aligned=simulation::estimate(wire(1,boundary.modem),boundary,true,channel,{},1,true,100,256);
    check(aligned.confidence_available&&aligned.one_bit_confidence_available&&aligned.differential_model_available&&
          aligned.pulse_projection_modeled&&!aligned.pulse_segment_projection_modeled,
          "aligned noncircular templates must retry a pristine real-covariance model");
}

void differential_model_limits() {
    transfer::Options options;
    options.modem.sample_rate=256;options.modem.carrier_hz=64;
    options.modem.bandwidth_hz=8;options.modem.pulse_shaping=false;
    options.modem.integration_seconds=25500;
    options.search_seconds=0;
    options.dsp_workspace_bytes=std::size_t{16}*1024*1024*1024;
    auto channel=clean_channel();channel.snr_db=-20;
    const auto below=simulation::estimate(wire(1,options.modem),options,true,channel);
    check(below.differential_windows==0 && below.differential_window_seconds==0 &&
          below.confidence_available && below.drift_model_available,
          "fewer than 256 complete default local windows must retain the existing four-quarter model");
    options.modem.integration_seconds=25600-1./256;
    const auto incomplete=simulation::estimate(wire(1,options.modem),options,true,channel,{},1,false);
    check(incomplete.differential_windows==0,
          "one missing sample cannot complete the 256th local differential window");
    options.modem.integration_seconds=25600;
    const auto eligible=simulation::estimate(wire(1,options.modem),options,true,channel);
    check(eligible.differential_windows==256 && eligible.differential_window_seconds==100,
          "default differential model coverage must start at 256 complete hundred-second windows");
    check(eligible.profile_matches && eligible.carrier_in_search && eligible.receiver_workspace_supported &&
          eligible.confidence_available && eligible.drift_model_available && eligible.differential_model_available &&
          !eligible.coherent_reference_only && eligible.probability_trials==4096 &&
          eligible.probability_interval_available && eligible.success_probability>.99 &&
          eligible.success_probability_low<eligible.success_probability && eligible.success_probability_high<=1,
          "eligible differential geometry must sample all detector branches and expose finite-trial uncertainty");
    check(eligible.cpu_seconds>10*below.cpu_seconds && eligible.tracking_seconds>below.tracking_seconds &&
          std::isfinite(eligible.receiver_cpu_seconds) && std::isfinite(eligible.gpu_seconds),
          "many-window FFT scoring and tracking must appear in the finite work estimates");
    channel.phase_noise_degrees_per_sqrt_second=.5;
    const auto drifting=simulation::estimate(wire(1,options.modem),options,true,channel);
    check(drifting.confidence_available && drifting.differential_model_available && drifting.phase_coherence_loss_db>0 &&
          drifting.section_phase_coherence_loss_db<drifting.phase_coherence_loss_db,
          "joint differential probability must preserve coherent and quarter phase diagnostics");
    const auto support=simulation::estimate(wire(1,options.modem),options,true,channel,{},1,false);
    near(support.cpu_seconds,drifting.cpu_seconds,"support-only planning must include identical differential work");
    check(!support.confidence_available && support.probability_trials==0,
          "support-only planning must not run probability trials");
    const auto disabled=simulation::estimate(wire(1,options.modem),options,true,channel,{},1,true,0);
    check(disabled.confidence_available && !disabled.differential_model_available && disabled.differential_windows==0,
          "an explicitly disabled receiver-local detector must use the corresponding older model");
    const auto curve=simulation::estimate(wire(1,options.modem),options,true,channel,{},1,true,100,512);
    check(curve.confidence_available && curve.probability_trials==512 &&
          curve.success_probability_low<drifting.success_probability_low,
          "reduced curve sampling must retain its actual trial count and wider sampling interval");
    auto compact_options=options;
    compact_options.modem.sample_rate=64;compact_options.modem.carrier_hz=16;
    compact_options.modem.bandwidth_hz=32;compact_options.modem.integration_seconds=512;
    compact_options.dsp_workspace_bytes=8*1024*1024;
    const auto compact=simulation::estimate(wire(3,compact_options.modem),compact_options,true,channel,{},1,true,1);
    check(!compact.confidence_available && compact.one_bit_confidence_available && compact.differential_model_available &&
          !compact.probability_interval_available && compact.probability_model_limit.find("timing ownership")!=std::string::npos,
          "unmodeled compact timing ownership must suppress draft confidence while retaining the modeled first bit");
    options.modem.integration_seconds+=99;
    const auto partial=simulation::estimate(wire(1,options.modem),options,true,channel,{},1,false);
    check(partial.differential_windows==256,
          "an incomplete final local window must not count as a complete differential observation");
    const auto unsupported=simulation::estimate(wire(1,options.modem),options,true,channel);
    check(!unsupported.confidence_available && !unsupported.probability_model_limit.empty() &&
          unsupported.probability_trials==0,
          "unsupported partial geometry must state the limit instead of reporting another detector's probability");
    options.modem.bandwidth_hz=.01;options.modem.integration_seconds=256*3200;
    const auto sparse=simulation::estimate(wire(1,options.modem),options,true,channel,{},1,false);
    check(sparse.differential_windows==256 && sparse.differential_window_seconds==3200,
          "very narrow profiles must extend the local duration to at least sixteen whole chips");
    options.modem.spreading_mode=modem::SpreadingMode::tone;
    const auto tone=simulation::estimate(wire(1,options.modem),options,true,channel,{},1,false);
    check(tone.differential_windows==0 && tone.differential_window_seconds==0,
          "tone profiles must not acquire differential pattern work or eligibility");
}
void drift_receiver_estimate() {
    transfer::Options options;
    options.modem.sample_rate=256;
    options.modem.carrier_hz=64;
    options.modem.bandwidth_hz=8;
    options.modem.integration_seconds=16;
    options.modem.pulse_shaping=false;
    options.search_seconds=0;
    options.dsp_workspace_bytes=std::size_t{1024}*1024*1024;
    auto channel=clean_channel();
    channel.snr_db=20-10*std::log10(2048.);
    const auto value=simulation::estimate(wire(1,options.modem),options,true,channel);
    check(value.confidence_available && value.drift_model_available && !value.coherent_reference_only && value.drift_sections==4,
          "an affordable sixteen-second pattern with sixteen complete chips per quarter models both receiver branches");
    near(value.drift_section_seconds,4,"four-section diagnostic must use the actual quarter duration");
    near(value.section_phase_coherence_loss_db,0,"stable phase must have no section phase loss");
    modem::PatternCode finite_code(options.modem,options.modem.stream_epoch);
    double finite_power=0;
    for(std::uint64_t i=0;i<finite_code.chips_per_symbol();++i)finite_power+=std::norm(finite_code.value(i,0));
    finite_power/=finite_code.chips_per_symbol();
    near(value.modeled_symbol_snr_db,20+10*std::log10(finite_power),
         "the joint statistic must retain actual finite-template power without an arbitrary three-dB margin");
    const auto repeated=simulation::estimate(wire(1,options.modem),options,true,channel);
    near(value.success_probability,repeated.success_probability,"fixed statistical draws must make receive estimates repeatable");
    const auto support=simulation::estimate(wire(1,options.modem),options,true,channel,{},1,false);
    check(support.profile_matches && support.carrier_in_search && support.receiver_workspace_supported &&
          !support.confidence_available && !support.drift_model_available && support.success_probability==0,
          "coverage-only planning must skip numeric confidence while retaining the actual search and RAM checks");
    near(support.cpu_seconds,value.cpu_seconds,"skipping probability trials must not change receiver compute estimates");

    channel.phase_noise_degrees_per_sqrt_second=180/std::numbers::pi*std::sqrt(2./16);
    const auto drift=simulation::estimate(wire(1,options.modem),options,true,channel);
    const auto quarter_fraction=8*(1+4*std::expm1(-.25));
    near(drift.section_phase_coherence_loss_db,-10*std::log10(quarter_fraction),
         "section phase diagnostic must integrate phase diffusion over four seconds, not the whole sixteen seconds");
    near(drift.phase_coherence_loss_db,10*std::log10(std::exp(1.)/2),
         "the whole-symbol diagnostic must retain its independent coherent phase penalty");
    near(value.modeled_symbol_snr_db-drift.modeled_symbol_snr_db,drift.phase_coherence_loss_db,
         "the coherent energy diagnostic must use the exact joint mean, separately from the sampled success distribution");
    check(drift.success_probability<value.success_probability &&
          drift.section_phase_coherence_loss_db<drift.phase_coherence_loss_db,
          "section diagnostics should show improved phase tolerance while finite drift still costs receive confidence");

    options.modem.integration_seconds=16-1./256;
    const auto short_symbol=simulation::estimate(wire(1,options.modem),options,true,channel);
    check(!short_symbol.coherent_reference_only && !short_symbol.drift_model_available && short_symbol.drift_sections==1,
          "the sub-sixteen-second boundary must retain the original detector model");
    near(short_symbol.section_phase_coherence_loss_db,short_symbol.phase_coherence_loss_db,
         "one-section diagnostic must equal the ordinary whole-symbol loss");
    options.modem.integration_seconds=16;
    options.modem.bandwidth_hz=7.9;
    const auto sparse=simulation::estimate(wire(1,options.modem),options,true,channel);
    check(!sparse.coherent_reference_only && !sparse.drift_model_available && sparse.drift_sections==1,
          "a quarter with fewer than sixteen complete chips cannot claim the section detector");
    options.modem.bandwidth_hz=8;
    options.modem.spreading_mode=modem::SpreadingMode::tone;
    const auto tone=simulation::estimate(wire(1,options.modem),options,true,channel);
    check(!tone.coherent_reference_only && !tone.drift_model_available && tone.drift_sections==1,
          "tone reception must keep its existing coherent estimate");

    options.modem.spreading_mode=modem::SpreadingMode::pattern;
    options.modem.sample_rate=128;options.modem.carrier_hz=4;
    options.modem.bandwidth_hz=4;options.modem.integration_seconds=64;
    options.modem.scramble=true;options.dsp_workspace_bytes=1024*1024;
    channel=clean_channel();channel.snr_db=-10;
    const auto fallback=simulation::estimate(wire(1,options.modem),options,true,channel);
    check(fallback.confidence_available && fallback.coherent_reference_only && !fallback.drift_model_available &&
          fallback.drift_sections==4 && fallback.receiver_workspace_supported,
          "tight compact workspace must label the limited coherent model instead of crediting unallocated section state");
}
void receiver_statistic_controls() {
    using simulation::detail::ReceiverProbabilityParameters;
    using simulation::detail::receiver_probability;
    ReceiverProbabilityParameters parameters;
    parameters.seconds=16;parameters.signal_energy=40;
    parameters.noise_dimensions=parameters.coherent_dimensions=parameters.section_dimensions=4096;
    parameters.acquisition_threshold=parameters.continuation_threshold=40;
    const auto stable=receiver_probability(parameters);
    check(stable.trials>=1024 && stable.acquired_correct>0 && stable.acquired_correct<1,
          "stable comparison must exercise the receiver probability transition");
    check(stable.acquired_correct<stable.coherent_acquired_correct,
          "stable patterns must pay the extra detector-choice penalty instead of gaining a free branch");
    auto searched=parameters;
    searched.diffusion_degrees=30;searched.signal_energy=60;
    const auto fixed_carrier=receiver_probability(searched);
    searched.frequency_step_hz=.25/searched.seconds;
    searched.frequency_bin_min=-2;searched.frequency_bin_max=2;
    const auto searched_carrier=receiver_probability(searched);
    check(searched_carrier.acquired_correct>fixed_carrier.acquired_correct+.02,
          "a finite carrier bank should recover some realized linear phase wander without perfect phase tracking");
    parameters.sections=false;
    const auto coherent=receiver_probability(parameters);
    near(coherent.acquired_correct,coherent.coherent_acquired_correct,
         "disabled section scoring must coincide with its coherent comparison on shared observations");

    parameters.sections=true;parameters.signal_energy=400;parameters.diffusion_degrees=90;
    const auto drift=receiver_probability(parameters);
    check(drift.acquired_correct>drift.coherent_acquired_correct+.03,
          "independent section phases should improve a sufficiently energetic drifting full-bit match");

    // This extreme duration must not retain the 1/node_count energy floor
    // produced by simply sampling a fixed number of independent phasors.
    parameters.seconds=1e12;parameters.signal_energy=1e8;
    parameters.noise_dimensions=parameters.coherent_dimensions=parameters.section_dimensions=1e9;
    const auto incoherent=receiver_probability(parameters);
    check(std::isfinite(incoherent.acquired_correct) && incoherent.acquired_correct<.005 &&
          incoherent.coherent_acquired_correct<.005,
          "days-long and longer phase diffusion must not invent coherence from the finite quadrature grid");

    parameters={};parameters.seconds=16;parameters.signal_energy=1e8;
    parameters.timing_coherence=.01;
    const auto mismatched=receiver_probability(parameters);
    check(mismatched.acquired_correct<.005,
          "unfitted high-power signal must remain in whole-symbol energy and impose an evidence ceiling");

    parameters.timing_coherence=1;parameters.signal_energy=100;
    parameters.correlations.fill(.999999);
    const auto indistinguishable=receiver_probability(parameters);
    check(indistinguishable.acquired_correct+indistinguishable.acquired_wrong<.01,
          "nearly identical bit templates cannot acquire confidence from independently invented noise");

    parameters.correlations.fill({0,.999999});
    const auto quadrature_copy=receiver_probability(parameters);
    check(quadrature_copy.acquired_correct+quadrature_copy.acquired_wrong<.01,
          "templates differing only by unknown complex phase must remain indistinguishable");
    auto unequal=parameters;unequal.sections=false;unequal.signal_energy=1000;
    unequal.correlations.fill(.999999);
    unequal.alternative_weights=std::array{.01,.01,.01,.97};
    check(receiver_probability(unequal).acquired_correct>.99,
          "coherent alternative fits must use their own section energy envelope");
    for(unsigned invalid=0;invalid<4;++invalid) {
        auto bad=unequal;
        if(invalid==0)bad.correlations[0]={0,std::numeric_limits<double>::quiet_NaN()};
        if(invalid==1)bad.correlations[0]={.8,.8};
        if(invalid==2)(*bad.alternative_weights)[0]=0;
        if(invalid==3)(*bad.alternative_weights)[0]=.5;
        bool rejected=false;try {(void)receiver_probability(bad);}catch(const Error&){rejected=true;}
        check(rejected,"invalid complex correlation or alternative weights accepted");
    }
    parameters.correlations.fill(0);
    const auto full_rank=receiver_probability(parameters);
    parameters.coherent_dimensions=parameters.section_dimensions=32;
    const auto capped_rank=receiver_probability(parameters);
    check(capped_rank.acquired_correct<full_rank.acquired_correct-.2,
          "reducing evidence dimensions must retain the larger physical-noise denominator");
}
void raw_sample_probability_geometry() {
    transfer::Options options;
    options.modem.sample_rate=128;options.modem.carrier_hz=32;
    options.modem.bandwidth_hz=8;options.modem.integration_seconds=16;
    options.modem.pulse_shaping=false;options.search_seconds=0;
    options.dsp_workspace_bytes=8*1024*1024;
    auto channel=clean_channel();channel.phase_noise_degrees_per_sqrt_second=30;
    channel.snr_db=26-10*std::log10(1024.);
    const auto projected=simulation::estimate(wire(1,options.modem),options,true,channel);
    // One additional sample changes gcd(symbol,chip) to1. The receiver must
    // fit raw real samples while retaining its conservative held-chip score
    // scale:1024.5 complex-equivalent noise dimensions,128.0625 score dimensions.
    // Integer quarters still contain16 complete chips, so both branches apply.
    options.modem.integration_seconds=2049./128;
    channel.snr_db=26-10*std::log10(1024.5);
    const auto raw_sample=simulation::estimate(wire(1,options.modem),options,true,channel);
    check(projected.drift_model_available && raw_sample.drift_model_available &&
          raw_sample.receiver_workspace_supported && raw_sample.carrier_in_search,
          "the sample-quantized gcd edge must retain eligible section scoring and finite coverage");
    check(projected.success_probability>.8 && raw_sample.success_probability<.25,
          "raw-sample scoring must retain its physical-noise denominator and larger finite start search");
}
void established_tracking_workload() {
    transfer::Options options;
    options.modem=tuning::resolve(1200,-10,tuning::PatternMode::auto_pattern,false).config;
    options.dsp_workspace_bytes=std::size_t{1024}*1024*1024;
    options.timestamp=1800000000;
    auto channel=clean_channel();channel.snr_db=-40;
    const auto one=simulation::estimate(wire(1,options.modem),options,true,channel);
    const auto three=simulation::estimate(wire(3,options.modem),options,true,channel);
    const auto ten=simulation::estimate(wire(10,options.modem),options,true,channel);
    check(three.receiver_workspace_supported && three.tracking_seconds>0,
          "wide long-symbol FFT planning must include established-stream tracking");
    near(one.tracking_symbol_windows,1,"one long bit requires tracking its fully observed absent symbol");
    near(three.tracking_symbol_windows,3,"three long bits need two continuation windows and one absent window");
    near(ten.tracking_symbol_windows,10,"tracking must follow exact payload symbols without byte padding");
    near(three.tracking_seconds,3*one.tracking_seconds,"each whole-symbol continuation must add the same modeled work");
    near(ten.tracking_seconds,10*one.tracking_seconds,"tracking work must scale with exact wire-bit count");
    check(three.cpu_seconds>three.tracking_seconds && three.gpu_seconds>three.tracking_seconds &&
          three.receiver_cpu_seconds>three.tracking_seconds,
          "serial continuation must be included in receiver-only CPU, simulation CPU and hypothetical GPU totals");
    check(ten.tracking_seconds>three.tracking_seconds && ten.cpu_seconds>three.cpu_seconds &&
          ten.gpu_seconds>three.gpu_seconds,"longer payloads must increase tracking and both total estimates");

    const auto keys=simulation::estimate(wire(3,options.modem),options,true,channel,{},3);
    near(keys.tracking_seconds,three.tracking_seconds,"unrelated key banks must not multiply established signal streams");
    near(keys.tracking_symbol_windows,three.tracking_symbol_windows,"key search must not duplicate desired-stream absence");
    auto other=options.modem;other.integration_seconds*=2;
    const std::array profiles{options.modem,other};
    const auto extra_profile=simulation::estimate(wire(3,options.modem),options,true,channel,profiles);
    near(extra_profile.tracking_seconds,three.tracking_seconds,"incompatible profiles add search but not desired-stream tracking");
    const auto no_profile=simulation::estimate(wire(3,options.modem),options,true,channel,std::span(&other,1));
    near(no_profile.tracking_seconds,0,"no matching receiver profile must not invent an established signal stream");
    const auto empty=simulation::estimate(wire(0,options.modem),options,true,channel);
    near(empty.tracking_seconds,0,"an empty draft must not invent a tracking or completion workload");
    near(empty.tracking_symbol_windows,0,"an empty draft has no established stream windows");

    options.modem.integration_seconds=1;
    const auto short_symbols=simulation::estimate(wire(3,options.modem),options,true,channel);
    near(short_symbols.tracking_symbol_windows,8,
         "one-second symbols require two remaining payload windows plus six complete absent windows");
    options.dsp_workspace_bytes=64*1024;
    const auto correlator=simulation::estimate(wire(3,options.modem),options,true,channel);
    near(correlator.tracking_seconds,0,"continuous correlator lane work must not gain duplicate FFT tracking costs");
}
void complete_symbol_absence() {
    transfer::Options options;options.modem.integration_seconds=4*60*60;
    auto channel=clean_channel();
    const auto value=wire(1,options.modem);
    const auto result=simulation::estimate(value,options,true,channel);
    const auto tail=result.simulated_seconds-value.total_seconds;
    const auto required=4*60*60+1+.175+2.*modem::pattern_pulse_padding_samples(options.modem)/options.modem.sample_rate;
    near(tail,required,"four-hour symbol needs a complete four-hour absent symbol plus lookahead");
    check(std::isfinite(result.cpu_seconds) && std::isfinite(result.gpu_seconds) && std::isfinite(result.tracking_seconds),
          "long sampled estimates must remain finite");
    near(result.tracking_symbol_windows,1,"a four-hour bit needs one fully scored four-hour absent window");
    channel.snr_db=std::numeric_limits<double>::quiet_NaN();
    bool rejected=false;
    try {(void)simulation::estimate(value,options,true,channel);}catch(const Error&){rejected=true;}
    check(rejected,"invalid channel input must be rejected");
}
std::optional<transfer::Received> sampled_reception(const Message& message,const transfer::Options& options,
                                                   const modem::ChannelConfig& channel,bool observe_absence=true,
                                                   modem::PatternSearch search={}) {
    const auto config=transfer::seeded_config(options,options.timestamp);
    auto source=transfer::message_transmitter(message,options);
    search.expand_clock_search=true;
    search.start_offset_seconds=(static_cast<double>(modem::training_sample_count(config))+
        modem::pattern_pulse_padding_samples(config))/config.sample_rate;
    search.start_uncertainty_seconds=options.search_seconds+1.;
    search.worker_threads=1;
    modem::PatternReceiver receiver(config,options.dsp_workspace_bytes,search);
    transfer::StreamReceiver content(options,options.timestamp);
    modem::SampledSimulationChannel impairment(config,channel);
    std::array<float,2048> samples{};
    std::optional<transfer::Received> result;
    const auto harvest=[&] {
        for(auto& burst:receiver.take_bursts())
            result=content.push(std::move(burst),receiver.diagnostics());
    };
    while(const auto count=impairment.read(*source,samples)) {
        receiver.push(std::span(samples).first(count));harvest();
    }
    // The actual sampled absence policy must complete reception; EOF cannot.
    auto trailing=observe_absence?modem::pattern_absence_samples(config)+config.sample_rate+
        2*modem::pattern_pulse_padding_samples(config):0;
    while(trailing) {
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(trailing,samples.size()));
        auto tail=std::span(samples).first(count);
        impairment.read_noise(tail);receiver.push(tail);harvest();trailing-=count;
    }
    // Flush scoring of already observed complete windows; this may classify
    // the sampled tail above, but must not invent absence without that tail.
    receiver.finish();harvest();
    return result;
}
void narrow_band_carrier_coverage() {
    transfer::Options options;
    options.modem=tuning::resolve(1,32,tuning::PatternMode::auto_pattern,false).config;
    options.timestamp=1800000000;
    Message message;message.kind=MessageKind::text;message.data={'a'};
    const auto transmission=transfer::estimate(message,options);
    check(transfer::message_wire_bits(message,options)==Bytes({0,1,1}),"a must retain its independent exact dictionary vector");
    check(modem::symbol_sample_count(options.modem)==768000 && options.modem.sample_rate==6000,
          "1 Hz auto-pattern must retain its 128-second sampled symbol");
    modem::ChannelConfig channel;
    channel.snr_db=tuning::link_budget(tuning::parse_simulation_preset("3dBm -120dB"),1,
                                     options.modem.sample_rate).sample_snr_db;
    const auto expanded=simulation::estimate(transmission,options,true,channel);
    check(expanded.profile_matches && expanded.carrier_in_search && expanded.confidence_available,
          "expanded narrow-band search must cover the default 100 ppm carrier mismatch");
    near(expanded.carrier_offset_hz,.15,"default clock mismatch must include the 1500 Hz carrier shift");
    near(expanded.carrier_search_half_width_hz,.30078125,"carrier bank must reflect the actual expanded symbol-scaled offsets");
    check(std::isfinite(expanded.cpu_seconds) && expanded.cpu_seconds>0 &&
          std::isfinite(expanded.gpu_seconds) && expanded.gpu_seconds>0,
          "expanded bank must retain finite reference simulation time estimates");
    modem::PatternSearch old_search;
    constexpr auto old_step=1./512;
    old_search.frequency_offsets_hz={0,-old_step,old_step,-2*old_step,2*old_step};
    const auto failed=sampled_reception(message,options,channel,true,old_search);
    check(!failed,"explicit old five-bin search must reproduce the uncovered-carrier failure");
    const auto expanded_received=sampled_reception(message,options,channel);
    check(expanded_received && expanded_received->stream_complete && expanded_received->short_text_decoded &&
          expanded_received->raw_bits==Bytes({0,1,1}) && expanded_received->content.message.data==message.data,
          "expanded default search must receive exact 011 and a despite the default clock error");

    channel.snr_db+=60;
    channel.frequency_offset_hz=.4;
    check(!simulation::estimate(transmission,options,true,channel).confidence_available,
          "stronger signal cannot restore confidence outside the receiver carrier search");
    channel.frequency_offset_hz=0;channel.clock_error_ppm=-100;
    const auto negative=simulation::estimate(transmission,options,true,channel);
    check(negative.carrier_in_search && negative.confidence_available,"expanded coverage must include both signs of clock error");
    near(negative.carrier_offset_hz,-.15,"negative clock mismatch must retain its sign");

    channel.snr_db-=60;channel.clock_error_ppm=0;
    const auto covered=simulation::estimate(transmission,options,true,channel);
    check(covered.carrier_in_search && covered.confidence_available,"zero clock error restores modeled carrier coverage");
    const auto eof_only=sampled_reception(message,options,channel,false);
    check(!eof_only || !eof_only->stream_complete,
          "EOF without a fully observed absent symbol must not complete the narrow-band reception");
    const auto received=sampled_reception(message,options,channel);
    check(received && received->stream_complete && received->short_text_decoded &&
          received->raw_bits==Bytes({0,1,1}) && received->content.message.data==message.data,
          "covered narrow-band sampled channel must receive exact 011 and a after physical completion");

    constexpr double edge=.30078125;
    for(const double sign:{-1.,1.}) {
        channel.frequency_offset_hz=sign*std::nextafter(edge,0.);
        check(simulation::estimate(transmission,options,true,channel).confidence_available,
              "carrier just inside either search edge remains modeled");
        channel.frequency_offset_hz=sign*edge;
        check(simulation::estimate(transmission,options,true,channel).confidence_available,
              "the outermost actual frequency hypothesis remains covered");
        channel.frequency_offset_hz=sign*std::nextafter(edge,std::numeric_limits<double>::infinity());
        check(!simulation::estimate(transmission,options,true,channel).confidence_available,
              "carrier just outside either search edge must not display a percentage");
    }
    // A requested fractional duration is rounded up to complete PCM samples.
    // Its search bank must follow that quantized duration, not the request.
    options.modem.integration_seconds=128.00001;
    channel.frequency_offset_hz=0;
    const auto quantized=simulation::estimate(wire(3,options.modem),options,true,channel);
    check(modem::symbol_sample_count(options.modem)==768001,"fractional integration fixture must add exactly one sample");
    const auto geometry=modem::default_pattern_frequency_search(options.modem);
    const auto quantized_edge=geometry.half_width_hz;
    near(quantized.carrier_search_half_width_hz,quantized_edge,"search width must follow sample-quantized symbol duration");
    const auto unquantized_edge=static_cast<double>(geometry.count/2)*.25/options.modem.integration_seconds;
    channel.frequency_offset_hz=(quantized_edge+unquantized_edge)/2;
    check(!simulation::estimate(wire(3,options.modem),options,true,channel).confidence_available,
          "unquantized duration must not enlarge the actual receiver carrier bank");
}
void coupled_and_independent_clock_estimates() {
    transfer::Options options;
    options.modem=tuning::resolve(100,-3,tuning::PatternMode::auto_pattern,false).config;
    options.timestamp=1800000000;
    const auto geometry=modem::default_pattern_frequency_search(options.modem);
    check(geometry.count==395,"weak 100-Hz profile must use expanded default search");
    auto independent=clean_channel();independent.snr_db=-20;independent.frequency_offset_hz=.15;
    const auto independent_result=simulation::estimate(wire(3,options.modem),options,true,independent);
    auto shared_clock=independent;shared_clock.frequency_offset_hz=0;shared_clock.clock_error_ppm=100;
    const auto coupled=simulation::estimate(wire(3,options.modem),options,true,shared_clock);
    check(independent_result.confidence_available && coupled.confidence_available,
          "independent carrier and common clock errors must both be represented by the default bank");
    check(coupled.modeled_symbol_snr_db<=independent_result.modeled_symbol_snr_db &&
          independent_result.modeled_symbol_snr_db-coupled.modeled_symbol_snr_db<.05,
          "coupled timing alternatives must remove most shared-clock smear while retaining independent carrier alternatives");
    options.modem.integration_seconds=86400;
    const auto capped=simulation::estimate(wire(3,options.modem),options,true,shared_clock);
    near(capped.carrier_search_half_width_hz,modem::default_pattern_frequency_search(options.modem).half_width_hz,
         "capped search model must advertise only its finite actual span");
    check(!capped.carrier_in_search && !capped.confidence_available &&
          std::isfinite(capped.cpu_seconds) && std::isfinite(capped.gpu_seconds),
          "finite frequency cap must not masquerade as complete day-long clock coverage");
}
void streamed_template_workload() {
    transfer::Options options;
    options.modem=tuning::resolve(100,-6,tuning::PatternMode::auto_pattern,false).config;
    options.timestamp=1800000000;
    auto channel=clean_channel();channel.snr_db=-20;
    const auto transmission=wire(3,options.modem);
    options.dsp_workspace_bytes=64*1024*1024;
    const auto streamed=simulation::estimate(transmission,options,true,channel);
    options.dsp_workspace_bytes=std::size_t{8}*1024*1024*1024;
    const auto retained=simulation::estimate(transmission,options,true,channel);
    check(streamed.receiver_workspace_supported && retained.receiver_workspace_supported &&
          streamed.confidence_available && retained.confidence_available,
          "retained and streamed FFT profiles must both have affordable core workspace");
    check(streamed.drift_sections==4 && retained.drift_sections==4,
          "the long dense fixture must exercise the new section detector");
    near(streamed.cpu_seconds,retained.cpu_seconds,
         "four-section FFT work must stream its partial templates even with enough RAM to cache old whole-bit rows");
    near(streamed.gpu_seconds,retained.gpu_seconds,
         "hypothetical GPU work must reflect the same fixed section-template policy");
    near(streamed.success_probability,retained.success_probability,
         "streaming transformed templates must preserve modeled search coverage and confidence");
    const std::array shared_profiles{options.modem,options.modem};
    const auto shared_large=simulation::estimate(transmission,options,true,channel,shared_profiles);
    options.dsp_workspace_bytes=64*1024*1024;
    const auto shared_small=simulation::estimate(transmission,options,true,channel,shared_profiles);
    near(shared_large.cpu_seconds,shared_small.cpu_seconds,
         "expanded live banks sharing RAM must stream rows even when an early bank could cache them");
    near(shared_large.gpu_seconds,shared_small.gpu_seconds,
         "shared-bank template policy must apply to both reference compute estimates");
    options.dsp_workspace_bytes=16*1024*1024;
    const auto half_budget=simulation::estimate(transmission,options,true,channel);
    check(!half_budget.receiver_workspace_supported && !half_budget.confidence_available,
          "a single live bank cannot claim the half of DSP memory reserved for transmit and peer work");
    options.dsp_workspace_bytes=1024*1024;
    const auto unsupported=simulation::estimate(transmission,options,true,channel);
    check(unsupported.profile_matches && unsupported.carrier_in_search &&
          !unsupported.receiver_workspace_supported && !unsupported.confidence_available,
          "unaffordable expanded FFT core must withhold confidence instead of substituting a correlator");
    check(std::isfinite(unsupported.cpu_seconds) && unsupported.cpu_seconds>0 &&
          std::isfinite(unsupported.receiver_cpu_seconds) && unsupported.receiver_cpu_seconds>0 &&
          unsupported.receiver_cpu_seconds<unsupported.cpu_seconds &&
          std::isfinite(unsupported.gpu_seconds) && unsupported.gpu_seconds>0,
          "unaffordable expanded FFT must retain requested receive-work estimates without restoring confidence");
    near(unsupported.carrier_search_half_width_hz,streamed.carrier_search_half_width_hz,
         "insufficient workspace must not silently shrink the requested carrier bank");

    // A sixty-chip pattern cannot support the extra fit. Preserve coverage
    // of the original cache-versus-stream compute policy for this geometry.
    options.modem=tuning::resolve(1,32,tuning::PatternMode::auto_pattern,false).config;
    options.modem.integration_seconds=120;
    const auto sparse_transmission=wire(3,options.modem);
    options.dsp_workspace_bytes=2*1024*1024;
    const auto sparse_streamed=simulation::estimate(sparse_transmission,options,true,channel);
    options.dsp_workspace_bytes=64*1024*1024;
    const auto sparse_retained=simulation::estimate(sparse_transmission,options,true,channel);
    check(sparse_streamed.drift_sections==1 && sparse_retained.drift_sections==1 &&
          sparse_streamed.receiver_workspace_supported && sparse_retained.receiver_workspace_supported,
          "original template-cache comparison must use an affordable geometry without section fitting");
    check(sparse_streamed.cpu_seconds>sparse_retained.cpu_seconds &&
          sparse_streamed.gpu_seconds>sparse_retained.gpu_seconds,
          "streamed whole-bit template generation must still add work outside the section detector");
    near(sparse_streamed.success_probability,sparse_retained.success_probability,
         "whole-bit template caching must preserve the original search confidence");
}
void target_and_channel_are_independent() {
    transfer::Options options;
    options.modem=tuning::resolve(100,-61,tuning::PatternMode::auto_pattern,false).config;
    options.timestamp=1800000000;
    Message message;message.kind=MessageKind::text;message.data={'a'};
    const auto weak_budget=tuning::link_budget(tuning::parse_simulation_preset("3dBm -170dB"),
                                              100,options.modem.sample_rate);
    near(weak_budget.snr_db_hz,-3,"-170 dB preset must supply -3 dB-Hz independently of target");
    near(modem::symbol_seconds(options.modem),std::pow(10.,7.9),
         "-61 dB-Hz design target must request its long integration, not set channel power");
    modem::ChannelConfig channel;channel.snr_db=weak_budget.sample_snr_db;
    const auto weak=simulation::estimate(transfer::estimate(message,options),options,true,channel);
    check(!weak.confidence_available && !weak.carrier_in_search,
          "long-integration target must not promise to overcome untracked carrier drift");
    options.modem=tuning::resolve(100,140,tuning::PatternMode::auto_pattern,false).config;
    const auto strong_budget=tuning::link_budget(tuning::parse_simulation_preset("3dBm -120dB"),
                                                100,options.modem.sample_rate);
    near(strong_budget.snr_db_hz,47,"-120 dB preset must supply +47 dB-Hz, not the 140 design target");
    near(modem::symbol_seconds(options.modem),1.28,"100 Hz high target must respect the minimum pattern length");
    channel.snr_db=strong_budget.sample_snr_db;
    const auto strong=simulation::estimate(transfer::estimate(message,options),options,true,channel);
    check(strong.confidence_available && strong.success_probability>.999,
          "TX design target must not become a hard minimum receive C/N0");
    const auto received=sampled_reception(message,options,channel);
    check(received && received->stream_complete && received->short_text_decoded &&
          received->raw_bits==Bytes({0,1,1}) && received->content.message.data==message.data,
          "strong sampled 100 Hz channel must receive a even below the numerical design target");
}
void oscillator_policy_geometry() {
    transfer::Options options;
    options.modem=tuning::resolve(100,-3,tuning::PatternMode::auto_pattern,false).config;
    options.timestamp=1800000000;options.dsp_workspace_bytes=128*1024*1024;
    auto channel=clean_channel();
    const auto legacy=simulation::estimate(wire(1,options.modem),options,true,channel,{},1,false);
    modem::OscillatorSearchConfig oscillator;
    oscillator.lf={.0001,.05};oscillator.rf={.0001,.005};
    options.modem.oscillator_search=oscillator;
    const auto narrow=simulation::estimate(wire(1,options.modem),options,true,channel,{},1,false);
    const auto plan=modem::oscillator_pattern_search(options.modem);
    check(narrow.frequency_rate_hypotheses==plan.hypotheses.size()&&
          narrow.frequency_rate_hypotheses<legacy.frequency_rate_hypotheses,
          "oscillator accuracy must reduce the bank used for real receiver workload estimates");
    near(narrow.carrier_search_half_width_hz,plan.frequency.half_width_hz,
         "estimated coverage must use the actual oscillator bank");
    check(narrow.receiver_cpu_seconds<legacy.receiver_cpu_seconds,
          "a smaller declared search must remove repeated receiver work");
    auto receive_profile=options.modem;
    receive_profile.oscillator_search->margin=2;
    receive_profile.oscillator_search->lf.accuracy_ppm=.0002;
    const auto receive_plan=modem::oscillator_pattern_search(receive_profile);
    const auto different_policy=simulation::estimate(wire(1,options.modem),options,true,channel,
        std::span(&receive_profile,1),1,false);
    check(different_policy.profile_matches&&different_policy.carrier_in_search&&different_policy.clock_in_search&&
          different_policy.frequency_rate_hypotheses==receive_plan.hypotheses.size(),
          "receiver oscillator assumptions must not change the matching PCM waveform identity");
    near(different_policy.requested_carrier_search_half_width_hz,receive_plan.frequency.requested_half_width_hz,
         "coverage estimates must use the actual matching receive policy rather than the transmit policy");
    const auto statistical=simulation::estimate(wire(1,options.modem),options,true,channel);
    check(statistical.confidence_available&&statistical.probability_search_approximation&&
          statistical.probability_model_limit.find("joint timing-path covariance")!=std::string::npos,
          "paired timing estimates must disclose their conditional statistical approximation");
    channel.clock_error_ppm=2*plan.clock_half_width_ppm+1;
    const auto uncovered=simulation::estimate(wire(1,options.modem),options,true,channel,{},1,false);
    check(!uncovered.clock_in_search&&!uncovered.confidence_available,
          "an independent sample clock outside the declared region cannot show confidence");
    oscillator.reference=modem::OscillatorReference::shared_radio;oscillator.rf_shift_hz=10000000;
    oscillator.lf={100,.5};options.modem.oscillator_search=oscillator;
    const auto effects=modem::oscillator_effects(options.modem);
    channel.clock_error_ppm=effects.clock_error_ppm;
    channel.frequency_offset_hz=effects.frequency_offset_hz;
    channel.phase_noise_degrees_per_sqrt_second=effects.phase_noise_degrees_per_sqrt_second;
    const auto shared=simulation::estimate(wire(1,options.modem),options,true,channel,{},1,false);
    const auto shared_plan=modem::oscillator_pattern_search(options.modem);
    check(shared.clock_in_search&&shared.carrier_in_search&&!shared.oscillator_search_limited&&
          shared.frequency_rate_hypotheses==shared_plan.hypotheses.size(),
          "radio reference must supply paired RF and ADC clock coverage despite an unused inaccurate LF model");
    near(shared.carrier_offset_hz,effects.frequency_offset_hz+
         options.modem.carrier_hz*effects.clock_error_ppm*1e-6,
         "radio simulation truth must retain additive RF and sample-clock contributions exactly once");
    channel.clock_error_ppm=0;channel.frequency_offset_hz=shared_plan.frequency.half_width_hz;
    const auto off_relation=simulation::estimate(wire(1,options.modem),options,true,channel,{},1,false);
    check(off_relation.carrier_in_search&&!off_relation.clock_in_search&&!off_relation.confidence_available,
          "independent carrier and timing errors cannot claim coverage of a shared-reference diagonal bank");
    // The declared model at a 1x edge can differ from its stored double
    // endpoint by one rounding unit after the channel's long-double product.
    auto boundary_options=options;
    auto& boundary=*boundary_options.modem.oscillator_search;
    boundary.reference=modem::OscillatorReference::independent_audio;
    boundary.rf_shift_hz=0;boundary.margin=1;boundary.lf.accuracy_ppm=100;
    auto boundary_channel=clean_channel();boundary_channel.clock_error_ppm=100;
    const auto edge=simulation::estimate(wire(1,boundary_options.modem),boundary_options,true,boundary_channel,{},1,false);
    check(edge.carrier_in_search&&edge.clock_in_search,
          "the declared crystal 1x endpoint must remain covered despite arithmetic representation error");
    boundary_channel.clock_error_ppm=100.001;
    const auto beyond=simulation::estimate(wire(1,boundary_options.modem),boundary_options,true,boundary_channel,{},1,false);
    check(!beyond.clock_in_search&&!beyond.carrier_in_search,
          "representation rounding must not expand the physical oscillator bound");
    boundary_channel.clock_error_ppm=0;
    boundary_channel.frequency_offset_hz=std::nextafter(edge.carrier_search_half_width_hz,
        std::numeric_limits<double>::infinity());
    const auto next_outside=simulation::estimate(wire(1,boundary_options.modem),boundary_options,true,boundary_channel,{},1,false);
    check(!next_outside.carrier_in_search,
          "the next representable frequency outside a policy endpoint must remain outside");
    oscillator.rf.accuracy_ppm=100;options.modem.oscillator_search=oscillator;
    const auto limited=simulation::estimate(wire(1,options.modem),options,true,clean_channel(),{},1,false);
    check(limited.oscillator_search_limited&&!limited.confidence_available&&
          limited.requested_carrier_search_half_width_hz>limited.carrier_search_half_width_hz,
          "finite radio frequency coverage must expose incomplete oscillator margin even for a central signal");
}
void utc_bank_estimate() {
    transfer::Options options;options.modem=tuning::resolve(100,16,tuning::PatternMode::auto_keystream,true).config;
    options.key.emplace(Bytes(32,0x39));options.timestamp=1800000000;options.dsp_workspace_bytes=128*1024*1024;
    modem::OscillatorSearchConfig policy;policy.lf={10,.05};policy.rf={.01,.005};policy.margin=3;
    options.modem.oscillator_search=policy;
    const auto baseline=simulation::estimate(wire(3,options.modem),options,true,clean_channel(),{},1,false);
    options.clock_sync=clock_sync::Policy{.001,.4,2.564};
    const auto actual=modem::oscillator_pattern_search(options.modem,modem::utc_transmit_rate_limit(options.modem));
    const auto estimate=simulation::estimate(wire(3,options.modem),options,true,clean_channel(),{},1,true,100,4096,
        simulation::ReceiverWorkMode::hardware_fallback);
    check(actual.transmit_rate_correction>0 && estimate.frequency_rate_hypotheses==actual.hypotheses.size() &&
        estimate.frequency_rate_hypotheses>=baseline.frequency_rate_hypotheses,
        "UTC estimator omitted actual expanded-bank hypotheses");
    check(estimate.receiver_cpu_seconds>=baseline.receiver_cpu_seconds,
        "UTC estimate discounted work without implemented arrival-window evidence");
    check(!estimate.confidence_available && !estimate.one_bit_confidence_available &&
        estimate.probability_reference_only && estimate.reference_probability_available &&
        estimate.one_bit_reference_available && !estimate.probability_interval_available &&
        estimate.probability_model_limit.find("UTC")!=std::string::npos,
        "UTC reference must remain distinct from a supported receiver probability");
    const auto simulated=simulation::estimate(wire(3,options.modem),options,true,clean_channel(),{},1,false);
    check(simulated.frequency_rate_hypotheses==baseline.frequency_rate_hypotheses,
        "known unsteered sampled simulation must not acquire hardware steering lanes");
    near(simulated.receiver_cpu_seconds,baseline.receiver_cpu_seconds,
        "clock controls alone must not increase known unsteered simulation work");
}
void clock_dsss_reference() {
    transfer::Options options;options.modem=tuning::resolve(1200,70,tuning::PatternMode::auto_keystream,true,9000,10).config;
    options.key.emplace(Bytes(32,0x63));options.search_seconds=6;options.dsp_workspace_bytes=128*1024*1024;
    modem::OscillatorSearchConfig policy;policy.lf={.0001,.5};policy.rf={.0001,.005};
    policy.rf_shift_hz=1000000;policy.margin=3;options.modem.oscillator_search=policy;
    const auto original=modem::oscillator_pattern_search(options.modem);
    const auto expanded=modem::oscillator_pattern_search(options.modem,modem::utc_transmit_rate_limit(options.modem));
    options.clock_sync=clock_sync::Policy{.0001,.001,0};options.audio_timing_error_seconds=.001;
    const auto draft=wire(3,options.modem);
    const auto sampled=simulation::estimate(draft,options,true,clean_channel());
    const auto hardware=simulation::estimate(draft,options,true,clean_channel(),{},1,true,100,4096,
        simulation::ReceiverWorkMode::hardware_fallback);
    const auto short_timed=simulation::estimate(draft,options,true,clean_channel(),{},1,false,100,4096,
        simulation::ReceiverWorkMode::hardware_timing_model);
    check(options.modem.sample_rate==48000&&modem::pattern_chip_samples(options.modem)==8&&
        modem::symbol_sample_count(options.modem)==5120,
        "imported DSSS configuration no longer has its resolved waveform geometry");
    check(sampled.frequency_rate_hypotheses==original.hypotheses.size()&&
        hardware.frequency_rate_hypotheses==expanded.hypotheses.size(),
        "work contexts must retain their actual oscillator banks");
    check(sampled.epoch_hypotheses==16&&hardware.epoch_hypotheses==13,
        "sampled startup and hardware initial epoch coverage must remain distinct");
    check(short_timed.timing_window_modeled&&short_timed.timing_hypotheses==hardware.timing_hypotheses&&
        short_timed.fft_retained_acquisition_batches<short_timed.fft_acquisition_batches,
        "short FFT prior must retain threshold alternatives while skipping excluded acquisition batches");
    check(short_timed.receiver_cpu_seconds<hardware.receiver_cpu_seconds,
        "implemented short FFT pruning must reduce acquisition work");
    near(short_timed.receiver_frontend_seconds,hardware.receiver_frontend_seconds,
        "FFT pruning must retain all input processing");
    near(short_timed.fallback_receiver_cpu_seconds,hardware.receiver_cpu_seconds,
        "FFT prior must report full-window fallback cost");
    for(const auto* estimate:{&sampled,&hardware}) {
        check(estimate->probability_reference_only&&estimate->reference_probability_available&&
            estimate->one_bit_reference_available&&!estimate->confidence_available&&
            !estimate->one_bit_confidence_available&&!estimate->probability_interval_available,
            "outer DSSS reference falsely qualified the new receiver behavior");
        check(estimate->success_probability>=0&&estimate->success_probability<=1&&
            estimate->probability_model_limit.find("outer DSSS")!=std::string::npos,
            "outer DSSS conditional reference or scope is missing");
    }
    check(sampled.receiver_work_assumptions.find("outer-code candidate-check")!=std::string::npos,
        "DSSS estimate hid its added coherent guard workload allowance");
    options.clock_sync.reset();
    const auto without_clock=simulation::estimate(draft,options,true,clean_channel(),{},1,false);
    near(sampled.receiver_cpu_seconds,without_clock.receiver_cpu_seconds,
        "tight clock fields must not invent simulated timing-bank work");
}
void rolling_fft_epoch_workload() {
    transfer::Options options;
    options.modem=tuning::resolve(10,40,tuning::PatternMode::auto_keystream,true,7500,1000).config;
    options.key.emplace(Bytes(32,0x5d));options.search_seconds=6;
    options.dsp_workspace_bytes=std::size_t{1024}*1024*1024;
    modem::OscillatorSearchConfig oscillator;oscillator.lf={.0001,.5};oscillator.rf={.0001,.005};
    oscillator.rf_shift_hz=1000000;oscillator.margin=3;options.modem.oscillator_search=oscillator;
    const auto original=modem::oscillator_pattern_search(options.modem);
    check(options.modem.sample_rate==40000&&modem::pattern_chip_samples(options.modem)==8&&
          modem::symbol_sample_count(options.modem)==512000&&original.hypotheses.size()==3,
          "rate10 DSSS1000 reproducer must preserve its exact waveform and original oscillator bank");
    const simulation::ReceiverTimingModel metadata{.005,0,1./options.modem.sample_rate};
    const auto estimate=[&](const transfer::Options& current,const transfer::Estimate& draft,
                            simulation::ReceiverWorkMode mode,
                            simulation::ReceiverTimingModel timing=simulation::ReceiverTimingModel{.005,0,1./40000}) {
        return simulation::estimate(draft,current,true,clean_channel(),{},1,false,100,4096,mode,timing);
    };
    const auto draft=wire(1,options.modem);
    const auto ordinary=estimate(options,draft,simulation::ReceiverWorkMode::hardware_fallback,metadata);
    options.clock_sync=clock_sync::Policy{.001,.001,0};options.audio_timing_error_seconds=.05;
    const auto expanded=modem::oscillator_pattern_search(options.modem,modem::utc_transmit_rate_limit(options.modem));
    check(expanded.hypotheses.size()==5,"UTC must preserve the original three lanes and its two correction endpoints");
    for(const auto& pair:original.hypotheses)
        check(std::any_of(expanded.hypotheses.begin(),expanded.hypotheses.end(),[&](const auto& candidate) {
            return candidate.frequency_offset_hz==pair.frequency_offset_hz&&candidate.clock_error_ppm==pair.clock_error_ppm;
        }),"UTC union dropped an original oscillator frequency/clock endpoint");
    const auto one=estimate(options,draft,simulation::ReceiverWorkMode::hardware_timing_model,metadata);
    check(one.receiver_workspace_supported&&one.timing_window_modeled&&one.epoch_hypotheses==13&&
          one.frequency_rate_hypotheses==5&&one.fft_acquisition_batches==1&&one.fft_retained_acquisition_batches==1,
          "one-bit GPS reproducer must expose the initial13 epoch and1/1 whole-FFT-batch plateau");
    check(one.new_epoch_admissions>0&&one.new_epoch_full_fft_batches>0&&
          one.new_epoch_retained_fft_batches<=one.new_epoch_full_fft_batches&&
          one.new_epoch_frontend_seconds>0&&one.new_epoch_setup_seconds>0,
          "automatic Live work must charge new epochs, constructors and their input processing");
    check(one.receiver_work_assumptions.find("initial FFT acquisition batches retained 1 / 1")!=std::string::npos&&
          one.receiver_work_assumptions.find("newly admitted epochs")!=std::string::npos,
          "rendered engineering diagnostic must distinguish initial and rolling FFT work");
    check(ordinary.frequency_rate_hypotheses==3&&ordinary.new_epoch_admissions>0&&
          one.frequency_rate_hypotheses>ordinary.frequency_rate_hypotheses,
          "default clock and qualified GPS work must retain their different actual oscillator domains");
    options.audio_timing_error_seconds=.01;
    const auto narrower=estimate(options,draft,simulation::ReceiverWorkMode::hardware_timing_model,metadata);
    check(narrower.timing_window_modeled&&narrower.fft_acquisition_batches==1&&
          narrower.fft_retained_acquisition_batches==1,"Audio10ms cannot invent a saved overlapping initial FFT batch");
    near(narrower.receiver_cpu_seconds,one.receiver_cpu_seconds,
         "Audio50ms and10ms must preserve the actual whole-batch CPU plateau for this one-bit geometry");
    auto over_budget=metadata;over_budget.capture_error_seconds=.02;
    const auto unsupported=estimate(options,draft,simulation::ReceiverWorkMode::hardware_timing_model,over_budget);
    const auto fallback=estimate(options,draft,simulation::ReceiverWorkMode::hardware_fallback,over_budget);
    check(!unsupported.timing_window_modeled&&unsupported.frequency_rate_hypotheses==5&&
          unsupported.timing_hypotheses==fallback.timing_hypotheses,
          "capture metadata outside Audioerror must retain all UTC lanes and full timing coverage");
    near(unsupported.receiver_cpu_seconds,fallback.receiver_cpu_seconds,
         "over-budget timing must use the full arrival-window work fallback");

    // The plotted CPU marker is per bit. Separately test extended observation
    // with a fixed one-bit desired stream: only acquisition horizon changes.
    auto long_draft=wire(70,options.modem);long_draft.wire_bits=1;
    const auto longer=estimate(options,long_draft,simulation::ReceiverWorkMode::hardware_timing_model,metadata);
    auto twice=wire(140,options.modem);twice.wire_bits=1;
    const auto doubled=estimate(options,twice,simulation::ReceiverWorkMode::hardware_timing_model,metadata);
    check(longer.fft_retained_acquisition_batches==doubled.fft_retained_acquisition_batches&&
          longer.fft_retained_acquisition_batches<longer.fft_acquisition_batches&&
          doubled.new_epoch_admissions>1.8*longer.new_epoch_admissions&&
          doubled.new_epoch_retained_fft_batches>1.5*longer.new_epoch_retained_fft_batches&&
          doubled.receiver_search_seconds>1.5*longer.receiver_search_seconds&&
          doubled.receiver_search_seconds<2.5*longer.receiver_search_seconds,
          "static per-epoch prior cap must not hide continuing, lifetime-capped Live acquisition work");

    // Independent explicit birth/batch enumeration, including first-pass and
    // hop endpoints. The model's affine envelope can exceed integer totals.
    const long double fs=options.modem.sample_rate,bin=modem::pattern_projection_bin_samples(options.modem,expanded.frequency.half_width_hz);
    const auto symbol=modem::symbol_sample_count(options.modem);
    auto minimum_rate=1.L;
    for(const auto& pair:expanded.hypotheses)
        minimum_rate=std::min(minimum_rate,1+static_cast<long double>(pair.clock_error_ppm)*1e-6L);
    const auto length=std::ceil(symbol/(minimum_rate*bin));
    const auto transform=std::exp2(std::ceil(std::log2(2*std::ceil(symbol/bin))));
    const auto hop=transform-length+1,first=(length+hop-1)*bin/fs,hop_seconds=hop*bin/fs;
    const auto upper=(2.L*options.search_seconds+1)*fs;
    long double complete_scan=first,next_start=hop*bin;
    while(next_start<=upper){complete_scan+=hop_seconds;next_start+=hop*bin;}
    const auto prefix=(modem::training_sample_count(options.modem)+modem::pattern_pulse_padding_samples(options.modem))/fs;
    const auto lifetime=std::max(complete_scan,prefix+symbol/fs+2*options.search_seconds+1)+2;
    for(const auto pass:{0,1})for(const auto side:{-1,1}) {
        auto edge=draft;
        const auto desired=first+pass*hop_seconds+side/fs;
        edge.total_seconds+=static_cast<double>(desired-one.simulated_seconds);
        const auto measured=estimate(options,edge,simulation::ReceiverWorkMode::hardware_timing_model,metadata);
        check(measured.fft_acquisition_batches==pass+(side>0?1:0),
              "hardware FFT work counted a partial/unobserved batch or omitted a complete one");
    }
    for(const auto offset:{-1.L,0.L,1.L,fs/4}) {
        auto edge=draft;
        const auto desired=first+hop_seconds+offset/fs;
        // The API includes physical absence/lookahead. Adjust only this test's
        // supplied waveform duration to select the desired observation horizon.
        edge.total_seconds+=static_cast<double>(desired-one.simulated_seconds);
        const auto measured=estimate(options,edge,simulation::ReceiverWorkMode::hardware_timing_model,metadata);
        for(const auto phase:{0.L,.25L,.99L}) {
            long double exact=0;
            for(auto birth=phase;birth<measured.simulated_seconds;birth+=1)
                for(auto batch=first;batch<=lifetime&&batch<=measured.simulated_seconds-birth;batch+=hop_seconds)++exact;
            check(measured.new_epoch_full_fft_batches+1e-9>=exact,
                  "fresh FFT work envelope undercounted an explicit fractional-birth acquisition batch");
        }
    }
    // Capture clocks need not advance at exactly one UTC second per nominal
    // sample second. Enumerate births at the fastest admitted cadence and
    // retirement at the slowest admitted rate, including a nonzero interval.
    for(const auto ppm:{-100.L,100.L})for(const auto uncertainty:{0.L,25e-6L}) {
        const auto slope=1+ppm*1e-6L;
        const auto slow=std::min(1.L,slope-uncertainty);
        const auto fast=std::max(1.L,slope+uncertainty);
        const auto retirement=std::max(complete_scan,
            (prefix+symbol/fs+2*options.search_seconds+1)/slow)+2/slow;
        auto clocked=metadata;
        clocked.capture_seconds_per_frame=static_cast<double>(slope/fs);
        clocked.capture_rate_uncertainty_fraction=static_cast<double>(uncertainty);
        for(const auto horizon:{first-1/fs,first+1/fs,first+hop_seconds+1/fs,150.L}) {
            auto edge=draft;
            edge.total_seconds+=static_cast<double>(horizon-one.simulated_seconds);
            const auto measured=estimate(options,edge,simulation::ReceiverWorkMode::hardware_timing_model,clocked);
            for(const auto phase:{0.L,.25L,.99L}) {
                long double births=0,batches=0;
                for(auto birth=phase/fast;birth<measured.simulated_seconds;birth+=1/fast) {
                    ++births;
                    for(auto batch=first;batch<=retirement&&batch<=measured.simulated_seconds-birth;batch+=hop_seconds)
                        ++batches;
                }
                check(measured.new_epoch_admissions>=births&&
                      measured.new_epoch_full_fft_batches+1e-9>=batches,
                      "fresh FFT envelope undercounted nonnominal capture cadence or uncertain retirement");
            }
        }
    }
    auto early=draft;early.total_seconds+=static_cast<double>(first-one.simulated_seconds-.5L);
    const auto newborns=estimate(options,early,simulation::ReceiverWorkMode::hardware_timing_model,metadata);
    check(newborns.new_epoch_full_fft_batches==0&&newborns.new_epoch_admissions>0&&
          newborns.new_epoch_frontend_seconds>0&&newborns.new_epoch_setup_seconds>0,
          "epochs too new for a full FFT still require constructors and input processing");
    auto fixed=options;fixed.timestamp=1800000000;
    const auto anchored=estimate(fixed,draft,simulation::ReceiverWorkMode::hardware_timing_model,metadata);
    const auto simulated=estimate(options,draft,simulation::ReceiverWorkMode::sampled_simulation,metadata);
    check(anchored.new_epoch_admissions==0&&anchored.new_epoch_frontend_seconds==0&&
          simulated.new_epoch_admissions==0&&simulated.frequency_rate_hypotheses==3,
          "fixed epoch and known sampled simulation must not invent automatic hardware epoch refresh");

    auto illustrated=options;
    illustrated.modem.oscillator_search->rf_shift_hz=20900000; // Fake 0.4s/200,100kHz spacing.
    const auto hop_bank=modem::oscillator_pattern_search(illustrated.modem,modem::utc_transmit_rate_limit(illustrated.modem));
    const auto hypothetical=estimate(illustrated,draft,simulation::ReceiverWorkMode::hardware_timing_model,metadata);
    check(hypothetical.frequency_rate_hypotheses==hop_bank.hypotheses.size()&&
          hypothetical.requested_carrier_search_half_width_hz>one.requested_carrier_search_half_width_hz&&
          hypothetical.clock_search_half_width_ppm==one.clock_search_half_width_ppm,
          "Fake FHSS highest illustrated RF must change oscillator frequency coverage without changing audio clock geometry");
    near(one.requested_carrier_search_half_width_hz,expanded.frequency.requested_half_width_hz,
         "actual fixed hardware RF coverage must use its1MHz Shift, separately from Fake hopping");
    near(hypothetical.requested_carrier_search_half_width_hz,hop_bank.frequency.requested_half_width_hz,
         "hypothetical Fake FHSS must retain highest20.9MHz Shift oscillator boundary");
}
void compact_clock_prior_workload() {
    transfer::Options options;options.key.emplace(Bytes(32,0x24));
    options.modem.sample_rate=6000;options.modem.carrier_hz=1500;options.modem.bandwidth_hz=100;
    options.modem.integration_seconds=64;options.modem.scramble=true;
    options.search_seconds=6;options.dsp_workspace_bytes=128*1024*1024;
    modem::OscillatorSearchConfig policy;policy.lf={.0001,.05};policy.rf={0,0};policy.margin=3;
    options.modem.oscillator_search=policy;
    options.clock_sync=clock_sync::Policy{.0001,.001,0};options.audio_timing_error_seconds=.001;
    const auto draft=wire(3,options.modem);
    const auto full=simulation::estimate(draft,options,true,clean_channel(),{},1,false,100,4096,
        simulation::ReceiverWorkMode::hardware_fallback);
    const auto tight=simulation::estimate(draft,options,true,clean_channel(),{},1,false,100,4096,
        simulation::ReceiverWorkMode::hardware_timing_model);
    simulation::ReceiverTimingModel late_capture;
    late_capture.capture_error_seconds=.03;
    late_capture.capture_rate_uncertainty_fraction=0;
    late_capture.capture_seconds_per_frame=1./options.modem.sample_rate;
    const auto timing_fallback=simulation::estimate(draft,options,true,clean_channel(),{},1,false,100,4096,
        simulation::ReceiverWorkMode::hardware_timing_model,late_capture);
    check(!timing_fallback.timing_window_modeled,
        "capture uncertainty exceeding selected Audio error must retain Live's full-window fallback");
    near(timing_fallback.receiver_cpu_seconds,full.receiver_cpu_seconds,
        "unqualified capture timing must not discount full-window CPU work");
    near(timing_fallback.timing_hypotheses,full.timing_hypotheses,
        "unqualified capture timing must retain every arrival origin");
    options.clock_sync=clock_sync::Policy{.001,.4,0};options.audio_timing_error_seconds=.03;
    const auto wide=simulation::estimate(draft,options,true,clean_channel(),{},1,false,100,4096,
        simulation::ReceiverWorkMode::hardware_timing_model);
    check(full.receiver_workspace_supported&&tight.receiver_workspace_supported&&wide.receiver_workspace_supported&&
        tight.timing_window_modeled&&wide.timing_window_modeled,
        "eligible compact hardware timing model must retain its workspace and count the admitted prior");
    check(tight.frequency_rate_hypotheses==full.frequency_rate_hypotheses&&
        wide.frequency_rate_hypotheses==full.frequency_rate_hypotheses&&
        tight.epoch_hypotheses==full.epoch_hypotheses,
        "a timing prior must not omit oscillator, key or epoch coverage");
    check(tight.timing_hypotheses<=wide.timing_hypotheses&&wide.timing_hypotheses<full.timing_hypotheses&&
        tight.receiver_search_seconds<=wide.receiver_search_seconds&&wide.receiver_cpu_seconds<full.receiver_cpu_seconds,
        "narrowing an implemented compact prior must reduce actual modeled search work monotonically");
    near(tight.receiver_frontend_seconds,full.receiver_frontend_seconds,
        "timing pruning must not erase ingestion or shared pulse-statistic costs");
    near(tight.fallback_receiver_cpu_seconds,full.receiver_cpu_seconds,
        "compact timing estimate must retain a separate complete-window fallback");
    near(tight.fallback_timing_hypotheses,full.timing_hypotheses,
        "fallback must retain every original timing origin");
    auto invalid=late_capture;invalid.capture_seconds_per_frame=0;
    bool rejected=false;
    try {
        (void)simulation::estimate(draft,options,true,clean_channel(),{},1,false,100,4096,
            simulation::ReceiverWorkMode::hardware_timing_model,invalid);
    } catch(const Error&) {rejected=true;}
    check(rejected,"invalid supplied capture metadata must not produce a timing estimate");
}
void nearby_shift_workload() {
    for(const auto target:{18.,-3.}) {
        simulation::Estimate baseline;
        for(const auto shift:{0.,.001,10.,20.,30.}) {
            transfer::Options options;
            options.modem=tuning::resolve(100,target,tuning::PatternMode::auto_keystream,true,1500-shift).config;
            options.timestamp=1800000000;options.dsp_workspace_bytes=128*1024*1024;
            modem::OscillatorSearchConfig policy;
            policy.lf=policy.rf=modem::OscillatorModel{.0001,.005};policy.rf_shift_hz=shift;
            options.modem.oscillator_search=policy;
            const auto effects=modem::oscillator_effects(options.modem);
            auto channel=clean_channel();channel.clock_error_ppm=effects.clock_error_ppm;
            channel.frequency_offset_hz=effects.frequency_offset_hz;
            channel.phase_noise_degrees_per_sqrt_second=effects.phase_noise_degrees_per_sqrt_second;
            const auto estimate=simulation::estimate(wire(1,options.modem),options,true,channel,{},1,false);
            check(estimate.receiver_workspace_supported&&!estimate.oscillator_search_limited&&
                  estimate.carrier_in_search&&estimate.clock_in_search,
                  "small independent shifts must retain the declared oscillator region and receiver workspace");
            if(shift==0)baseline=estimate;
            else {
                check(estimate.frequency_rate_hypotheses==baseline.frequency_rate_hypotheses &&
                      estimate.pulse_projection_modeled==baseline.pulse_projection_modeled,
                      "a narrow independent shift must not inflate lanes or discard the projected backend");
                check(estimate.receiver_cpu_seconds<=1.05*baseline.receiver_cpu_seconds &&
                      estimate.receiver_cpu_seconds>=.95*baseline.receiver_cpu_seconds,
                      "small shifts at Rate100 must not create an artificial receiver CPU cliff");
            }
        }
        if(target<0)check(baseline.pulse_projection_modeled,
                         "long private symbol regression must exercise real projected admission");
    }
}
void bounded_affine_rf_workload() {
    // A wide RF uncertainty bank can fit the exact affine path while the
    // larger whole-chip Gram kernels cannot fit. Keep the full paired bank.
    transfer::Options rf;
    rf.modem=tuning::resolve(1,-24,tuning::PatternMode::auto_keystream,true,1500).config;
    modem::OscillatorSearchConfig policy;
    policy.lf=policy.rf={.0001,.005};policy.rf_shift_hz=30000000;
    rf.modem.oscillator_search=policy;rf.timestamp=0;rf.dsp_workspace_bytes=1999661056;
    std::array<std::uint8_t,32> key{};rf.key.emplace(key);
    const auto affine=simulation::estimate(wire(1,rf.modem),rf,true,clean_channel(),{},1,false);
    check(affine.frequency_rate_hypotheses==1181&&!affine.oscillator_search_limited&&
          affine.receiver_workspace_supported&&affine.pulse_segment_projection_modeled,
          "wide RF bank must model bounded affine fallback without dropping paired hypotheses");
    rf.dsp_workspace_bytes=16ULL*1024*1024*1024;
    const auto whole=simulation::estimate(wire(1,rf.modem),rf,true,clean_channel(),{},1,false);
    check(whole.frequency_rate_hypotheses==affine.frequency_rate_hypotheses&&
          whole.pulse_projection_modeled&&!whole.pulse_segment_projection_modeled,
          "ample workspace must preserve the existing whole-chip first choice and bank coverage");
}
transfer::Options affine_work_options(std::uint32_t fs,std::uint64_t chip,std::uint64_t chips) {
    transfer::Options options;
    options.modem.sample_rate=fs;options.modem.carrier_hz=fs==64?8:1500;
    options.modem.bandwidth_hz=std::nextafter(2.*fs/chip,std::numeric_limits<double>::infinity());
    options.modem.integration_seconds=static_cast<double>(chips*chip+1)/fs;
    options.modem.scramble=true;
    modem::OscillatorSearchConfig policy;policy.lf=policy.rf={0,0};
    options.modem.oscillator_search=policy;
    options.timestamp=1800000000;options.search_seconds=0;
    options.dsp_workspace_bytes=128*1024*1024;
    std::array<std::uint8_t,32> key{};key[0]=0x35;options.key.emplace(key);
    check(modem::pattern_chip_samples(options.modem)==chip&&
          modem::symbol_sample_count(options.modem)==chips*chip+1&&
          modem::pattern_pulse_enabled(options.modem),
          "affine work fixture must resolve the exact shaped partial geometry");
    return options;
}
long double affine_fallback_search(const simulation::Estimate& estimate,const modem::Config& config,
                                  long double block_samples,long double banks=1) {
    // This reference charges the preceding 1800 allowance on every real fit,
    // with the independently specified finite-pulse/partial-tail span bound.
    const auto chip=modem::pattern_chip_samples(config),symbol=modem::symbol_sample_count(config);
    const auto plan=modem::oscillator_pattern_search(config);
    const auto samples=static_cast<long double>(estimate.simulated_seconds)*config.sample_rate;
    long double maximum_rate=1,lanes=0;
    for(const auto& h:plan.hypotheses) {
        const auto rate=1+static_cast<long double>(h.clock_error_ppm)*1e-6L;
        maximum_rate=std::max(maximum_rate,rate);
        lanes+=std::ceil(4.L*config.sample_rate*rate/chip)+1;
    }
    const auto symbols=std::ceil(samples*maximum_rate/symbol);
    const auto tail=symbol%chip?257*symbols*std::min(9.L,std::ceil(static_cast<long double>(symbol)/chip)):0.L;
    const auto natural=std::min(samples,1+257*std::ceil(samples*maximum_rate/chip)+tail+
        symbols*(4+estimate.differential_windows));
    const auto fits=std::min(samples,std::ceil(samples/block_samples)+natural);
    const auto groups=symbol%config.sample_rate?2.L:1.L;
    const auto detector_work=128.L*((estimate.drift_sections>1)+bool(estimate.differential_windows));
    return fits*lanes*groups*(1800+detector_work)*banks/1.5e9L;
}
void affine_coefficient_workload() {
    // More than 16 chips keeps the shaped waveform. These exact partial
    // symbols reject the whole-chip backend without dropping a hypothesis.
    for(const auto chip:{8191ULL,8192ULL,8193ULL,16384ULL}) {
        auto options=affine_work_options(64,chip,16);
        const auto estimate=simulation::estimate(wire(1,options.modem),options,true,clean_channel(),{},1,false);
        check(estimate.receiver_workspace_supported&&estimate.pulse_segment_projection_modeled&&
              estimate.frequency_rate_hypotheses==1&&!estimate.oscillator_search_limited&&
              estimate.drift_sections==1&&estimate.differential_windows==0,
              "compact coefficient gate must retain shaped affine coverage and detector geometry");
        const auto fallback=affine_fallback_search(estimate,options.modem,32);
        if(chip<=8192)near(estimate.receiver_search_seconds,static_cast<double>(fallback),
            "short or equal-width spans must preserve all 1800 uncached work");
        else check(estimate.receiver_search_seconds<fallback,
            "long natural private intervals must reduce repeated preparation while retaining block fits");
    }
    // The complete oscillator bank's fastest clock, rather than its nominal
    // lane, determines whether a natural interval can outlive a block.
    for(const auto chip:{8193ULL,8194ULL}) {
        auto options=affine_work_options(64,chip,16);
        options.modem.oscillator_search->lf.accuracy_ppm=41;
        const auto plan=modem::oscillator_pattern_search(options.modem);
        long double fastest=1;
        for(const auto& h:plan.hypotheses)fastest=std::max(fastest,1+h.clock_error_ppm*1e-6L);
        const auto estimate=simulation::estimate(wire(1,options.modem),options,true,clean_channel(),{},1,false);
        check(estimate.receiver_workspace_supported&&estimate.pulse_segment_projection_modeled&&
              !estimate.oscillator_search_limited&&estimate.frequency_rate_hypotheses==plan.hypotheses.size(),
              "clock-width cache policy must retain the complete paired frequency/rate search");
        const auto fallback=affine_fallback_search(estimate,options.modem,32);
        if(chip==8193) {
            check(chip/(256*fastest)<=32,"clock-boundary fixture must cross the nominal-only cache width");
            near(estimate.receiver_search_seconds,static_cast<double>(fallback),
                 "fastest clock boundary must retain uncached work even when nominal intervals exceed32");
        } else {
            check(chip/(256*fastest)>32&&estimate.receiver_search_seconds<fallback,
                  "complete clock region strictly above the width gate must permit coefficient reuse");
        }
    }
    for(const auto extra:{0ULL,1ULL}) {
        auto options=affine_work_options(4096,8192,32);
        options.modem.integration_seconds=static_cast<double>(32*8192ULL+4096+extra)/4096;
        const auto estimate=simulation::estimate(wire(1,options.modem),options,true,clean_channel(),{},1,false);
        check(modem::symbol_sample_count(options.modem)==32*8192ULL+4096+extra&&
              estimate.pulse_segment_projection_modeled&&estimate.drift_sections==1&&estimate.differential_windows==0,
              "phase-group work fixture must retain exact partial-symbol geometry");
        near(estimate.receiver_search_seconds,static_cast<double>(affine_fallback_search(estimate,options.modem,32)),
             "each possible stream-phase group must retain its independent private preparation and fits");
    }
    // Non-hint compact receivers use 128-sample blocks. C8192 is insufficient
    // here; the strict width boundary is C32768 at the zero-clock hypothesis.
    for(const auto chip:{16384ULL,32768ULL,32769ULL}) {
        auto options=affine_work_options(32768,chip,32);options.dsp_workspace_bytes=8*1024*1024;
        const auto estimate=simulation::estimate(wire(1,options.modem),options,true,clean_channel(),{},1,false);
        check(modem::symbol_sample_count(options.modem)<60ULL*options.modem.sample_rate&&
              estimate.receiver_workspace_supported&&estimate.pulse_segment_projection_modeled&&
              estimate.frequency_rate_hypotheses==1&&estimate.drift_sections==1&&estimate.differential_windows==0,
              "non-hint affine work fixture must use its actual 128-sample block policy");
        const auto fallback=affine_fallback_search(estimate,options.modem,128);
        if(chip<=32768)near(estimate.receiver_search_seconds,static_cast<double>(fallback),
            "128-sample equality must not receive 32-sample cache credit");
        else check(estimate.receiver_search_seconds<fallback,
            "strictly longer natural intervals must admit non-hint coefficient reuse");
    }
    auto options=affine_work_options(4096,16384,16);
    const auto draft=wire(1,options.modem);
    const auto ample=simulation::estimate(draft,options,true,clean_channel(),{},1,false);
    // This is the planner's deliberately conservative fixed allowance, not an
    // assertion about exact heap bytes. No optional detector is eligible here.
    constexpr long double lanes=2,groups=2,block=32;
    const auto fixed=256*1024.L+lanes*(512+(groups-1)*sizeof(std::array<modem::detail::CorrelationFit,2>)+2)+
        64+(block+1)*sizeof(modem::detail::CorrelationProjection)+32*sizeof(modem::PatternEvidence)+
        64*sizeof(std::complex<double>)+sizeof(modem::PatternFrequencyRateHypothesis)+
        (block+1)*sizeof(std::complex<double>)+4*sizeof(modem::detail::CorrelationCarrierMoments)+
        lanes*groups*sizeof(std::uint64_t);
    const auto admitted=2*static_cast<std::size_t>(std::ceil(fixed+80*lanes*groups+2*lanes+34));
    options.dsp_workspace_bytes=admitted-1;
    const auto low=simulation::estimate(draft,options,true,clean_channel(),{},1,false);
    options.dsp_workspace_bytes=admitted;
    const auto high=simulation::estimate(draft,options,true,clean_channel(),{},1,false);
    for(const auto* value:{&low,&high})check(value->receiver_workspace_supported&&
        value->pulse_segment_projection_modeled&&value->frequency_rate_hypotheses==ample.frequency_rate_hypotheses&&
        value->oscillator_search_limited==ample.oscillator_search_limited&&
        value->drift_sections==ample.drift_sections&&value->differential_windows==ample.differential_windows,
        "optional cache memory gate must preserve mandatory geometry and detector coverage");
    near(low.receiver_search_seconds,static_cast<double>(affine_fallback_search(low,options.modem,32)),
         "insufficient optional cache workspace must retain the complete uncached work");
    near(high.receiver_search_seconds,ample.receiver_search_seconds,
         "admitting the optional cache must not depend on reserving every configured retained bit");
    check(high.receiver_search_seconds<low.receiver_search_seconds,
          "workspace admission must reduce actual private preparation modeled work");
    near(high.receiver_frontend_seconds,low.receiver_frontend_seconds,
         "coefficient reuse must not erase original-sample frontend work");
    near(high.receiver_kernel_rebuild_seconds,low.receiver_kernel_rebuild_seconds,
         "coefficient reuse must not erase carrier covariance/kernel work");
    near(high.tracking_symbol_windows,low.tracking_symbol_windows,
         "optional memory admission must preserve physical continuation/absence windows");

    // Same transmitted geometry at twice Fs preserves preparation cadence,
    // but still doubles original-sample ingestion and increases per-block fits.
    options.dsp_workspace_bytes=128*1024*1024;
    const auto base=simulation::estimate(draft,options,true,clean_channel(),{},1,false);
    auto scaled=options;scaled.modem.sample_rate*=2;scaled.modem.integration_seconds=options.modem.integration_seconds;
    check(modem::pattern_chip_samples(scaled.modem)==2*modem::pattern_chip_samples(options.modem)&&
          modem::symbol_sample_count(scaled.modem)==2*modem::symbol_sample_count(options.modem),
          "fixed waveform scaling fixture must preserve chip and symbol time");
    const auto larger=simulation::estimate(wire(1,scaled.modem),scaled,true,clean_channel(),{},1,false);
    near(larger.simulated_seconds,base.simulated_seconds,"paired work-model sample rates must preserve physical duration");
    check(larger.pulse_segment_projection_modeled&&larger.frequency_rate_hypotheses==base.frequency_rate_hypotheses&&
          larger.receiver_frontend_seconds>base.receiver_frontend_seconds&&
          larger.receiver_search_seconds>base.receiver_search_seconds&&
          larger.receiver_search_seconds<2*base.receiver_search_seconds,
          "private preparation must follow natural intervals while block fits and frontend still follow Fs");
    near(larger.receiver_kernel_rebuild_seconds,base.receiver_kernel_rebuild_seconds,
         "unchanged natural pulse geometry must retain the same carrier moment allowance");

    // Geometry reuse never reuses a private candidate across keys or epochs.
    for(const auto keys:{2U,3U}) {
        const auto multiple=simulation::estimate(draft,options,true,clean_channel(),{},keys,false);
        near(multiple.receiver_search_seconds,keys*base.receiver_search_seconds,"each key must retain private preparation and fitting work");
        near(multiple.receiver_frontend_seconds,keys*base.receiver_frontend_seconds,"central estimate must charge each key frontend conservatively");
        near(multiple.receiver_kernel_rebuild_seconds,keys*base.receiver_kernel_rebuild_seconds,"each key bank must retain covariance preparation work");
        check(multiple.frequency_rate_hypotheses==base.frequency_rate_hypotheses&&multiple.receiver_workspace_supported,
              "additional keys must not reduce one bank's complete hypothesis coverage");
    }
    auto unknown=options;unknown.timestamp=0;
    const auto epochs=1+std::ceil((static_cast<long double>(modem::training_sample_count(options.modem))+
        modem::pattern_pulse_padding_samples(options.modem))/options.modem.sample_rate);
    const auto unanchored=simulation::estimate(draft,unknown,true,clean_channel(),{},1,false);
    check(unanchored.receiver_workspace_supported&&unanchored.pulse_segment_projection_modeled,
          "unknown epoch work fixture must retain complete affordable private banks");
    near(unanchored.receiver_search_seconds,static_cast<double>(epochs*base.receiver_search_seconds),
         "every possible epoch must retain fresh private coefficient preparation");
    auto other=options.modem;other.spreading_seed.fill(0xa7);other.stream_epoch+=9;other.stream_phase_samples=17;
    const std::array identical{options.modem,options.modem};
    const std::array independent{options.modem,other};
    const auto repeated=simulation::estimate(draft,options,true,clean_channel(),identical,1,false);
    const auto fresh=simulation::estimate(draft,options,true,clean_channel(),independent,1,false);
    near(fresh.receiver_search_seconds,repeated.receiver_search_seconds,
         "private identities can share geometry calculation without sharing secret coefficients");
    near(fresh.receiver_search_seconds,2*base.receiver_search_seconds,
         "each profile must retain its own complete private search cost");
}
void equivalent_receive_profiles() {
    bounded_affine_rf_workload();
    transfer::Options options;options.modem.scramble=true;
    options.modem.integration_seconds=.25;options.timestamp=1800000000;
    options.dsp_workspace_bytes=128*1024*1024;
    modem::OscillatorSearchConfig policy;policy.rf_shift_hz=1000000;
    options.modem.oscillator_search=policy;
    const auto channel=clean_channel();const auto draft=wire(3,options.modem);
    auto other_key=options.modem;
    other_key.spreading_seed.fill(0x35);other_key.dsss_seed.fill(0xa7);
    other_key.stream_epoch+=9;other_key.stream_phase_samples=17;
    const std::array identical{options.modem,options.modem};
    const std::array different_keys{options.modem,other_key};
    const auto reference=simulation::estimate(draft,options,true,channel,identical);
    const auto equivalent=simulation::estimate(draft,options,true,channel,different_keys);
    for(const auto member:{&simulation::Estimate::cpu_seconds,&simulation::Estimate::receiver_cpu_seconds,
            &simulation::Estimate::receiver_frontend_seconds,&simulation::Estimate::receiver_search_seconds,
            &simulation::Estimate::receiver_kernel_rebuild_seconds,&simulation::Estimate::gpu_seconds,
            &simulation::Estimate::tracking_seconds,&simulation::Estimate::tracking_symbol_windows,
            &simulation::Estimate::carrier_search_half_width_hz,&simulation::Estimate::clock_search_half_width_ppm,
            &simulation::Estimate::modeled_symbol_snr_db,&simulation::Estimate::success_probability})
        near(equivalent.*member,reference.*member,
             "equivalent key/epoch profiles must preserve modeled work, coverage and probability");
    check(equivalent.receiver_profiles==2&&equivalent.profile_matches&&
          equivalent.frequency_rate_hypotheses==reference.frequency_rate_hypotheses&&
          equivalent.receiver_workspace_supported==reference.receiver_workspace_supported&&
          equivalent.confidence_available==reference.confidence_available,
          "local geometry reuse must preserve receive profile count and support diagnostics");
    const auto single=simulation::estimate(draft,options,true,channel);
    check(equivalent.receiver_cpu_seconds>single.receiver_cpu_seconds,
          "reusing the estimate calculation must still charge each equivalent receive bank");

    // Same waveform identity does not imply the same receiver search policy.
    // The first bank cannot cover the test offset; the second matching bank can.
    auto narrow=options.modem;
    narrow.oscillator_search->lf.accuracy_ppm=.0001;
    narrow.oscillator_search->rf.accuracy_ppm=.0001;
    auto offset_channel=channel;offset_channel.frequency_offset_hz=10;
    const std::array narrow_first{narrow,options.modem};
    const std::array wide_first{options.modem,narrow};
    const auto selected=simulation::estimate(draft,options,true,offset_channel,narrow_first,1,false);
    const auto reordered=simulation::estimate(draft,options,true,offset_channel,wide_first,1,false);
    const auto wide_plan=modem::oscillator_pattern_search(options.modem);
    check(selected.carrier_in_search&&selected.clock_in_search&&
          selected.frequency_rate_hypotheses==wide_plan.hypotheses.size()&&
          selected.frequency_rate_hypotheses==reordered.frequency_rate_hypotheses,
          "matching waveforms with different oscillator policies must keep distinct coverage banks");
    near(selected.requested_carrier_search_half_width_hz,wide_plan.frequency.requested_half_width_hz,
         "selected diagnostic must retain the covering receiver policy");
    near(selected.receiver_cpu_seconds,reordered.receiver_cpu_seconds,
         "receive profile order must not change total modeled work");

    options.timestamp=0;
    const auto ordinary=simulation::estimate(draft,options,true,channel,identical,1,false);
    const auto epochs=simulation::estimate(draft,options,true,channel,different_keys,1,false);
    near(epochs.receiver_cpu_seconds,ordinary.receiver_cpu_seconds,
         "equivalent private banks must retain the same unknown-epoch work");
    auto invalid_training=options.modem;invalid_training.training_seconds+=3;
    const std::array invalid_duplicate{options.modem,invalid_training};
    bool rejected=false;
    try {(void)simulation::estimate(draft,options,true,channel,invalid_duplicate,1,false);}
    catch(const Error&) {rejected=true;}
    check(rejected,"a cached geometry must not bypass validation of a later receive profile");
    // A call with unrelated profiles still uses the transmit geometry for its
    // diagnostic bank, without retaining any bank from preceding estimates.
    auto unrelated=options.modem;unrelated.spreading_factor*=2;unrelated.integration_seconds=.5;
    const auto unmatched=simulation::estimate(draft,options,true,channel,std::span(&unrelated,1),1,false);
    check(!unmatched.profile_matches&&!unmatched.confidence_available&&
          unmatched.frequency_rate_hypotheses==wide_plan.hypotheses.size(),
          "unmatched receive profiles must retain independent transmit diagnostic geometry");
}
void projected_pattern_workload() {
    transfer::Options options;
    options.timestamp=1800000000;options.search_seconds=0;
    options.dsp_workspace_bytes=64*1024*1024;
    options.modem.sample_rate=400;options.modem.carrier_hz=50;
    options.modem.bandwidth_hz=100;options.modem.integration_seconds=64;
    options.modem.scramble=true;
    modem::OscillatorSearchConfig oscillator;oscillator.lf={0,0};oscillator.rf={0,0};
    options.modem.oscillator_search=oscillator;
    const auto low=simulation::estimate(wire(3,options.modem),options,true,clean_channel(),{},1,false);
    check(low.receiver_workspace_supported&&low.pulse_projection_modeled&&!low.kernel_rebuild_upper_bound&&
          low.receiver_frontend_seconds>0&&low.receiver_search_seconds>0&&low.receiver_kernel_rebuild_seconds>0,
          "eligible long shaped symbols must model sample frontend, chip search and cached Gram work separately");
    check(low.receiver_frontend_seconds+low.receiver_search_seconds+low.receiver_kernel_rebuild_seconds+
          low.tracking_seconds+low.payload_processing_seconds<=low.receiver_cpu_seconds,
          "receive components must be included once within the total CPU allowance");
    const auto synthetic_serial=(low.cpu_seconds-low.receiver_cpu_seconds);
    // Both models leave projected core on the caller; the GPU adds setup and
    // transfer allowances, rather than accelerating this serial search work.
    check(low.gpu_seconds>=low.receiver_frontend_seconds+low.receiver_search_seconds+
          low.receiver_kernel_rebuild_seconds+synthetic_serial,
          "the hypothetical GPU total must retain all serial projected search work");
    near(low.tracking_symbol_windows,3,"compact planning must retain logical desired-stream continuation and absence count");
    near(low.tracking_seconds,0,"compact continuation scoring must not acquire duplicate serial tracking work");
    options.modem.sample_rate=6000;options.modem.carrier_hz=1500;
    const auto high=simulation::estimate(wire(3,options.modem),options,true,clean_channel(),{},1,false);
    check(high.pulse_projection_modeled&&high.receiver_frontend_seconds>low.receiver_frontend_seconds&&
          high.receiver_kernel_rebuild_seconds>low.receiver_kernel_rebuild_seconds,
          "higher real sample rates must retain their frontend and kernel setup allowance");
    near(high.receiver_search_seconds,low.receiver_search_seconds,
         "private chip search with equal bandwidth, duration and bank must not grow with carrier or sample rate");
    options.modem.oscillator_search->lf.accuracy_ppm=.001;
    const auto fractional=simulation::estimate(wire(3,options.modem),options,true,clean_channel(),{},1,false);
    check(fractional.pulse_projection_modeled&&fractional.kernel_rebuild_upper_bound&&
          fractional.receiver_kernel_rebuild_seconds>high.receiver_kernel_rebuild_seconds&&
          fractional.receiver_kernel_rebuild_upper_seconds>fractional.receiver_kernel_rebuild_seconds,
          "fractional-rate projection must separate central cache work from its conservative rebuild allowance");
    options.modem.oscillator_search=oscillator;
    options.modem.integration_seconds=64+1./options.modem.sample_rate;
    const auto partial=simulation::estimate(wire(3,options.modem),options,true,clean_channel(),{},1,false);
    check(partial.receiver_workspace_supported&&!partial.pulse_projection_modeled&&partial.receiver_kernel_rebuild_seconds==0,
          "partial chip/quarter geometry must model the full raw shaped fallback");
    options.modem.integration_seconds=64;options.dsp_workspace_bytes=64*1024;
    const auto unaffordable=simulation::estimate(wire(3,options.modem),options,true,clean_channel(),{},1,false);
    check(!unaffordable.receiver_workspace_supported&&!unaffordable.pulse_projection_modeled&&!unaffordable.confidence_available,
          "a compact bank whose mandatory state cannot fit must not claim supported receiver coverage");
    options.dsp_workspace_bytes=64*1024*1024;
    options.modem.bandwidth_hz=.01;options.modem.integration_seconds=6400;
    options.modem.sample_rate=64;options.modem.carrier_hz=.005;
    const auto narrow=simulation::estimate(wire(1,options.modem),options,true,clean_channel(),{},1,false);
    options.modem.sample_rate=6000;options.modem.carrier_hz=1500;
    const auto oversampled=simulation::estimate(wire(1,options.modem),options,true,clean_channel(),{},1,false);
    check(narrow.pulse_projection_modeled && oversampled.pulse_projection_modeled,
          "long-chip affine statistics must replace the former 4096-sample eligibility limit");
    near(narrow.receiver_search_seconds,oversampled.receiver_search_seconds,
         "private long-chip search must follow chip count rather than nominal carrier sample rate");
    check(oversampled.receiver_kernel_rebuild_seconds<2*narrow.receiver_kernel_rebuild_seconds,
          "long-chip Gram work must follow table pieces and logarithmic geometric moments, not PCM chip length");
}
}
int main(int argc,char** argv) {
    try {
        advisory_cancellation();
        if(argc==2&&std::string(argv[1])=="--cancellation-only") {std::cout<<"advisory cancellation tests passed\n";return 0;}
        if(argc==2&&std::string(argv[1])=="--rolling-epochs-only") {
            rolling_fft_epoch_workload();std::cout<<"rolling FFT epoch work tests passed\n";return 0;
        }
        if(argc==2&&std::string(argv[1])=="--utc-only") {
            utc_bank_estimate();clock_dsss_reference();rolling_fft_epoch_workload();compact_clock_prior_workload();std::cout<<"UTC bank estimate tests passed\n";return 0;
        }
        if(argc==2&&std::string(argv[1])=="--affine-work-only") {
            bounded_affine_rf_workload();affine_coefficient_workload();projected_pattern_workload();
            std::cout<<"affine work model tests passed\n";return 0;
        }
        if(argc==2&&std::string(argv[1])=="--partial-only") {
            partial_compact_probability();std::cout<<"partial simulation estimate tests passed\n";return 0;
        }
        utc_bank_estimate();clock_dsss_reference();rolling_fft_epoch_workload();compact_clock_prior_workload();probability_and_framing();workload_and_impairments();receiver_cpu_budget();received_processing_budget();whole_symbol_phase_coherence();partial_compact_probability();differential_model_limits();drift_receiver_estimate();receiver_statistic_controls();raw_sample_probability_geometry();established_tracking_workload();complete_symbol_absence();narrow_band_carrier_coverage();
        coupled_and_independent_clock_estimates();streamed_template_workload();target_and_channel_are_independent();oscillator_policy_geometry();nearby_shift_workload();equivalent_receive_profiles();affine_coefficient_workload();projected_pattern_workload();
        std::cout<<"simulation estimate tests passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<"simulation estimate tests failed: "<<error.what()<<'\n';return 1;}
}
