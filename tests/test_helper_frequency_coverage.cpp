#include "datapump/helper_carrier.hpp"
#include "datapump/helper_receiver.hpp"
#include "datapump/modem.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <optional>
#include <random>
#include <string>
#include <vector>

using namespace datapump;
namespace {
constexpr double tau=2*std::numbers::pi;
void require(bool value,const char* message) {if(!value)throw Error(message);}
struct Summary {
    std::size_t references=0,full_misses=0,local_misses=0,anchor_rejections=0,phase_references=0,phase_misses=0;
    double max_error=0,max_process=0,min_gain=1e100,max_gain=0,max_phase_error=0,max_phase_ci=0;
};
Summary strong_reference(double physical_sigma,double modeled_sigma,double chirp,double seconds=4) {
    constexpr std::uint32_t rate=24000;
    constexpr double expected=2450.3125,offset=.58,delay=.03719;
    helper::Options options;options.power_w=15000;options.timecode=true;
    const Crypto key{std::array<std::uint8_t,32>{}};
    helper::Generator generator(options,{expected,.7,0,15000},1800000000,true,key);
    helper::FrequencyPrecisionOptions precision;
    precision.phase_noise_radians_per_sqrt_second=modeled_sigma;
    helper::Receiver receiver(expected,20,{key},{},precision);
    std::mt19937_64 random(1),phase_random(1^0xa0761d6478bd642fULL);
    std::normal_distribution<double> noise(0,.0001),phase_step(0,physical_sigma/std::sqrt(rate));
    std::array<float,137> samples{};double phase=.81;Summary result;
    const auto total=static_cast<std::uint64_t>(std::llround(seconds*rate));
    std::vector<double> actual_phase(total);
    std::optional<std::array<double,2>> anchor;
    for(std::uint64_t first=0;first<total;first+=samples.size()) {
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(samples.size(),total-first));
        for(std::size_t i=0;i<count;++i) {
            const auto time=static_cast<double>(first+i)/rate;
            phase+=phase_step(phase_random);
            const auto source_time=static_cast<long double>(time)-delay;
            actual_phase[first+i]=phase+tau*(expected*static_cast<double>(source_time)+
                offset*time+chirp*time*time/2);
            const auto signal=source_time<0?0.:std::real(generator.mix_analytic({},source_time,rate)*
                std::polar(1.,phase+tau*(offset*time+chirp*time*time/2)));
            samples[i]=static_cast<float>(signal+noise(random));
        }
        receiver.push(std::span<const float>(samples).first(count),rate,first);
        if(const auto reference=receiver.take_update()) {
            const auto& r=*reference;
            require(r.sample<first+count,"frequency publication used future raw PCM");
            const auto end=r.frequency_sample?r.frequency_sample:r.sample;
            require(end<=r.sample && r.frequency_first_sample<=end,"frequency support is not causal");
            require(r.frequency_fit!=helper::FrequencyFit::unknown,"frequency publication omitted its fit provenance");
            require(end-r.frequency_first_sample+1==rate/25,"strong frequency aperture changed");
            require(r.frequency_phase_diffusion_gain>=0 && std::isfinite(r.frequency_phase_diffusion_gain),
                    "frequency process gain is invalid");
            require(r.frequency_process_uncertainty_hz>=0 &&
                    r.frequency_process_uncertainty_hz<=r.frequency_uncertainty_hz,
                    "frequency model allowance is missing from its full interval");
            ++result.references;
            if(r.phase_valid&&r.timing_valid&&r.phase_observation_known&&r.phase_observation_sample==r.sample) {
                ++result.phase_references;
                const auto phase_error=std::abs(std::remainder(r.phase_radians-actual_phase[r.phase_observation_sample],tau));
                result.phase_misses+=phase_error>r.phase_uncertainty_radians;
                result.max_phase_error=std::max(result.max_phase_error,phase_error);
                result.max_phase_ci=std::max(result.max_phase_ci,r.phase_uncertainty_radians);
            }
            // The oscillator model describes phase diffusion around a constant
            // mean carrier, never an instantaneous Brownian derivative.
            const auto error=std::abs(r.frequency_hz-expected-offset);
            result.max_error=std::max(result.max_error,error);
            result.full_misses+=error>r.frequency_uncertainty_hz;
            result.local_misses+=error>r.frequency_uncertainty_hz-r.frequency_process_uncertainty_hz;
            result.max_process=std::max(result.max_process,r.frequency_process_uncertainty_hz);
            result.min_gain=std::min(result.min_gain,r.frequency_phase_diffusion_gain);
            result.max_gain=std::max(result.max_gain,r.frequency_phase_diffusion_gain);
            if(!anchor)anchor=std::array<double,2>{r.frequency_hz-r.frequency_uncertainty_hz,
                                                r.frequency_hz+r.frequency_uncertainty_hz};
            else result.anchor_rejections+=r.frequency_hz+r.frequency_uncertainty_hz<(*anchor)[0] ||
                                           r.frequency_hz-r.frequency_uncertainty_hz>(*anchor)[1];
        }
    }
    return result;
}
void strong_kernel_coverage() {
    const auto sigma=.50249378105604448*std::numbers::pi/180;
    const auto awgn=strong_reference(0,0,0);
    const auto old_local=strong_reference(sigma,0,0);
    const auto modeled=strong_reference(sigma,sigma,0);
    const auto moving=strong_reference(sigma,sigma,4);
    const auto slow_moving=strong_reference(sigma,sigma,.2,12);
    const std::array<std::pair<const char*,Summary>,5> cases{{
        {"awgn",awgn},{"phase-rw-local-only",old_local},{"phase-rw-modeled",modeled},
        {"large-chirp",moving},{"slow-chirp",slow_moving}}};
    for(const auto& [name,s]:cases)
        std::cout<<std::setprecision(17)<<name<<" refs="<<s.references<<" full_misses="<<s.full_misses
            <<" local_misses="<<s.local_misses<<" anchor_rejections="<<s.anchor_rejections
            <<" max_mean_error_hz="<<s.max_error<<" max_process_hz="<<s.max_process
            <<" min_gain="<<s.min_gain<<" max_gain="<<s.max_gain
            <<" timed_phase_refs="<<s.phase_references<<" phase_misses="<<s.phase_misses
            <<" max_phase_error="<<s.max_phase_error<<" max_phase_ci="<<s.max_phase_ci<<'\n';
    require(awgn.references>20 && awgn.full_misses==0,"strong AWGN reference coverage failed");
    require(old_local.references>20 && old_local.full_misses>0,"matched old local-CI defect was not reproduced");
    require(modeled.references>20 && modeled.full_misses==0 && modeled.anchor_rejections==0,
            "finite-aperture oscillator model excluded its constant mean carrier");
    require(modeled.max_process>0 && modeled.min_gain>0,"phase model added no actual finite-kernel allowance");
    require(awgn.phase_references>5 && awgn.phase_misses==0 &&
            modeled.phase_references>5 && modeled.phase_misses==0,
            "actual measured endpoint phase escaped its model interval");
    require(moving.references==0 || moving.anchor_rejections>0,
            "a large moving-carrier chirp remained a fixed oscillator reference");
    require(slow_moving.references>5 && slow_moving.anchor_rejections>0,
            "a gradual moving-carrier chirp remained a fixed oscillator reference");
}
void off_basis_punctuation() {
    constexpr std::uint32_t rate=57600;
    constexpr double expected=5275;
    std::array<std::uint8_t,32> seed{};
    for(std::size_t i=0;i<seed.size();++i)seed[i]=static_cast<std::uint8_t>(i+1);
    const Crypto key{seed};
    helper::Options options;options.power_w=15000;options.data_power_w=4;options.ratio_limit_db=90;
    auto placement=helper::plan(options,1500,1800,rate,0,.5);
    placement.frequency_hz=expected;
    const auto sigma=std::hypot(.5,.05)*std::numbers::pi/180;
    const auto awgn=std::sqrt(modem::nominal_signal_power*std::pow(10.,-101.44727494896694/10))*
        placement.data_gain*2;
    for(const double offset:{-30.,0.,30.})for(const double ppm:{0.,100.}) {
        const auto clock=1+ppm*1e-6;
        helper::Generator generator(options,placement,1800000005,true,key);
        helper::FrequencyPrecisionOptions precision;
        precision.phase_noise_radians_per_sqrt_second=sigma;
        helper::Receiver receiver(expected,100,{key},{},precision);
        std::mt19937_64 phase_random(1^0xa0761d6478bd642fULL),noise_random(1);
        std::normal_distribution<double> phase_normal(0,1),noise_normal(0,1);
        double phase=.3141592653589793,max_dwell_error=0;
        std::size_t references=0,timed=0;
        std::array<float,115> samples{};
        for(std::uint64_t first=0;first<8ULL*rate;first+=samples.size()) {
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(samples.size(),8ULL*rate-first));
            for(std::size_t i=0;i<count;++i) {
                const auto seconds=static_cast<long double>(first+i)/rate;
                const auto analytic=generator.mix_analytic({},seconds*clock,rate);
                samples[i]=static_cast<float>((analytic*std::polar(1.,phase+tau*offset*
                    static_cast<double>(seconds))).real()+awgn*noise_normal(noise_random));
                phase+=sigma/std::sqrt(static_cast<double>(rate))*phase_normal(phase_random);
            }
            receiver.push(std::span<const float>(samples).first(count),rate,first);
            if(const auto reference=receiver.take_update()) {
                ++references;
                require(reference->sample<first+count,"punctuation publication used future PCM");
                require(reference->frequency_valid && std::abs(reference->frequency_hz-expected*clock-offset)<=
                        reference->frequency_uncertainty_hz,"off-basis reference excluded its mean carrier");
                if(reference->timing_valid) {
                    ++timed;
                    const auto error=std::abs(std::remainder(1+reference->dwell_start_seconds*clock,.4));
                    max_dwell_error=std::max(max_dwell_error,error);
                    require(error<.00015,"off-basis punctuation lost the physical dwell grid");
                }
            }
        }
        std::cout<<std::setprecision(17)<<"off-basis punctuation offset="<<offset<<" ppm="<<ppm
                 <<" references="<<references<<" timed="<<timed<<" max_dwell_error="<<max_dwell_error<<'\n';
        require(timed>50,"off-basis finite-bin projection prevented punctuation acquisition");
    }
}
}
int main() {
    try {strong_kernel_coverage();off_basis_punctuation();std::cout<<"helper frequency coverage passed\n";}
    catch(const std::exception& error) {std::cerr<<"helper frequency coverage: "<<error.what()<<'\n';return 1;}
}
