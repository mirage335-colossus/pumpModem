#include "datapump/channel.hpp"
#include "datapump/helper_tone_tracker.hpp"
#include "datapump/helper_segmented_precision.hpp"
#include "datapump/transfer.hpp"
#include "../src/live_helper_pilot_stage.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <iostream>
#include <numbers>
#include <random>
#include <set>

using namespace datapump;
namespace {
void check(bool value,const char* reason){if(!value)throw Error(reason);}
struct Measurements {
    std::size_t fits=0,escapes=0,wrong=0,hops=0,phase_fits=0,frequency_only_fits=0,phase_hops=0;
    double maximum_error=0,maximum_uncertainty=0,first_qualified_seconds=0;
};
Measurements channel_case(double cn0,unsigned seed,bool wide=false,bool noise_only=false,double phase_noise=.0) {
    modem::Config config;config.sample_rate=8000;config.carrier_hz=1500;config.bandwidth_hz=200;
    config.spreading_factor=128;config.pulse_shaping=false;
    config.oscillator_search=modem::OscillatorSearchConfig{{100,phase_noise},{wide?100.:2.,phase_noise},wide?3e7:7e6,3};
    transfer::Options options;options.modem=config;options.timestamp=1800000000;
    options.key=Crypto(std::array<std::uint8_t,32>{});
    Bytes bits(64);for(std::size_t i=0;i<bits.size();++i)bits[i]=(i*7+i/3)%2;
    auto source=transfer::binary_transmitter(bits,options);
    helper::Options helper;helper.power_w=40;helper.data_power_w=4;helper.callsign="N0CALL";helper.timecode=true;
    const auto plan=helper::plan(helper,helper::placement_band(config),120000000,0,modem::outer_dsss_transmit_gain(config));
    modem::ChannelConfig impairment;impairment.clock_error_ppm=100;impairment.frequency_offset_hz=wide?3000:7.3;
    impairment.phase_noise_degrees_per_sqrt_second=phase_noise;impairment.seed=seed;
    impairment.snr_db=cn0-10*std::log10(config.sample_rate/2.)-10*std::log10(helper.power_w/helper.data_power_w)-
        20*std::log10(modem::outer_dsss_transmit_gain(config))-10*std::log10(helper::pilot_power_fraction);
    std::optional<modem::SampledSimulationChannel::PreconversionCapture> capture;
    if(wide)capture=modem::SampledSimulationChannel::PreconversionCapture{-10000,14000,32000};
    modem::SampledSimulationChannel channel(config,impairment,helper,0,{},capture);
    channel.set_helper_time_key(options.key);
    const auto rate=channel.physical_sample_rate();const auto rf0=config.oscillator_search->rf_shift_hz;
    helper::ToneTrackerConfig tracking{rate,plan.frequency_hz,wide?12000.:60.,phase_noise*std::numbers::pi/180,.5,true};
    helper::ToneTracker tracker(tracking,helper::ToneTracker::workspace_required(tracking));
    modem::SampledSimulationChannel::PhysicalRfSegment segment;
    modem::SampledSimulationChannel::RfRetuneSimulation retunes;
    retunes.rf_shift_hz={rf0,1.1*rf0,1.2*rf0,1.3*rf0};retunes.phase_seed=49273+seed;
    channel.set_rf_retune_simulation(retunes);
    Measurements out;std::uint64_t last_fit=0;std::set<std::uint64_t> qualified_hops,phase_hops;
    channel.set_physical_rf_observer([&](const auto& next,std::uint32_t observed_rate) {
        check(observed_rate==rate,"local aperture changed ADC clock");segment=next;
        const auto& policy=*config.oscillator_search;
        const auto width=(next.rf_shift_hz*policy.rf.accuracy_ppm+plan.frequency_hz*policy.lf.accuracy_ppm)*policy.margin*1e-6;
        tracker.begin_pilot_segment(next.first_sample,plan.frequency_hz,width,false,1,next.settled_first_sample,true);
        check(!tracker.snapshot().qualified,"local aperture carried qualification across an unknown hop phase");
    });
    channel.set_physical_observer([&](std::span<const float> raw,std::uint32_t observed_rate,std::uint64_t first) {
        check(observed_rate==rate,"local aperture changed observed rate");tracker.push(raw,first);
        const auto fit=tracker.snapshot();if(!fit.qualified || fit.frequency_context_end_sample==last_fit)return;
        last_fit=fit.frequency_context_end_sample;
        check(fit.frequency_local_aperture && fit.frequency_covariance_valid && fit.frequency_rate_hz_per_second==0 &&
            fit.frequency_rate_covariance_hz2_per_second==0 && fit.frequency_rate_variance_hz2_per_second2==0,
            "local aperture fabricated a pooled oscillator/drift model");
        check(fit.first_sample==fit.frequency_context_first_sample && fit.sample+1==fit.frequency_context_end_sample &&
            fit.first_sample>=segment.settled_first_sample && fit.sample<segment.end_sample,
            "local aperture escaped its independently observed RF segment");
        check(!fit.frequency_at(fit.first_sample-1).valid && !fit.frequency_at(fit.sample+1).valid,
            "local aperture granted earlier or future frequency authority");
        const auto support=fit.phase_fit_support_uncertainty_cycles;
        const bool phase_eligible=fit.phase_fit_qualified && !fit.frequency_used_fallback && support &&
            std::isfinite(*support) && *support>=0 && *support<.125;
        if(phase_eligible) {
            check(fit.phase_fit_first_sample==fit.first_sample && fit.phase_fit_end_sample==fit.sample+1,
                "local phase eligibility borrowed another frequency aperture's dates");
            ++out.phase_fits;phase_hops.insert(segment.phase_segment);
        } else {
            ++out.frequency_only_fits;
            check(!live::detail::helper_pilot_phase_observation(fit,plan.frequency_hz,rate),
                "frequency-only local fit granted payload phase authority");
        }
        const auto truth=plan.frequency_hz*(1+impairment.clock_error_ppm*1e-6)+
            impairment.frequency_offset_hz*segment.rf_shift_hz/rf0;
        for(const auto date:std::array{fit.first_sample,fit.sample}) {
            const auto estimate=fit.frequency_at(date);check(estimate.valid,"local aperture lost its own observed endpoints");
            const auto error=std::abs(estimate.frequency_hz-truth);
            out.maximum_error=std::max(out.maximum_error,error);out.maximum_uncertainty=std::max(out.maximum_uncertainty,estimate.uncertainty_hz);
            out.escapes+=error>estimate.uncertainty_hz;out.wrong+=error>50;
        }
        if(!out.fits)out.first_qualified_seconds=static_cast<double>(fit.captured_end_sample)/rate;
        ++out.fits;qualified_hops.insert(segment.phase_segment);
    });
    std::array<float,257> discarded{};std::uint64_t logical=0;
    while(logical<6ULL*config.sample_rate) {
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(discarded.size(),6ULL*config.sample_rate-logical));
        if(noise_only){channel.read_noise(std::span(discarded).first(count));logical+=count;}
        else {const auto n=channel.read(*source,std::span(discarded).first(count));check(n,"local aperture source unexpectedly ended");logical+=n;}
    }
    out.hops=qualified_hops.size();out.phase_hops=phase_hops.size();
    std::cout<<"local_aperture_component,cn0="<<cn0<<",seed="<<seed<<",wide="<<wide<<",noise_only="<<noise_only
        <<",phase_noise_degrees="<<phase_noise<<",fits="<<out.fits<<",qualified_hops="<<out.hops
        <<",phase_eligible_fits="<<out.phase_fits<<",phase_eligible_hops="<<out.phase_hops
        <<",frequency_only_fits="<<out.frequency_only_fits
        <<",endpoint_escapes="<<out.escapes<<",wrong_tones="<<out.wrong<<",max_error_hz="<<out.maximum_error
        <<",max_U_hz="<<out.maximum_uncertainty<<",first_qualified_seconds="<<out.first_qualified_seconds
        <<",tracker_bytes="<<tracker.workspace_bytes()<<'\n';
    return out;
}
void unsupported_gap_then_straight_recovery() {
    constexpr std::uint32_t rate=48000;
    constexpr std::uint64_t first=80003,discovery=4096,aperture=rate/5;
    constexpr auto context_first=first+discovery,end=context_first+aperture,jump=context_first+rate/10;
    constexpr double truth=2413.37,tau=2*std::numbers::pi;
    helper::ToneTrackerConfig config{rate,2400,9000,0,.5,true};
    helper::ToneTracker tracker(config,helper::ToneTracker::workspace_required(config),first);
    tracker.begin_pilot_segment(first,2400,9000,false,1,first,true);
    const auto bytes=tracker.workspace_bytes();
    std::mt19937 random{89471};std::normal_distribution<double> normal;
    std::array<float,960> pcm{};
    // At this rate the local discovery aperture is4096 samples. Put the
    // second unknown phase on the fifth actual20ms row boundary afterward.
    // The truth frequency only generates PCM; acquisition receives no seed.
    for(auto at=first;at<end;) {
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(pcm.size(),end-at));
        for(std::size_t i=0;i<count;++i) {
            const auto sample=at+i;const auto t=static_cast<double>(sample)/rate;
            pcm[i]=static_cast<float>(.6*std::cos(.37+tau*truth*t+(sample>=jump?2.:0.))+.01*normal(random));
        }
        tracker.push(std::span(pcm).first(count),at);at+=count;
    }
    const auto fit=tracker.snapshot();
    const auto phase=live::detail::helper_pilot_phase_observation(fit,2400,rate);
    std::cout<<"local_frequency_only_gap,qualified="<<fit.qualified<<",fallback="<<fit.frequency_used_fallback
        <<",gap_index="<<fit.frequency_gap_index<<",first="<<fit.first_sample<<",end="<<fit.sample+1
        <<",captured_end="<<fit.captured_end_sample<<",phase_fit="<<fit.phase_fit_qualified
        <<",phase_plan="<<phase.has_value()<<",frequency_error_hz="<<std::abs(fit.frequency_hz-truth)
        <<",U_hz="<<fit.uncertainty_hz<<'\n';
    // This local4096-point acquisition requires469 gap-profile grid points;
    // the bounded profile has251. It declines before fitting. This is an
    // implementation limit, not an SNR limit or positive gap-fit coverage.
    check(!fit.qualified && !fit.frequency_knowledge_valid && !fit.frequency_covariance_valid &&
          !fit.phase_fit_qualified && !fit.phase_fit_support_uncertainty_cycles && !phase &&
          fit.captured_end_sample==end && tracker.workspace_bytes()==bytes &&
          !fit.frequency_at(context_first).valid && !fit.frequency_at(end-1).valid,
          "unsupported local gap manufactured frequency or phase authority");
    // A fresh straight context recovers causally, without reset or a frequency
    // seed. Neither the declined gap nor future input belongs to its support.
    const auto recovered_end=end+aperture;
    for(auto at=end;at<recovered_end;) {
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(pcm.size(),recovered_end-at));
        for(std::size_t i=0;i<count;++i)pcm[i]=static_cast<float>(
            .6*std::cos(.37+tau*truth*static_cast<double>(at+i)/rate+2.)+.01*normal(random));
        tracker.push(std::span(pcm).first(count),at);at+=count;
    }
    const auto recovered=tracker.snapshot();
    check(recovered.qualified && recovered.frequency_local_aperture && !recovered.frequency_used_fallback &&
          recovered.frequency_knowledge_valid && recovered.frequency_covariance_valid &&
          recovered.first_sample==end && recovered.sample+1==recovered_end &&
          recovered.captured_end_sample==recovered_end && tracker.workspace_bytes()==bytes,
          "straight local context did not recover on its own bounded support");
    for(const auto date:std::array{end,recovered_end-1}) {
        const auto frequency=recovered.frequency_at(date);
        check(frequency.valid && std::abs(frequency.frequency_hz-truth)<=frequency.uncertainty_hz,
            "recovered local frequency did not cover its observed endpoints");
    }
    check(!recovered.frequency_at(end-1).valid && !recovered.frequency_at(recovered_end).valid,
          "local recovery borrowed declined or future support");
    std::cout<<"local_unsupported_gap_recovery,first="<<recovered.first_sample<<",end="<<recovered.sample+1
        <<",error_hz="<<std::abs(recovered.frequency_hz-truth)<<",U_hz="<<recovered.uncertainty_hz
        <<",gap_positive_coverage=0\n";
}
void local_drift() {
    constexpr std::uint32_t rate=32000;constexpr std::size_t dwell=rate*2/5,settle=384;
    constexpr double pilot=2400,drift=.4,cn0=10000,amplitude=.6;
    helper::ToneTrackerConfig config{rate,pilot,60,0,.5,true};
    helper::ToneTracker tracker(config,helper::ToneTracker::workspace_required(config));
    std::mt19937 random(293);std::normal_distribution<double> normal;std::uniform_real_distribution<double> phase(-std::numbers::pi,std::numbers::pi);
    std::array<float,256> block{};std::size_t fits=0,escapes=0;std::uint64_t previous=0;
    for(std::uint64_t hop=0;hop<8;++hop) {
        const auto first=hop*dwell;tracker.begin_pilot_segment(first,pilot,60,false,1,first+settle,true);
        const auto jump=phase(random);
        for(std::size_t at=0;at<dwell;at+=block.size()) {
            for(std::size_t i=0;i<block.size();++i) {
                const auto sample=first+at+i;
                const auto t=static_cast<double>(sample)/rate;
                block[i]=static_cast<float>((at+i<settle?0:amplitude*std::cos(jump+2*std::numbers::pi*((pilot+7.3)*t+.5*drift*t*t)))+
                    amplitude*std::sqrt(rate/(4*cn0))*normal(random));
            }
            tracker.push(block,first+at);const auto fit=tracker.snapshot();
            if(!fit.qualified || fit.frequency_context_end_sample==previous)continue;
            previous=fit.frequency_context_end_sample;++fits;
            for(const auto date:std::array{fit.first_sample,fit.sample}) {
                const auto observed=fit.frequency_at(date);check(observed.valid,"drift aperture lost its observed endpoint");
                escapes+=std::abs(observed.frequency_hz-(pilot+7.3+drift*date/rate))>observed.uncertainty_hz;
            }
        }
    }
    std::cout<<"local_aperture_drift,pure_tone=1,drift_hz_per_second="<<drift<<",configured_operational_allowance=.5,fits="<<fits<<",endpoint_escapes="<<escapes<<'\n';
    check(fits>=6 && !escapes,"local aperture failed its supported drift endpoint enclosure");
}
}
int main()try {
    const auto start=std::clock();std::size_t escapes=0,wrong=0,noise_fits=0;
    for(const auto cn0:std::array{30.,40.})for(unsigned seed=1;seed<=8;++seed) {
        const auto observed=channel_case(cn0,seed);escapes+=observed.escapes;wrong+=observed.wrong;
        check(observed.hops>=6,"strong local reference failed to acquire enough independently phased hops");
    }
    for(unsigned seed=1;seed<=3;++seed)for(const auto cn0:std::array{20.,60.}) {
        const auto observed=channel_case(cn0,seed);escapes+=observed.escapes;wrong+=observed.wrong;
        if(cn0==60)check(observed.hops>=6,"strong cable local reference did not acquire enough independent hops");
    }
    for(unsigned seed=1;seed<=3;++seed) {
        const auto wide=channel_case(40,seed,true);escapes+=wide.escapes;wrong+=wide.wrong;
        check(wide.hops>=6,"wide local reference did not causally acquire its actual capture range");
        const auto noisy=channel_case(40,seed,false,false,.5);escapes+=noisy.escapes;wrong+=noisy.wrong;
        check(noisy.hops>=6,"supported phase-noise local reference did not acquire enough independent hops");
    }
    for(unsigned seed=1;seed<=12;++seed)noise_fits+=channel_case(40,seed,false,true).fits;
    unsupported_gap_then_straight_recovery();local_drift();
    std::cout<<"local_aperture_ensemble,endpoint_escapes="<<escapes<<",wrong_tones="<<wrong<<",cold_noise_qualified_fits="<<noise_fits
        <<",process_cpu_seconds="<<static_cast<double>(std::clock()-start)/CLOCKS_PER_SEC<<'\n';
    return escapes||wrong||noise_fits?2:0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
