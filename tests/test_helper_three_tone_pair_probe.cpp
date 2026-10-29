// Diagnostic only: two causal trackers, independent hop phases and actual waveform.
// Reports failures rather than treating the conditional Q=0 model as qualification.
// Explicit zero diffusion matches tracker declarations; retain mismatch evidence separately.
#include "datapump/channel.hpp"
#include "datapump/helper_tone_tracker.hpp"
#include "datapump/transfer.hpp"
#include "../src/helper_oscillator_model.hpp"
#include <array>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <iostream>
using namespace datapump;
namespace {
helper::IndependentFrequencyAperture aperture(const helper::ToneFrequencyObservation& o,helper::LinkMapping m) {
    helper::IndependentFrequencyAperture a{m,o.first_sample,o.end_sample,o.mean_sample,o.mean_sample_fraction,o.frequency_hz,
        o.independent_variance_hz2+o.residual_variance_hz2,double(o.end_sample-o.first_sample)/m.sample_rate};
    a.process_moments=helper::FrequencyApertureProcessMoments{o.valid,o.first_sample,o.end_sample,o.state_endpoint_sample,o.mass,o.mu_seconds,
        o.integral_k_seconds2,o.integral_b_minus_s_k_seconds3,o.integral_k_squared_seconds3,o.k_at_end_seconds,o.k_prime_at_end};
    return a;
}
void run(double cn0,unsigned seed) {
    modem::Config c;c.sample_rate=8000;c.carrier_hz=1500;c.bandwidth_hz=200;c.spreading_factor=128;c.pulse_shaping=false;
    c.oscillator_search=modem::OscillatorSearchConfig{{100,0},{100,0},30000000,3};
    transfer::Options opt;opt.modem=c;opt.timestamp=1800000000;opt.key=Crypto(std::array<std::uint8_t,32>{});
    Bytes bits(512);for(std::size_t i=0;i<bits.size();++i)bits[i]=(i*7+i/3)%2;
    auto source=transfer::binary_transmitter(bits,opt);
    helper::Options h;h.power_w=40;h.data_power_w=4;h.callsign="N0CALL";h.timecode=true;
    const auto plan=helper::plan(h,helper::placement_band(c),120000000,0,modem::outer_dsss_transmit_gain(c));
    modem::ChannelConfig impair;impair.clock_error_ppm=100;impair.frequency_offset_hz=7.3;impair.seed=seed;impair.phase_noise_degrees_per_sqrt_second=0;
    impair.snr_db=cn0-10*std::log10(c.sample_rate/2.)-10*std::log10(h.power_w/h.data_power_w)-20*std::log10(modem::outer_dsss_transmit_gain(c))-10*std::log10(helper::pilot_power_fraction);
    modem::SampledSimulationChannel channel(c,impair,h,0,{},{});channel.set_helper_time_key(opt.key);
    const auto rate=channel.physical_sample_rate();
    helper::ToneTrackerConfig hi{rate,plan.frequency_hz,60,0,.5,true},lo{rate,550,60,0,.5,true};
    helper::ToneTracker high(hi,helper::ToneTracker::workspace_required(hi)),low(lo,helper::ToneTracker::workspace_required(lo));
    helper::OscillatorFrequencyModelConfig mc;mc.rf_prior_sigma_ppm=100;mc.audio_prior_sigma_ppm=100;
    mc.rf_acceleration_psd_ppm2_per_second3=0;mc.audio_acceleration_psd_ppm2_per_second3=0;mc.maximum_prediction_age_seconds=2;mc.learn_repeat_visit_scatter=false;
    helper::OscillatorFrequencyModel pair,single;if(!pair.reset(mc)||!single.reset(mc))throw Error("model setup");
    modem::SampledSimulationChannel::PhysicalRfSegment segment{};bool have=false;
    unsigned hops=0,high_fit=0,low_fit=0,paired=0,pair_rejected=0,escapes=0,single_escapes=0;double err=0,u=0,s_err=0,s_u=0,first=-1;
    auto harvest=[&](){if(!have)return; ++hops;const auto a=high.snapshot().frequency_observation,b=low.snapshot().frequency_observation;
        high_fit+=a.valid;low_fit+=b.valid;if(!a.valid)return;
        helper::LinkMapping m;m.capture_generation=1;m.timebase_identity=1;m.reference_identity=1;m.reference_identity_known=true;
        m.sample_rate=rate;m.settled_first_sample=segment.settled_first_sample;m.settled_end_sample=segment.end_sample;
        m.actual_rf_shift_hz=segment.rf_shift_hz;m.helper_audio_hz=plan.frequency_hz;m.data_audio_hz=c.carrier_hz;
        m.provenance=helper::LinkMappingProvenance::modelled_physical_capture;m.phase_segment=segment.phase_segment;
        const auto ah=aperture(a,m);const auto end=a.end_sample;
        if(single.observe(ah,end)==helper::OscillatorModelReason::accepted){auto p=single.predict(m,end-1);if(p.valid){double truth=impair.frequency_offset_hz*segment.rf_shift_hz/30000000.+c.carrier_hz*impair.clock_error_ppm*1e-6;s_err=std::abs(p.data_residual_hz-truth);s_u=p.data_three_sigma_hz();single_escapes+=s_err>s_u;}}
        if(!b.valid)return;m.helper_audio_hz=550;auto al=aperture(b,m);
        const auto status=pair.observe_pair(ah,al,end);if(status!=helper::OscillatorModelReason::accepted){++pair_rejected;return;}
        ++paired;if(first<0)first=double(end)/rate;m.helper_audio_hz=plan.frequency_hz;auto p=pair.predict(m,end-1);
        if(p.valid){double truth=impair.frequency_offset_hz*segment.rf_shift_hz/30000000.+c.carrier_hz*impair.clock_error_ppm*1e-6;err=std::abs(p.data_residual_hz-truth);u=p.data_three_sigma_hz();escapes+=err>u;}
    };
    modem::SampledSimulationChannel::RfRetuneSimulation rt;rt.rf_shift_hz={30000000,33000000,36000000,39000000};rt.phase_seed=49273+seed;channel.set_rf_retune_simulation(rt);
    channel.set_physical_rf_observer([&](const auto& next,std::uint32_t){harvest();segment=next;have=true;
        high.begin_pilot_segment(next.first_sample,plan.frequency_hz,60,false,1,next.settled_first_sample,true,next.end_sample);
        low.begin_pilot_segment(next.first_sample,550,60,false,1,next.settled_first_sample,true,next.end_sample);});
    channel.set_physical_observer([&](std::span<const float> raw,std::uint32_t,std::uint64_t first_sample){high.push(raw,first_sample);low.push(raw,first_sample);});
    std::array<float,257> buf{};std::uint64_t n=0;while(n<36ULL*c.sample_rate){const auto k=channel.read(*source,std::span(buf).first(std::min<std::uint64_t>(buf.size(),36ULL*c.sample_rate-n)));if(!k)throw Error("source ended");n+=k;}harvest();
    std::cout<<"pair_probe cn0="<<cn0<<" seed="<<seed<<" hops="<<hops<<" high="<<high_fit<<" low="<<low_fit<<" paired="<<paired<<" pair_rejected="<<pair_rejected<<" first_s="<<first<<" pair_error="<<err<<" pair_U="<<u<<" pair_escapes="<<escapes<<" high_error="<<s_err<<" high_U="<<s_u<<" high_escapes="<<single_escapes<<" tracker_bytes="<<high.workspace_bytes()+low.workspace_bytes()<<'\n';
}
}
int main(int argc,char** argv)try {const auto start=std::clock();if(argc==3)run(std::strtod(argv[1],nullptr),std::strtoul(argv[2],nullptr,10));else for(double c:{30.,40.,60.})for(unsigned seed=1;seed<=3;++seed)run(c,seed);std::cout<<"cpu_s="<<double(std::clock()-start)/CLOCKS_PER_SEC<<'\n';return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
