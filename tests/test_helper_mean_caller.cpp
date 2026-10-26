#include "../src/live_helper.hpp"
#include "../src/live_helper_prior.hpp"
#include <iostream>
using namespace datapump;
void check(bool value,const char* text){if(!value)throw Error(text);}
helper::Reference endpoint(std::uint64_t at) {
    constexpr double tau=2*std::numbers::pi,frequency=5305.5;
    helper::Reference r;r.sample=at;r.sample_rate=14400;r.frequency_sample=at-1;
    r.frequency_first_sample=at-576;r.frequency_fit=helper::FrequencyFit::local_polynomial;
    r.frequency_hz=frequency;r.frequency_uncertainty_hz=.3;r.frequency_valid=true;
    r.phase_radians=std::remainder(tau*frequency*at/r.sample_rate,tau);
    r.phase_uncertainty_radians=.001;r.phase_valid=true;
    r.phase_observation_known=true;r.phase_observation_sample=at;
    r.timing_valid=true;r.dwell_start_seconds=static_cast<double>(at)/r.sample_rate-.24;
    r.dwell_uncertainty_seconds=.000001;return r;
}
int main(){try {
    helper::PhaseChord chord;check(chord.configure(14400,.001,5305.5),"chord configuration declined");
    helper::Reference current;
    for(const auto at:{10000ULL,16000ULL,22000ULL,28000ULL}){current=endpoint(at);chord.observe(current,at);}
    const auto prediction=chord.prediction();check(prediction.has_value(),"qualified chord aperture absent");
    const auto choose=[&](const helper::Reference& r,bool accepted=true,bool raw=true,std::uint64_t frontier=28001){
        return live::detail::helper_frequency_interval(r,&*prediction,accepted,raw,frontier);
    };
    const auto selected=choose(current);
    check(selected.phase_mean&&selected.uncertainty_hz<current.frequency_uncertainty_hz,
          "current independently qualified aperture mean did not tighten frequency assistance");
    check(selected.first_sample==prediction->first_sample&&selected.last_sample==current.sample,
          "mean aperture lost its actual endpoint dates");
    modem::Config physical;physical.oscillator_search=modem::OscillatorSearchConfig{{100,0},{1,0},30000000,3};
    const auto utc=live::detail::helper_utc_frequency_allowance(physical,true,5275);
    check(utc.valid&&utc.radius_hz>1.58,"joint caller lost independent UTC steering uncertainty");
    const auto combined=live::detail::helper_prior_detail::sum(selected.uncertainty_hz,utc.radius_hz).upper;
    check(combined>=utc.radius_hz+selected.uncertainty_hz&&combined>selected.uncertainty_hz,
          "optional endpoint mean replaced the mandatory UTC addend");
    for(const auto absent:{choose(current,false),choose(current,true,false),choose(current,true,true,27999),choose(current,true,true,28000),
                           choose(current,true,true,42401)}) {
        check(!absent.phase_mean&&absent.frequency_hz==current.frequency_hz&&
            absent.uncertainty_hz==current.frequency_uncertainty_hz,"optional decline changed direct frequency authority");
    }
    auto carried=current;++carried.sample;
    check(!choose(carried,true,true,carried.sample+1).phase_mean,"metadata date refreshed an old phase endpoint");
    auto moved=current;moved.frequency_hz+=2;moved.frequency_uncertainty_hz=.01;
    const auto fresh_moved=choose(moved);
    check(!fresh_moved.phase_mean&&fresh_moved.frequency_hz==moved.frequency_hz&&
          fresh_moved.uncertainty_hz==moved.frequency_uncertainty_hz,
          "an incompatible historical mean vetoed a fresh moving direct frequency");
    auto coarse=current;coarse.frequency_fit=helper::FrequencyFit::unknown;
    check(!choose(coarse).phase_mean,"coarse seed borrowed endpoint mean authority");
    std::cout<<"mean_caller first="<<selected.first_sample<<" last="<<selected.last_sample
        <<" raw_radius_hz="<<current.frequency_uncertainty_hz<<" effective_radius_hz="<<selected.uncertainty_hz
        <<" unchanged_direct_fallbacks=8 no_future_precision_lease\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
