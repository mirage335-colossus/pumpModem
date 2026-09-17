#include "datapump/simulation_estimate.hpp"
#include "datapump/boundary_sync.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/pattern_receiver.hpp"
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
    check(!no_profile.profile_matches && no_profile.success_probability==0,"incompatible RX profile cannot show successful reception");
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
}
int main() {
    try {probability_and_framing();workload_and_impairments();complete_symbol_absence();
        std::cout<<"simulation estimate tests passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<"simulation estimate tests failed: "<<error.what()<<'\n';return 1;}
}
