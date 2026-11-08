#include "fixture_crypto.hpp"
#include "datapump/helper_carrier.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace h=datapump::helper;
namespace {
constexpr double tau=2*std::numbers::pi;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F action){try{action();}catch(const datapump::Error&){return;}throw std::runtime_error("invalid Helper configuration accepted");}
std::complex<double> oscillator(double frequency,long double seconds){return std::polar(1.,tau*static_cast<double>(std::fmod(frequency*seconds,1.L)));}
h::Options options(){h::Options o;o.power_w=40;o.data_power_w=4;o.timecode=false;return o;}
void power_and_placement(){
    h::Options off;const auto disabled=h::plan(off,1725,200,48000);h::Generator bypass(off,disabled);
    for(const float value:{0.f,-0.f,.125f,-.83f,std::numeric_limits<float>::infinity()})
        check(std::bit_cast<std::uint32_t>(bypass.mix(value,0,0))==std::bit_cast<std::uint32_t>(value),"disabled Helper changed payload bits");
    auto o=options();const auto plan=h::plan(o,1725,200,48000);
    check(plan.frequency_hz==2400&&plan.low_frequency_hz==550&&plan.auxiliary_frequency_hz==950,"canonical Helper tone placement changed");
    check(plan.effective_power_w==36,"total power did not reserve DATA allocation");
    check(h::minimum_output_rate(o,1725,200,6000)==48000,"Helper lost independent audio clock floor");
    check(plan.data_gain+plan.helper_gain*(h::pilot_amplitude_fraction+h::low_pilot_amplitude_fraction+h::auxiliary_amplitude_fraction)<=.950000000001,"composite lost peak headroom");
    const auto shifted=h::plan(o,4000,3000,48000);
    check(shifted.low_frequency_hz==550&&shifted.auxiliary_frequency_hz==950&&shifted.frequency_hz>=5950,"DATA geometry moved fixed Helper tones");
    const auto floor=h::plan(o,1725,200,48000,8250);check(floor.frequency_hz>=8250,"explicit high-pilot floor ignored");
    const auto backed=h::plan(o,1725,200,48000,0,.5);check(backed.data_gain==plan.data_gain&&backed.helper_gain==.5*plan.helper_gain,"DATA backoff changed power ratio");
    const auto mean=h::mean_power(o);
    check(std::abs(mean.low_pilot/mean.high_pilot-h::low_pilot_relative_power)<1e-14,"low/high power ratio changed");
    check(mean.total==mean.high_pilot+mean.low_pilot+mean.auxiliary,"component power accounting differs");
    o.ratio_limit_db=3;check(std::abs(h::effective_helper_power(o)-4*std::pow(10.,.3))<1e-12,"Helper ratio ceiling ignored");
    o=options();o.power_w=o.data_power_w;check(h::plan(o,1725,200,48000).helper_gain==0,"zero Helper remainder emits Helper");
    const auto no_level=h::reference_level(o,10);check(std::isinf(no_level.straight_cn0_db_hz)&&no_level.straight_cn0_db_hz<0,"absent Helper has finite reference power");
    o=options();rejects([&]{h::plan(o,1000,200,48000);});rejects([&]{h::plan(o,1725,200,4000);});
    rejects([&]{h::plan(o,1725,200,48000,-1);});rejects([&]{h::plan(o,1725,200,48000,0,0);});
    o.power_w=-1;rejects([&]{h::validate(o);});o=options();o.callsign=std::string(17,'A');rejects([&]{h::validate(o);});
    o.callsign=std::string(1,char(128));rejects([&]{h::validate(o);});
}
void exact_waveform_and_metadata(){
    auto o=options();auto plan=h::plan(o,1725,200,48000);
    h::Generator full(o,plan);auto p=plan;p.auxiliary_frequency_hz=0;h::Generator pilots(o,p);
    const auto auxiliary=[&](const h::Generator& generator,long double t){
        return (generator.mix_analytic({},t,48000)-pilots.mix_analytic({},t,48000))/
            (plan.helper_gain*h::auxiliary_amplitude_fraction*oscillator(plan.auxiliary_frequency_hz,t));};
    check(std::abs(auxiliary(full,.151L)-std::complex<double>{1,0})<1e-12,"auxiliary clean flank changed");
    check(std::abs(auxiliary(full,.160L))<1e-12,"auxiliary phase reversal moved");
    check(std::abs(auxiliary(full,.180L)+std::complex<double>{1,0})<1e-12,"auxiliary inverted flank changed");
    for(const long double t:{.05L,.2L,.399L,.4L,.401L}){
        const auto expected=plan.helper_gain*(h::pilot_amplitude_fraction*oscillator(plan.frequency_hz,t)+h::low_pilot_amplitude_fraction*oscillator(plan.low_frequency_hz,t));
        check(std::abs(pilots.mix_analytic({},t,48000)-expected)<1e-12,"continuous pilots reset or disappeared at a dwell boundary");
    }
    o.timecode=true;o.callsign="N0CALL";
    std::array<std::uint8_t,32> aes{};aes[0]=23;const std::array<std::uint8_t,32> serpent{0xa5};
    const auto key=fixture_crypto::key(serpent,aes);
    for(const bool encrypted:{false,true}){
        h::Generator carrying(o,plan,0,true,encrypted?std::optional{key}:std::nullopt);
        const auto expected=h::metadata_packet(0,o.callsign,encrypted?&key:nullptr);
        for(std::size_t index=0;index<expected.count;++index){
            const auto t=.232L+.020L*index;
            check(std::abs(auxiliary(carrying,t)+h::apsk_point(expected.symbols[index]))<2e-11,"canonical Generator metadata differs from current packet codec");
        }
    }
    p=plan;p.pilot_dwell_gating=true;h::Generator gated(o,p);
    check(std::abs(gated.mix_analytic({},.4L,48000))<1e-12,"FHSS Helper dwell boundary lacks its gate");
    check(full.mix(0,.8L,48000,.8L,0)==0,"Helper emitted after its complete endpoint");
}
void punctuation_and_mapper(){
    for(const long double end:{0.L,.1L,.151999L,.192L,.3L,.4L})check(h::protected_endpoint(0,0,end)==end,"unrelated endpoint extended");
    for(const long double end:{.152L,.16L,.17L,.18L,.191999L})check(std::abs(h::protected_endpoint(0,0,end)-.192L)<1e-15,"stop truncated in-progress punctuation");
    check(h::protected_endpoint(0,.128001L,.16L)==.16L,"punctuation began without required clean flank");
    rejects([]{h::protected_endpoint(0,1,0);});
    for(const std::uint32_t word:{0U,1U,32U,127U,128U,8192U,16383U}){
        const auto point=h::apsk_point(word);const auto ring=std::lround((std::abs(point)-.2)*127/.8);
        const auto sector=std::lround(std::arg(point)*128/tau);
        check(((static_cast<std::uint32_t>(ring)<<7)|(static_cast<std::uint32_t>(sector)&127U))==word,"APSK boundary mapping changed");
    }
    rejects([]{h::apsk_point(1U<<14);});
}
}
int main(){try{power_and_placement();exact_waveform_and_metadata();punctuation_and_mapper();std::cout<<"canonical Helper generator invariants passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
