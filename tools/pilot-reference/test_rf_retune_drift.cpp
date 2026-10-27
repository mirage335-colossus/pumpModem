#include "datapump/channel.hpp"
#include "datapump/helper_tone_tracker.hpp"
#include "datapump/transfer.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <set>
#include <string_view>
#include <vector>
using namespace datapump;
namespace {
void require(bool ok,const char* why){if(!ok)throw Error(why);}
modem::Config configuration() {
    modem::Config c;c.sample_rate=8000;c.carrier_hz=1500;c.bandwidth_hz=200;c.spreading_factor=128;c.pulse_shaping=false;
    c.oscillator_search=modem::OscillatorSearchConfig{{100,0},{2,0},7e6,3};return c;
}
helper::Options helper_options(){helper::Options h;h.power_w=40;h.data_power_w=4;h.callsign="N0CALL";h.timecode=true;return h;}
transfer::Options source_options(const modem::Config& c){transfer::Options o;o.modem=c;o.timestamp=1800000000;o.key=Crypto(std::array<std::uint8_t,32>{});return o;}
modem::ChannelConfig impairment(const modem::Config& c,const helper::Options& h,int side,double cn0=60) {
    modem::ChannelConfig i;i.clock_error_ppm=100;i.frequency_offset_hz=side*7.3;i.seed=5181;
    i.snr_db=cn0-10*std::log10(c.sample_rate/2.)-10*std::log10(h.power_w/h.data_power_w)-
        20*std::log10(modem::outer_dsss_transmit_gain(c))-10*std::log10(helper::pilot_power_fraction);return i;
}
modem::SampledSimulationChannel::RfRetuneSimulation retunes(int side,double slew=0) {
    modem::SampledSimulationChannel::RfRetuneSimulation r;r.rf_shift_hz={7e6,21e6,35e6,49e6};
    r.phase_seed=49273;r.sideband_sign=side;r.rf_fractional_slew_per_second=slew;return r;
}
void zero_output(const char* path) {
    const auto c=configuration();const auto h=helper_options();auto i=impairment(c,h,1);i.phase_noise_degrees_per_sqrt_second=.5;
    const auto o=source_options(c);auto source=transfer::binary_transmitter(Bytes(64,1),o);
    modem::SampledSimulationChannel channel(c,i,h);channel.set_helper_time_key(o.key);channel.set_rf_retune_simulation(retunes(1));
    std::vector<float> raw,logical;std::uint64_t next=0;std::size_t segments=0;
    channel.set_physical_observer([&](std::span<const float> x,std::uint32_t,std::uint64_t first) {
        require(first==next,"zero path raw dates changed");next+=x.size();raw.insert(raw.end(),x.begin(),x.end());
    });
    channel.set_physical_rf_observer([&](const auto&,std::uint32_t){++segments;});
    std::array<float,257> block{};
    while(logical.size()<2*c.sample_rate) {
        const auto n=channel.read(*source,std::span(block).first(std::min<std::size_t>(block.size(),2*c.sample_rate-logical.size())));
        require(n,"zero source ended");logical.insert(logical.end(),block.begin(),block.begin()+n);
    }
    std::ofstream output(path,std::ios::binary);const std::array<std::uint64_t,3> counts{raw.size(),logical.size(),segments};
    output.write(reinterpret_cast<const char*>(counts.data()),sizeof(counts));
    output.write(reinterpret_cast<const char*>(raw.data()),raw.size()*sizeof(float));
    output.write(reinterpret_cast<const char*>(logical.data()),logical.size()*sizeof(float));require(bool(output),"zero output write failed");
    std::cout<<"rf_drift_zero_output,raw_samples="<<raw.size()<<",logical_samples="<<logical.size()<<",segments="<<segments<<'\n';
}
void drift_case(int side,double slew) {
    const auto c=configuration();const auto h=helper_options();const auto i=impairment(c,h,side);const auto o=source_options(c);
    auto source=transfer::binary_transmitter(Bytes(64,1),o);modem::SampledSimulationChannel channel(c,i,h);channel.set_helper_time_key(o.key);
    const auto p=helper::plan(h,helper::placement_band(c),120000000,0,modem::outer_dsss_transmit_gain(c));
    // Retain this transport discriminator's explicit zero tracker process
    // allowance while injecting ChannelConfig's default phase noise. It is a
    // labelled stress check, not matched-process or thermal-only precision.
    const auto rate=channel.physical_sample_rate();helper::ToneTrackerConfig tc{rate,p.frequency_hz,320,0,.5,true};
    helper::ToneTracker tracker(tc,helper::ToneTracker::workspace_required(tc));
    modem::SampledSimulationChannel::PhysicalRfSegment segment;std::size_t fits=0,escapes=0,hops=0;double max_error=0;
    std::set<std::uint64_t> fitted;std::uint64_t next=0,last_context=0;channel.set_rf_retune_simulation(retunes(side,slew));
    channel.set_physical_rf_observer([&](const auto& s,std::uint32_t observed_rate) {
        require(observed_rate==rate && s.first_sample==next && s.sideband_sign==side,"RF descriptor lost its causal sample boundary");
        segment=s;++hops;tracker.begin_pilot_segment(s.first_sample,p.frequency_hz,320,false,1,s.settled_first_sample,true);
        require(!tracker.snapshot().qualified,"retune retained an unknown old phase/frequency certificate");
    });
    channel.set_physical_observer([&](std::span<const float> x,std::uint32_t,std::uint64_t first) {
        require(first==next,"RF drift source dropped raw samples");next+=x.size();tracker.push(x,first);const auto fit=tracker.snapshot();
        if(!fit.qualified || fit.frequency_context_end_sample==last_context)return;last_context=fit.frequency_context_end_sample;
        require(fit.frequency_local_aperture && fit.first_sample>=segment.settled_first_sample && fit.sample<segment.end_sample &&
            fit.frequency_context_first_sample==fit.first_sample && fit.frequency_context_end_sample==fit.sample+1,
            "RF drift fit crossed an independent retune or settling interval");
        ++fits;fitted.insert(segment.phase_segment);
        for(const auto date:std::array{fit.first_sample,fit.sample}) {
            const auto q=fit.frequency_at(date);require(q.valid,"local RF drift fit lost an observed endpoint");
            const auto truth=p.frequency_hz*(1+i.clock_error_ppm*1e-6)+i.frequency_offset_hz*segment.rf_shift_hz/7e6+
                side*segment.rf_shift_hz*slew*(static_cast<double>(date)/rate);
            const auto error=std::abs(q.frequency_hz-truth);max_error=std::max(max_error,error);escapes+=error>q.uncertainty_hz;
        }
    });
    std::array<float,257> discarded{};std::uint64_t count=0;
    while(count<8ULL*c.sample_rate){const auto n=channel.read(*source,std::span(discarded).first(std::min<std::uint64_t>(discarded.size(),8ULL*c.sample_rate-count)));require(n,"drift source ended");count+=n;}
    std::cout<<std::setprecision(12)<<"rf_drift_actual_channel,sideband="<<side<<",RF_fractional_slew_per_second="<<slew
        <<",sampled_pilot_cn0_target_db_hz=60,truth_frequency_seed=0,independent_hop_phases=1,hops="<<hops<<",fits="<<fits
        <<",injected_phase_noise_degrees_per_sqrt_second="<<i.phase_noise_degrees_per_sqrt_second
        <<",tracker_phase_noise_radians_per_sqrt_second="<<tc.phase_noise_radians_per_sqrt_second
        <<",unmodelled_phase_noise_stress=1"
        <<",qualified_hops="<<fitted.size()<<",endpoint_escapes="<<escapes<<",max_endpoint_error_hz="<<max_error<<",raw_samples="<<next
        <<",tracker_bytes="<<tracker.workspace_bytes()<<'\n';
    require(fitted.size()>=12 && !escapes,"actual RF drift transport failed independent local-fit endpoint coverage");
}
void invalid_fields() {
    const auto c=configuration();const auto h=helper_options();auto r=retunes(1);
    r.rf_fractional_slew_per_second=std::numeric_limits<double>::quiet_NaN();
    bool rejected=false;try {modem::SampledSimulationChannel ch(c,impairment(c,h,1),h);ch.set_rf_retune_simulation(r);}catch(const Error&){rejected=true;}
    require(rejected,"nonfinite RF drift accepted");
    r=retunes(1,1);rejected=false;const auto o=source_options(c);auto source=transfer::binary_transmitter(Bytes(64,1),o);
    try {modem::SampledSimulationChannel ch(c,impairment(c,h,1),h);ch.set_rf_retune_simulation(r);std::array<float,1> out{};ch.read(*source,out);}catch(const Error&){rejected=true;}
    require(rejected,"RF drift crossing capture bounds accepted");std::cout<<"rf_drift_boundaries,nonfinite_rejected=1,unsafe_dwell_rejected=1\n";
}
}
int main(int argc,char** argv)try {
    if(argc==3 && std::string_view(argv[1])=="--emit-zero"){zero_output(argv[2]);return 0;}
    const auto start=std::clock();invalid_fields();for(const auto side:std::array{1,-1})for(const auto slew:std::array{3e-9,-3e-9})drift_case(side,slew);
    std::cout<<"rf_drift_transport,PASS,process_cpu_seconds="<<static_cast<double>(std::clock()-start)/CLOCKS_PER_SEC<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
