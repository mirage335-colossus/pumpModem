#include "datapump/simulation_estimate.hpp"
#include "datapump/boundary_sync.hpp"
#include "datapump/channel.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/pattern_receiver.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
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
void complete_symbol_absence() {
    transfer::Options options;options.modem.integration_seconds=4*60*60;
    auto channel=clean_channel();
    const auto value=wire(1,options.modem);
    const auto result=simulation::estimate(value,options,true,channel);
    const auto tail=result.simulated_seconds-value.total_seconds;
    const auto required=4*60*60+1+.175+2.*modem::pattern_pulse_padding_samples(options.modem)/options.modem.sample_rate;
    near(tail,required,"four-hour symbol needs a complete four-hour absent symbol plus lookahead");
    check(std::isfinite(result.cpu_seconds) && std::isfinite(result.gpu_seconds),"long sampled estimates must remain finite");
    channel.snr_db=std::numeric_limits<double>::quiet_NaN();
    bool rejected=false;
    try {(void)simulation::estimate(value,options,true,channel);}catch(const Error&){rejected=true;}
    check(rejected,"invalid channel input must be rejected");
}
std::optional<transfer::Received> sampled_reception(const Message& message,const transfer::Options& options,
                                                   const modem::ChannelConfig& channel,bool observe_absence=true) {
    const auto config=transfer::seeded_config(options,options.timestamp);
    auto source=transfer::message_transmitter(message,options);
    modem::PatternSearch search;
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
    const auto outside=simulation::estimate(transmission,options,true,channel);
    check(outside.profile_matches && !outside.carrier_in_search && !outside.confidence_available,
          "100 ppm narrow-band carrier mismatch must not display a success percentage");
    near(outside.carrier_offset_hz,.15,"default clock mismatch must include the 1500 Hz carrier shift");
    near(outside.carrier_search_half_width_hz,.00390625,"carrier bank must use the actual five symbol-scaled offsets");
    check(std::isfinite(outside.cpu_seconds) && outside.cpu_seconds>0 &&
          std::isfinite(outside.gpu_seconds) && outside.gpu_seconds>0,
          "unavailable confidence must preserve reference simulation time estimates");
    const auto failed=sampled_reception(message,options,channel);
    check(!failed,"default narrow-band channel must reproduce failure to admit any message");

    channel.snr_db+=60;
    check(!simulation::estimate(transmission,options,true,channel).confidence_available,
          "stronger signal cannot restore confidence outside the receiver carrier search");
    channel.clock_error_ppm=-100;
    const auto negative=simulation::estimate(transmission,options,true,channel);
    check(!negative.carrier_in_search && !negative.confidence_available,"negative carrier offsets need the same coverage gate");
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

    constexpr double edge=.00390625;
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
    const auto quantized_edge=.5*options.modem.sample_rate/768001.;
    near(quantized.carrier_search_half_width_hz,quantized_edge,"search width must follow sample-quantized symbol duration");
    channel.frequency_offset_hz=(quantized_edge+.5/options.modem.integration_seconds)/2;
    check(!simulation::estimate(wire(3,options.modem),options,true,channel).confidence_available,
          "unquantized duration must not enlarge the actual receiver carrier bank");
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
    try {probability_and_framing();workload_and_impairments();complete_symbol_absence();narrow_band_carrier_coverage();
        target_and_channel_are_independent();
        std::cout<<"simulation estimate tests passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<"simulation estimate tests failed: "<<error.what()<<'\n';return 1;}
}
