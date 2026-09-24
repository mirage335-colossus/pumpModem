#include "datapump/simulation_estimate.hpp"
#include "datapump/boundary_sync.hpp"
#include "datapump/channel.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/pattern_receiver.hpp"
#include "datapump/pattern_search.hpp"
#include "../src/receiver_probability.hpp"
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
    near(value.modeled_symbol_snr_db,20,
         "the joint statistic already models receiver noise and must not retain an arbitrary three-dB margin");
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
}
int main() {
    try {probability_and_framing();workload_and_impairments();receiver_cpu_budget();received_processing_budget();whole_symbol_phase_coherence();differential_model_limits();drift_receiver_estimate();receiver_statistic_controls();raw_sample_probability_geometry();established_tracking_workload();complete_symbol_absence();narrow_band_carrier_coverage();
        coupled_and_independent_clock_estimates();streamed_template_workload();target_and_channel_are_independent();
        std::cout<<"simulation estimate tests passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<"simulation estimate tests failed: "<<error.what()<<'\n';return 1;}
}
