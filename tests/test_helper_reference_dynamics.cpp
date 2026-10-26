#include "../src/helper_reference_dynamics.hpp"
#include "datapump/helper_carrier.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <span>
#include <stdexcept>

namespace h=datapump::helper;
namespace {
constexpr double tau=2*std::numbers::pi;
void check(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
h::Reference anchor(std::uint64_t sample,std::uint32_t rate,double phase,double frequency=17.25,
        double phase_uncertainty=.0001,double frequency_uncertainty=.001) {
    h::Reference reference;
    reference.sample=sample;reference.sample_rate=rate;
    reference.phase_radians=std::remainder(phase,tau);reference.frequency_hz=frequency;
    reference.phase_uncertainty_radians=phase_uncertainty;
    reference.frequency_uncertainty_hz=frequency_uncertainty;
    reference.frequency_valid=reference.phase_valid=reference.timing_valid=true;
    reference.phase_observation_known=true;reference.phase_observation_sample=sample;
    reference.dwell_start_seconds=.16;reference.dwell_uncertainty_seconds=.0001;
    return reference;
}
bool is(const h::ReferenceDynamicsResult& result,h::ReferenceDynamicsVerdict verdict,
        h::ReferenceDynamicsReason reason) {return result.verdict==verdict&&result.reason==reason;}

void phase_forecast() {
    const auto reference=anchor(48000,48000,.4);
    const auto actual=h::helper_phase_forecast_uncertainty(reference,4,.01);
    const auto expected=static_cast<long double>(reference.phase_uncertainty_radians)+
        2*std::numbers::pi_v<long double>*reference.frequency_uncertainty_hz*4+8*.01L*2;
    check(actual>=expected && actual-expected<1e-13L,"forecast omitted frequency or Wiener phase allowance");
    const auto zero=h::helper_phase_forecast_uncertainty(reference,0,0);
    check(zero>=reference.phase_uncertainty_radians,"zero-horizon forecast rounded inward");
    auto missing=reference;missing.timing_valid=false;
    check(std::isinf(h::helper_phase_forecast_uncertainty(missing,1,.01)),"forecast accepted unknown punctuation sign");
    missing=reference;missing.phase_valid=false;
    check(std::isinf(h::helper_phase_forecast_uncertainty(missing,1,.01)),"frequency-only forecast claimed phase authority");
    missing=reference;missing.frequency_hz=std::numeric_limits<double>::quiet_NaN();
    check(std::isinf(h::helper_phase_forecast_uncertainty(missing,1,.01)),"forecast accepted a nonfinite reference");
    missing=reference;missing.phase_observation_known=false;
    check(std::isinf(h::helper_phase_forecast_uncertainty(missing,1,.01)),"forecast accepted unknown phase origin");
    missing=reference;missing.phase_observation_sample=reference.sample-1;
    check(std::isinf(h::helper_phase_forecast_uncertainty(missing,1,.01)),"forecast accepted a carried phase as fresh");
    check(std::isinf(h::helper_phase_forecast_uncertainty(reference,-1,.01)) &&
          std::isinf(h::helper_phase_forecast_uncertainty(reference,1,-.01)) &&
          std::isinf(h::helper_phase_forecast_uncertainty(reference,std::numeric_limits<double>::infinity(),0)),
          "invalid forecast inputs did not decline assistance");
}

void chronology_and_invalid() {
    h::ReferenceDynamics gate;
    auto reference=anchor(1000,1000,0);
    check(is(gate.observe(reference,1000,1000,.001),h::ReferenceDynamicsVerdict::unqualified,
        h::ReferenceDynamicsReason::sample_clock_unset),"continuity implicitly selected a sample clock");
    gate.reset(1000);
    check(is(gate.observe(reference,1000,1000,-1),h::ReferenceDynamicsVerdict::unqualified,
        h::ReferenceDynamicsReason::invalid_model),"negative diffusion was accepted");
    check(gate.anchor_count()==0,"invalid diffusion mutated phase history");
    auto invalid=reference;invalid.frequency_uncertainty_hz=-1;
    check(is(gate.observe(invalid,1000,1000,.001),h::ReferenceDynamicsVerdict::unqualified,
        h::ReferenceDynamicsReason::invalid_measurement),"invalid uncertainty was accepted");
    invalid=reference;invalid.phase_radians=std::numeric_limits<double>::quiet_NaN();
    check(gate.observe(invalid,1000,1000,.001).reason==h::ReferenceDynamicsReason::invalid_measurement,
        "nonfinite phase was accepted");
    invalid=reference;invalid.frequency_sample=1001;
    check(gate.observe(invalid,1000,1000,.001).reason==h::ReferenceDynamicsReason::future_measurement,
        "frequency component from the future was accepted");
    check(gate.observe(reference,999,1000,.001).reason==h::ReferenceDynamicsReason::future_measurement,
        "future phase measurement was accepted");
    check(gate.observe(reference,1000,1000,.001).reason==h::ReferenceDynamicsReason::awaiting_anchors &&
        gate.anchor_count()==1,"causal frontier-equal anchor was not retained");
    check(gate.observe(reference,1000,1000,.001).reason==h::ReferenceDynamicsReason::nonincreasing_measurement &&
        gate.anchor_count()==1,"duplicate date became an independent anchor");
    reference.sample=999;
    reference.phase_observation_sample=reference.sample;
    check(gate.observe(reference,1000,1000,.001).reason==h::ReferenceDynamicsReason::nonincreasing_measurement,
        "backdated phase became an independent anchor");
    reference.sample=1100;reference.phase_observation_sample=reference.sample;reference.sample_rate=2000;
    check(gate.observe(reference,1100,2000,.001).reason==h::ReferenceDynamicsReason::sample_clock_mismatch &&
        gate.anchor_count()==1,"sample clock change silently reset continuity");
    gate.reset(2000);
    check(gate.anchor_count()==0 && gate.observe(reference,1100,2000,.001).anchors==1,
        "explicit new-capture reset did not bind the new sample clock");
}

void frequency_only_and_punctuation() {
    h::ReferenceDynamics gate;gate.reset(1000);
    for(std::uint64_t sample=1000;sample<=3000;sample+=1000) {
        auto reference=anchor(sample,1000,0);
        reference.phase_valid=false;reference.timing_valid=false;
        check(is(gate.observe(reference,sample,1000,.001),h::ReferenceDynamicsVerdict::unqualified,
            h::ReferenceDynamicsReason::phase_or_punctuation_unqualified),
            "frequency-only fit invented oscillator-model trend authority");
    }
    check(gate.anchor_count()==0,"frequency-only fits entered the phase-anchor history");
    auto reference=anchor(4000,1000,0);reference.dwell_uncertainty_seconds=.2;
    check(gate.observe(reference,4000,1000,.001).reason==h::ReferenceDynamicsReason::phase_or_punctuation_unqualified,
        "unresolved punctuation alias was admitted");
}

void actual_phase_provenance() {
    h::ReferenceDynamics gate;gate.reset(1000);
    auto reference=anchor(0,1000,.2);
    check(gate.observe(reference,0,1000,.001).anchors==1,"phase observation at sample zero became unknown");
    reference=anchor(40,1000,.2+tau*17.25*.04);
    auto unknown=reference;unknown.phase_observation_known=false;
    check(gate.observe(unknown,40,1000,.001).reason==h::ReferenceDynamicsReason::phase_observation_unknown &&
        gate.anchor_count()==1,"unknown phase origin became an independent anchor");
    auto carried=reference;carried.phase_observation_sample=0;
    check(gate.observe(carried,40,1000,.001).reason==h::ReferenceDynamicsReason::carried_phase_observation &&
        gate.anchor_count()==1,"carried phase became a fresh anchor");
    auto future=reference;future.phase_observation_sample=41;
    check(gate.observe(future,40,1000,.001).reason==h::ReferenceDynamicsReason::future_measurement,
        "phase provenance from the future was accepted");
    check(gate.observe(reference,40,1000,.001).anchors==2,"fresh observed phase was not retained");
    // Publishing metadata later does not create the third measured increment.
    carried=reference;carried.sample=80;
    carried.phase_radians=std::remainder(carried.phase_radians+tau*carried.frequency_hz*.04,tau);
    carried.metadata.emplace();
    check(gate.observe(carried,80,1000,.001).reason==h::ReferenceDynamicsReason::carried_phase_observation &&
        gate.anchor_count()==2,"metadata-propagated phase created a false independent anchor");
    reference=anchor(80,1000,.2+tau*17.25*.08);
    check(gate.observe(reference,80,1000,.001).verdict==h::ReferenceDynamicsVerdict::accept,
        "a genuinely observed third phase failed after a metadata-only publication");
}

void receiver_publication_provenance() {
    constexpr std::uint32_t rate=48000;
    constexpr double frequency=2100.37,delay=.03719;
    h::Options options;options.power_w=100;options.callsign="N0CALL";options.timecode=false;
    h::Generator generator(options,{frequency,.7,.1,100},1700000000,true);
    h::Receiver receiver(2100,20);
    h::ReferenceDynamics gate;gate.reset(rate);
    std::array<float,137> samples{};
    std::size_t observed=0,propagated=0,accepted=0;
    for(std::uint64_t first=0;first<rate*16/5;first+=samples.size()) {
        const auto count=std::min<std::size_t>(samples.size(),rate*16/5-first);
        for(std::size_t i=0;i<count;++i) {
            const auto time=static_cast<long double>(first+i)/rate-delay;
            samples[i]=time<0?0:static_cast<float>(std::real(generator.mix_analytic({},time,rate)*
                std::polar(1.,.81)));
        }
        receiver.push(std::span<const float>(samples).first(count),rate,first);
        const auto publication=receiver.take_update();
        if(!publication)continue;
        const auto& reference=*publication;
        if(!reference.phase_valid) {
            check(!reference.phase_observation_known,"frequency-only fit claimed an observed phase");
            continue;
        }
        check(reference.phase_observation_known && reference.phase_observation_sample<=reference.sample,
            "receiver lost causal phase-fit provenance");
        const auto before=gate.anchor_count();
        const auto result=gate.observe(reference,first+count,rate,.001);
        if(reference.metadata) {
            ++propagated;
            check(reference.phase_observation_sample<reference.sample,
                "metadata publication manufactured a new phase observation date");
            check(result.reason==h::ReferenceDynamicsReason::carried_phase_observation && gate.anchor_count()==before,
                "actual receiver metadata extrapolation entered the phase-trend history");
        } else {
            ++observed;
            check(reference.phase_observation_sample==reference.sample,"direct fit did not retain its actual phase date");
            accepted+=result.verdict==h::ReferenceDynamicsVerdict::accept;
        }
    }
    check(observed>5 && propagated>0 && accepted>0,"actual receiver provenance paths were not exercised");
}

void constant_frequency_and_dates() {
    for(const auto first:std::array<std::uint64_t,2>{1000,std::numeric_limits<std::uint64_t>::max()-10000}) {
        h::ReferenceDynamics gate;gate.reset(48000);
        constexpr double frequency=2118.125;
        for(unsigned i=0;i<6;++i) {
            const auto delta=std::uint64_t(i)*1920;
            const auto reference=anchor(first+delta,48000,.73+tau*frequency*delta/48000,frequency);
            const auto result=gate.observe(reference,reference.sample,48000,.001);
            if(i<2)check(result.verdict==h::ReferenceDynamicsVerdict::unqualified,"trend qualified before three dates");
            else check(is(result,h::ReferenceDynamicsVerdict::accept,h::ReferenceDynamicsReason::accepted) &&
                result.frequency_change_hz<=result.allowed_change_hz,"constant tone was rejected across phase cycles");
        }
    }
}

void wiener_scaled_allowance() {
    constexpr double sigma=.01,frequency=17.25;
    // Three disjoint Wiener-sized phase increments, with unequal durations.
    // These are deterministic 1- and 2-sigma realizations, not a stochastic
    // coverage claim. The model allowance must scale with sqrt(time), not Hz/s.
    constexpr std::array<double,4> time{0,.04,.44,2.04};
    constexpr std::array<double,3> normal{1,-2,1};
    h::ReferenceDynamics gate;gate.reset(10000);
    double noise_phase=0;
    for(std::size_t i=0;i<time.size();++i) {
        if(i)noise_phase+=sigma*std::sqrt(time[i]-time[i-1])*normal[i-1];
        auto reference=anchor(10000+std::llround(time[i]*10000),10000,
            .3+tau*frequency*time[i]+noise_phase,frequency,.0001,.02);
        const auto result=gate.observe(reference,reference.sample,10000,sigma);
        if(i>=2)check(result.verdict==h::ReferenceDynamicsVerdict::accept,
            "Wiener-scaled configured oscillator variation was rejected");
    }
}

void large_chirp_and_recovery() {
    constexpr double step=.4,frequency=17.25,chirp=1,sigma=.001;
    h::ReferenceDynamics gate;gate.reset(10000);
    h::ReferenceDynamicsResult result;
    for(unsigned i=0;i<3;++i) {
        const auto t=i*step;
        const auto reference=anchor(10000+i*4000,10000,.2+tau*(frequency*t+.5*chirp*t*t),
            frequency+chirp*t,.0001,.4);
        result=gate.observe(reference,reference.sample,10000,sigma);
    }
    check(is(result,h::ReferenceDynamicsVerdict::reject,h::ReferenceDynamicsReason::oscillator_innovation_exceeded) &&
        result.frequency_change_hz>.39 && result.frequency_change_hz>result.allowed_change_hz,
        "vehicle-like coherent frequency trend passed the oscillator gate");
    // A declined trend does not request/reset an external phase convention.
    // Later valid constant-frequency anchors can roll through this same gate.
    for(unsigned i=3;i<7;++i) {
        const auto t=i*step;
        const auto reference=anchor(10000+i*4000,10000,.2+tau*(frequency*t+.32),frequency,.0001,.4);
        result=gate.observe(reference,reference.sample,10000,sigma);
    }
    check(result.verdict==h::ReferenceDynamicsVerdict::accept,"trend rejection prevented later independent qualification");
}

void phase_ambiguity() {
    h::ReferenceDynamics gate;gate.reset(1000);
    auto first=anchor(1000,1000,.2,17.25,.0001,1);
    auto second=anchor(2000,1000,.2+tau*17.25,17.25,.0001,1);
    (void)gate.observe(first,1000,1000,.001);
    check(is(gate.observe(second,2000,1000,.001),h::ReferenceDynamicsVerdict::unqualified,
        h::ReferenceDynamicsReason::ambiguous_phase_unwrap),"wide prior guessed a phase cycle");
    // A nonoverlapping prior interval is also not a licensed cycle guess.
    gate.reset(1000);first.frequency_uncertainty_hz=.001;second.frequency_uncertainty_hz=.001;
    second.phase_radians=std::remainder(first.phase_radians+tau*17.25+.5,tau);
    (void)gate.observe(first,1000,1000,.001);
    check(gate.observe(second,2000,1000,.001).reason==h::ReferenceDynamicsReason::unsupported_phase_interval,
        "phase outside every prior-supported cycle was admitted");
    // Very large arithmetic cannot hide the missing branch precision.
    gate.reset(1000);first.frequency_hz=std::numeric_limits<double>::max();
    (void)gate.observe(first,1000,1000,.001);
    const auto result=gate.observe(second,2000,1000,.001);
    check(result.verdict==h::ReferenceDynamicsVerdict::unqualified,"unrepresentable phase prediction was accepted");
}
}

int main() {
    try {
        phase_forecast();chronology_and_invalid();frequency_only_and_punctuation();actual_phase_provenance();
        constant_frequency_and_dates();wiener_scaled_allowance();large_chirp_and_recovery();phase_ambiguity();
        receiver_publication_provenance();
        std::cout<<"helper reference dynamics passed\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
