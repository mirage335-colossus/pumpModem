#include "datapump/helper_frontend.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/types.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <span>
#include <vector>

using namespace datapump;
namespace {
using Translator=helper::AnalyticFrequencyTranslator;
constexpr double tau=2*std::numbers::pi;
void check(bool condition,const char* message) {if(!condition)throw Error(message);}
double phase_error(double a,double b) {return std::abs(std::remainder(a-b,tau));}

void dated_phase_uses_input_centers_and_large_coordinates() {
    constexpr std::uint32_t rate=48000;
    for(const auto origin:std::array<std::uint64_t,3>{7,(1ULL<<63)+13007,UINT64_MAX-10000})
        for(const auto conjugate:{false,true}) {
            Translator translator({rate,conjugate?10000.:0.,{4000,6000},origin,conjugate,true},Translator::workspace_required());
            Translator::PilotObservation observation{5200,5200.125,.3125,origin+200,.25,
                origin,origin+1500,origin+1500};
            const auto initial_phase=translator.phase_at(origin);
            const auto target=origin+2*Translator::group_delay;
            const auto old_target_phase=translator.phase_at(target);
            check(translator.track_pilot(observation),"valid dated pilot phase was declined");
            check(translator.next_sample()==origin && translator.phase_at(origin)==initial_phase,
                  "scheduled pilot phase changed an earlier output coordinate");
            std::array<float,2*Translator::group_delay> input{},output{};
            translator.push(input,output,origin);
            check(translator.next_sample()==target && phase_error(translator.phase_at(target),old_target_phase)<1e-12,
                  "pending pilot phase was applied before its first supported input center");
            float zero=0,result=0;
            translator.push(std::span<const float>(&zero,1),std::span<float>(&result,1),target);
            // Integer expected frequency permits this independent small-product
            // reference even when the absolute sample origin exceeds 2^63.
            const auto expected_cycles=std::fmod(5200.L*((origin%rate)+Translator::group_delay),rate)/rate;
            const auto observed_cycles=.3125L+5200.125L*(Translator::group_delay-200.25L)/rate;
            const auto sign=conjugate?-1.L:1.L;
            const auto expected=static_cast<double>(2*std::numbers::pi_v<long double>*
                std::remainder(expected_cycles-sign*observed_cycles,1.L));
            const auto slope=5200-static_cast<double>(sign)*5200.125;
            check(phase_error(translator.phase_at(target),expected)<1e-12,
                  "pilot phase transfer lost fractional dates, conjugation or large-coordinate precision");
            check(phase_error(translator.phase_at(target+1234),expected+tau*slope*1234/rate)<1e-10,
                  "pilot update did not retain its measured signed frequency slope");
            check(translator.next_sample()==target+1,"pilot phase update changed the processed frontier");
            auto pooled=observation;pooled.tracking_frequency_hz=5200.0625;
            Translator with_pooled_slope({rate,conjugate?10000.:0.,{4000,6000},origin,conjugate,true},Translator::workspace_required());
            check(with_pooled_slope.track_pilot(pooled),"dated pooled tracking slope was declined");
            with_pooled_slope.push(input,output,origin);
            with_pooled_slope.push(std::span<const float>(&zero,1),std::span<float>(&result,1),target);
            check(phase_error(with_pooled_slope.phase_at(target),expected)<1e-12,
                  "pooled tracking slope changed the locally transported phase anchor");
            check(phase_error(with_pooled_slope.phase_at(target+1234),expected+
                  tau*(5200-static_cast<double>(sign)*5200.0625)*1234/rate)<1e-10,
                  "dated pooled frequency did not control the continued NCO slope");
        }
}

void pending_update_preserves_samples_and_frequency_only_control() {
    constexpr std::uint32_t rate=48000;
    constexpr std::uint64_t origin=170003;
    const Translator::Config config{rate,0,{4000,6000},origin,false,true};
    Translator assisted(config,Translator::workspace_required()),baseline(config,Translator::workspace_required());
    Translator cancelled(config,Translator::workspace_required());
    Translator::PilotObservation observation{5000,5000,.25,origin+200,.5,
        origin+123,origin+1500,origin+1500};
    check(assisted.track_pilot(observation) && cancelled.track_pilot(observation),
          "supported future phase update was declined");
    check(!assisted.track_pilot(observation),"a second observation overwrote an unapplied phase update");
    check(cancelled.track_frequency(0),"frequency-only update could not cancel a pending phase update");
    std::vector<float> input(1200),a(input.size()),b(input.size()),c(input.size());
    for(std::size_t i=0;i<input.size();++i)
        input[i]=static_cast<float>(std::cos(tau*5000*(origin+i)/rate+.6));
    constexpr auto before=123+2*Translator::group_delay;
    assisted.push(std::span<const float>(input).first(before),std::span<float>(a).first(before),origin);
    baseline.push(std::span<const float>(input).first(before),std::span<float>(b).first(before),origin);
    check(std::equal(a.begin(),a.begin()+before,b.begin()),
          "phase assistance rewrote payload samples before its supported input center");
    assisted.push(std::span<const float>(input).subspan(before),std::span<float>(a).subspan(before),origin+before);
    baseline.push(std::span<const float>(input).subspan(before),std::span<float>(b).subspan(before),origin+before);
    cancelled.push(input,c,origin);
    check(c==b,"frequency-only control retained an unexpected pending phase correction");
    check(assisted.next_sample()==origin+input.size() && baseline.next_sample()==assisted.next_sample(),
          "phase assistance dropped or manufactured samples");
    check(!std::equal(a.begin()+before,a.end(),b.begin()+before),"supported pilot phase never reached PCM");
    const auto before_slope=assisted.phase_at(assisted.next_sample());
    check(assisted.track_frequency(-.125),"ordinary frequency-only tracking was declined");
    check(phase_error(assisted.phase_at(assisted.next_sample()),before_slope)<1e-12,
          "frequency-only tracking changed its existing continuous phase convention");
}

void invalid_observations_leave_state_unchanged() {
    constexpr std::uint64_t origin=1000;
    const Translator::Config config{48000,0,{4000,6000},origin,false,true};
    Translator translator(config,Translator::workspace_required());
    const Translator::PilotObservation valid{5000,5001,.25,origin+200,.5,
        origin,origin+1500,origin+1500};
    const auto reject=[&](const Translator::PilotObservation& observation) {
        const auto phase=translator.phase_at(origin+900);
        check(!translator.track_pilot(observation),"invalid pilot observation was accepted");
        check(translator.next_sample()==origin && translator.phase_at(origin+900)==phase &&
              translator.output_band().lower_hz==4000 && translator.output_band().upper_hz==6000,
              "declined pilot observation changed translator state");
    };
    for(const auto bad:{std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        auto observation=valid;observation.expected_hz=bad;reject(observation);
        observation=valid;observation.observed_hz=bad;reject(observation);
        observation=valid;observation.phase_cycles=bad;reject(observation);
        observation=valid;observation.phase_sample_fraction=bad;reject(observation);
        observation=valid;observation.tracking_frequency_hz=bad;reject(observation);
    }
    auto observation=valid;observation.phase_sample_fraction=-.1;reject(observation);
    observation=valid;observation.phase_sample_fraction=1;reject(observation);
    observation=valid;observation.end_sample=observation.first_sample;reject(observation);
    observation=valid;--observation.captured_end_sample;reject(observation);
    observation=valid;observation.phase_sample=observation.end_sample;reject(observation);
    observation=valid;observation.phase_sample=observation.first_sample-1;reject(observation);
    observation=valid;observation.end_sample=observation.first_sample+2*Translator::group_delay;reject(observation);
    observation=valid;observation.first_sample=UINT64_MAX-100;
    observation.end_sample=observation.captured_end_sample=UINT64_MAX;
    observation.phase_sample=UINT64_MAX-50;reject(observation);
    observation=valid;observation.expected_hz=1;reject(observation);
    auto disabled_config=config;disabled_config.continuous_reference=false;
    Translator disabled(disabled_config,Translator::workspace_required());
    check(!disabled.track_pilot(valid),"unprepared analytic translator accepted phase assistance");
    std::array<float,1600> input{},output{};translator.push(input,output,origin);
    check(!translator.track_pilot(valid),"expired phase support authorized a later input center");
}

void independent_hop_phases_are_removed_from_actual_robust_pcm() {
    constexpr std::uint32_t rate=8000;
    constexpr std::size_t dwell=rate*2/5,hops=8,count=dwell*hops;
    constexpr std::uint64_t origin=170003;
    constexpr double pilot=2400,data_gain=.04;
    modem::Config config;config.sample_rate=rate;config.carrier_hz=1200;config.bandwidth_hz=36;
    config.dsss_factor=10;config.outer_dsss_version=modem::OuterDsssVersion::interleaved_v2;
    config.spreading_factor=64;config.integration_seconds=14.2;config.training_seconds=2;
    config.memory_limit=64ULL*1024*1024;config.scramble=true;
    config.data_key=Crypto{std::array<std::uint8_t,32>{}};
    modem::PatternTransmitter transmitter(Bytes{1,0,1,1,0,0,1,0},config,1800000000,0,false);
    std::vector<std::complex<double>> data(count);
    check(transmitter.read_analytic(data)==data.size(),"actual Robust generator ended before phase experiment");
    std::vector<float> mixed(count),payload(count),ideal(count),a(count),p(count),u(count);
    const std::array offsets{127.25,-83.75,250.5,-180.25,450.125,-320.5,12.875,-.375};
    std::mt19937 random(77291);std::uniform_real_distribution<double> phase(-std::numbers::pi,std::numbers::pi);
    for(std::size_t hop=0;hop<hops;++hop) {
        const auto initial_phase=phase(random);
        for(std::size_t local=0;local<dwell;++local) {
            const auto i=hop*dwell+local;
            const auto common=std::polar(1.,initial_phase+tau*offsets[hop]*local/rate);
            const auto pure=std::polar(.9,tau*pilot*(origin+i)/rate);
            const auto data_sample=data_gain*data[i];
            mixed[i]=static_cast<float>(((pure+data_sample)*common).real());
            payload[i]=static_cast<float>((data_sample*common).real());
            ideal[i]=static_cast<float>(data_sample.real());
        }
    }
    const Translator::Config translator_config{rate,0,{600,3100},origin,false,true};
    Translator assisted(translator_config,Translator::workspace_required());
    Translator payload_assisted(translator_config,Translator::workspace_required());
    Translator unassisted(translator_config,Translator::workspace_required());
    double corrected_error=0,uncorrected_error=0,mixed_error=0,energy=0;
    std::size_t measured=0;
    for(std::size_t hop=0;hop<hops;++hop) {
        const auto begin=hop*dwell,first=begin+Translator::tap_count;
        const auto before=first-begin;
        assisted.push(std::span<const float>(mixed).subspan(begin,before),std::span<float>(a).subspan(begin,before),origin+begin);
        payload_assisted.push(std::span<const float>(payload).subspan(begin,before),std::span<float>(p).subspan(begin,before),origin+begin);
        // This component test supplies the exact frequency to an independent
        // 20ms Hann phase projection. It verifies transfer of an observation,
        // not causal tone discovery or tracker qualification.
        constexpr std::size_t aperture=rate/50;
        constexpr double center=(aperture-1)*.5;
        std::complex<double> projection{};
        double weight=0;
        for(std::size_t j=0;j<aperture;++j) {
            const auto w=.5-.5*std::cos(tau*(j+.5)/aperture);
            projection+=static_cast<double>(mixed[first+j])*w*
                std::polar(1.,-tau*(pilot+offsets[hop])*(static_cast<double>(j)-center)/rate);
            weight+=w;
        }
        projection*=2/weight;
        const Translator::PilotObservation observation{pilot,pilot+offsets[hop],std::arg(projection)/tau,
            origin+first+(aperture-1)/2,.5,origin+first,origin+begin+dwell,origin+begin+dwell};
        check(assisted.track_pilot(observation) && payload_assisted.track_pilot(observation),
              "independent settled hop phase was declined");
        assisted.push(std::span<const float>(mixed).subspan(first,dwell-before),std::span<float>(a).subspan(first,dwell-before),origin+first);
        payload_assisted.push(std::span<const float>(payload).subspan(first,dwell-before),std::span<float>(p).subspan(first,dwell-before),origin+first);
        unassisted.push(std::span<const float>(payload).subspan(begin,dwell),std::span<float>(u).subspan(begin,dwell),origin+begin);
        const auto measured_first=first+2*Translator::group_delay+128;
        for(auto n=measured_first;n<begin+dwell;++n) {
            const auto input_center=n-Translator::group_delay;
            const auto reference=static_cast<double>(ideal[input_center]);
            const auto pure=.9*std::cos(tau*pilot*(origin+input_center)/rate);
            corrected_error+=std::pow(p[n]-reference,2);
            uncorrected_error+=std::pow(u[n]-reference,2);
            mixed_error+=std::pow(a[n]-pure-reference,2);
            energy+=reference*reference;++measured;
        }
    }
    check(energy>0 && measured>hops*1000,"phase experiment did not measure actual Robust samples");
    const auto corrected=std::sqrt(corrected_error/energy),uncorrected=std::sqrt(uncorrected_error/energy);
    const auto mixture=std::sqrt(mixed_error/energy);
    std::cout<<"PILOT_PHASE_TRANSFER independent_hops="<<hops<<" measured="<<measured
             <<" payload_relative_rms="<<corrected<<" unassisted_relative_rms="<<uncorrected
             <<" pilot_and_payload_relative_rms="<<mixture
             <<" frequency_seed=LABORATORY_EXACT acquisition=UNTESTED\n";
    check(corrected<.01 && uncorrected>.5 && corrected<uncorrected*.02,
          "dated pilot phase did not remove independent hop phase/frequency errors from Robust PCM");
    check(mixture<.07,"strong pilot self-interference defeated common phase transfer");
    check(assisted.next_sample()==origin+count && payload_assisted.next_sample()==origin+count &&
          unassisted.next_sample()==origin+count,"phase-assisted hops changed sample counts or dates");
}
}

int main() {
    try {
        dated_phase_uses_input_centers_and_large_coordinates();
        pending_update_preserves_samples_and_frequency_only_control();
        invalid_observations_leave_state_unchanged();
        independent_hop_phases_are_removed_from_actual_robust_pcm();
        std::cout<<"helper pilot phase tests passed\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
