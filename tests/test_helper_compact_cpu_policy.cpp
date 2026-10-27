#include "helper_compact_cpu_admission.hpp"
#include "helper_reference_search.hpp"
#include "datapump/transfer.hpp"
#include "datapump/simulation_estimate.hpp"
#include "datapump/live.hpp"
#include <iostream>
#include <limits>

using namespace datapump;
using namespace datapump::modem;
namespace {
void check(bool v,const char* message){if(!v)throw Error(message);}
Config geometry() {
    Config c;c.sample_rate=6000;c.carrier_hz=1500;c.bandwidth_hz=1;c.spreading_factor=64;
    c.integration_seconds=128;c.pattern_symbols=true;c.constellation_bits=1;c.scramble=true;
    c.stream_epoch=1800000000;c.spreading_seed[3]=51;c.data_key=Crypto(c.spreading_seed);
    OscillatorSearchConfig p;p.lf.accuracy_ppm=p.rf.accuracy_ppm=.0001;
    p.margin=3;p.rf_shift_hz=5.8e9;p.reference=OscillatorReference::independent_audio;
    c.oscillator_search=p;c.receiver_cpu_budget=.6;
    // Match the actual EME Fake-FCC waveform/Auto policy. Without this hop
    // plan the diagnostic legitimately permits frequency-axis/amplitude Auto
    // operators which are unavailable to the production continuous FHSS case.
    FhssPlan hopping;hopping.channels=75;hopping.dwell_seconds=.4;
    for(unsigned i=0;i<hopping.channels;++i)hopping.shifts[i]=5.8e9+25000*i;
    transfer::Options options;options.modem=c;options.key=c.data_key;options.audio_timing_error_seconds=0;
    c.fake_hopping=transfer::transmit_hopping(hopping,options);
    check(c.fake_hopping->smooth_ramps&&!deferred_symbols(c)&&!c.fake_hopping->matched_gaps,
        "actual long EME fixture lost its continuous Fake-FCC power policy");
    return c;
}
PatternSearch search(const Config& c,double uncertainty) {
    PatternSearch s;
    apply_helper_reference_search(helper_reference_search(c,uncertainty,2450.3125,0,
        utc_transmit_rate_limit(c),HelperReferenceClockRelation::common_corrected_source,{},
        PatternUtcClockGrid::complete_expanded_domain),s);
    s.compact_clock_search=s.helper_wait_for_shared_pulse=true;
    s.start_offset_seconds=7;s.start_uncertainty_seconds=7;
    s.receiver_bank_count=receiver_strategy_bank_count(c,6,1,1);s.worker_threads=1;
    return s;
}
void policy() {
    auto c=geometry();transfer::Options options;options.modem=c;options.key=c.data_key;
    options.dsp_workspace_bytes=128U*1024*1024;
    const auto catalogue=helper_reference_search(c,.07515,2450.3125,0,utc_transmit_rate_limit(c),
        HelperReferenceClockRelation::common_corrected_source,{},PatternUtcClockGrid::complete_expanded_domain).catalogue;
    transfer::Estimate transmission;transmission.wire_bits=1;
    transmission.total_seconds=static_cast<double>(training_sample_count(c)+
        2*pattern_pulse_padding_samples(c)+suppression_sample_count(c)+symbol_sample_count(c))/c.sample_rate;
    ChannelConfig channel;channel.clock_error_ppm=0;channel.phase_noise_degrees_per_sqrt_second=0;
    const auto old=simulation::estimate_with_receiver_search(transmission,options,true,channel,catalogue);
    const auto off=simulation::estimate_with_receiver_search(transmission,options,true,channel,catalogue,
        1,100,simulation::ReceiverWorkMode::sampled_simulation,{},nullptr,{},nullptr,false);
    const auto on=simulation::estimate_with_receiver_search(transmission,options,true,channel,catalogue,
        1,100,simulation::ReceiverWorkMode::sampled_simulation,{},nullptr,{},nullptr,true);
    const std::string marker="helper shared-pulse CPU estimate uses serial caller work";
    check(old.receiver_geometry.receiver_strategy_reason==off.receiver_geometry.receiver_strategy_reason &&
        old.receiver_geometry.receiver_strategy_reason.find(marker)==std::string::npos,
        "old/default work-only API gained helper shared-pulse policy");
    check(on.receiver_geometry.receiver_strategy_reason.find(marker)!=std::string::npos,
        "explicit helper policy was not propagated through simulation layout/resolver");
    live::Settings settings;settings.transfer=options;settings.simulation=true;
    settings.dsp_workspace_bytes=options.dsp_workspace_bytes;settings.helper.power_w=15000;
    const auto enabled_only=simulation::estimate_with_receiver_search(transmission,options,true,channel,catalogue,
        1,100,simulation::ReceiverWorkMode::sampled_simulation,{},&settings);
    check(enabled_only.receiver_geometry.receiver_strategy_reason.find(marker)==std::string::npos,
        "enabled power alone acquired explicit helper shared-pulse policy");
    std::cout<<"old/default/explicit false unchanged; explicit true policy and enabled-power-only control passed\n";
}
}
int main(){try{policy();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
