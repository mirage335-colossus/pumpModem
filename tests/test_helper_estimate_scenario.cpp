#include "datapump/simulation_estimate.hpp"
#include "datapump/pattern_receiver.hpp"
#include <limits>
#include "datapump/pattern_search.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/live.hpp"
#include "live_helper_prior.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace datapump;
namespace {
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
void near(double x,double y,const char* text){check(std::isfinite(x)&&std::abs(x-y)<=1e-10*std::max(1.,std::abs(y)),text);}
void strong_requirements() {
    const auto low=simulation::helper_strong_requirements(6000,2200,20,80,0,0);
    const auto high=simulation::helper_strong_requirements(6000,2200,20,100,0,0);
    check(low.frequency_requirements_available&&high.frequency_requirements_available,"strong finite WLS requirement unavailable");
    near(low.frequency_radius_hz/high.frequency_radius_hz,10,"WLS frequency radius does not follow actual expected-noise C/N0 scale");
    check(high.fit_samples==240&&high.fit_seconds==.04,"forecast changed actual40ms fit geometry");
    check(high.chord_requirements_available&&high.chord_aperture_seconds>=.4&&high.chord_aperture_seconds<1.5,
        "forecast invented arbitrary long chord averaging or omitted actual cycle gate");
    const auto weak=simulation::helper_strong_requirements(48000,2200,20,30.7609,0,0);
    check(!weak.frequency_requirements_available&&weak.expected_explained_fraction<.9&&
        weak.reason.find("90%")!=std::string::npos,"weak EME was relabelled a strong-fit forecast");
    const auto native=simulation::helper_masked_frequency_requirements(48960,5462.6773731334843,9000.45,
        .5*std::acos(-1.)/180,true);
    check(native.geometry_supported&&native.coarse_required&&native.required_bytes>0&&native.coarse_bytes>0&&
        native.aperture_seconds>1.30&&native.aperture_seconds<1.32&&native.profile_half_width_hz==25.90625&&
        native.cold_deadline_seconds==10,"actual native frequency-only profile was excluded by the short strong-fit gate");
    check(!simulation::helper_strong_requirements(48960,5462.75,20,49.7403126773,
        .5*std::acos(-1.)/180,0).frequency_requirements_available,
        "native component eligibility was relabelled as a short polynomial fit");
    const auto logical=simulation::helper_masked_frequency_requirements(6120,2200,20,0,true);
    check(!logical.geometry_supported&&logical.reason.find("physical")!=std::string::npos,
        "logical sample rate was passed off as native physical capture");
    const auto process=simulation::helper_strong_requirements(6000,2200,20,100,.005,0);
    check(process.frequency_requirements_available&&process.frequency_radius_hz>high.frequency_radius_hz&&
        process.phase_radius_radians>high.phase_radius_radians,"configured phase process disappeared from forecast");
    check(!simulation::helper_strong_requirements(6000,2995,20,100,0,0).frequency_requirements_available,
        "raw Nyquist capture limit was ignored");
    check(!simulation::helper_strong_requirements(6000,2200,20,100,1,0).chord_requirements_available,
        "large oscillator process was assigned a canonical phase chord");
}
transfer::Options options() {
    transfer::Options o;auto& c=o.modem;c.sample_rate=6000;c.carrier_hz=1500;c.bandwidth_hz=36;
    c.integration_seconds=1;c.receiver_strategy=modem::ReceiverStrategy::complex_only;
    c.oscillator_search.emplace();c.oscillator_search->lf={.05,0};c.oscillator_search->rf={1,0};
    c.oscillator_search->rf_shift_hz=30000000;c.oscillator_search->margin=3;
    o.key.emplace(Bytes(32,0x73));c.scramble=true;o.search_seconds=3;o.dsp_workspace_bytes=256*1024*1024;
    return o;
}
void original_domain() {
    const auto o=options();const std::array<std::uint8_t,8> bits{1,0,1,1,0,0,1,0};
    const auto tx=transfer::estimate_binary(bits,o);
    modem::ChannelConfig ch;ch.clock_error_ppm=.05;ch.frequency_offset_hz=30;
    ch.phase_noise_degrees_per_sqrt_second=0;ch.snr_db=0;
    simulation::HelperFrequencyForecastRequest request;request.physical_sample_rate=6000;
    request.physical_passband_upper_hz=2999;request.helper_frequency_hz=2200;
    request.expected_usable_helper_cn0_db_hz=120;
    const auto forecast=simulation::helper_frequency_forecast(tx,o,ch,request);
    check(forecast.scenario.has_value(),forecast.reason.c_str());
    check(forecast.scenario->origin==simulation::HelperScenarioOrigin::requirements_forecast,
        "source requirements manufactured validated utility or observed qualification");
    const auto baseline=simulation::estimate(tx,o,true,ch,{},1,false,100,64,
        simulation::ReceiverWorkMode::hardware_fallback);
    const auto result=simulation::estimate_helper_frequency(tx,o,true,ch,*forecast.scenario,{},1,false,100,64,
        simulation::ReceiverWorkMode::hardware_fallback);
    check(result.scenario_available,result.reason.c_str());
    near(result.baseline.receiver_cpu_seconds,baseline.receiver_cpu_seconds,"ordinary baseline CPU changed");
    near(result.assisted.logical_acquisition_threshold,baseline.logical_acquisition_threshold,"original logical threshold changed");
    near(result.assisted.logical_search_trials,baseline.logical_search_trials,"original logical trial domain changed");
    near(result.assisted.initial_epoch_setup_seconds,baseline.initial_epoch_setup_seconds,
        "reported original constructor scoring work changed");
    near(result.assisted.new_epoch_setup_seconds,baseline.new_epoch_setup_seconds,
        "reported rolling constructor scoring work changed");
    near(result.assisted.tracking_seconds,baseline.tracking_seconds,"ordinary full-domain continuation changed");
    near(result.assisted.tracking_symbol_windows,baseline.tracking_symbol_windows,"physical absence/payload windows changed");
    check(result.assisted.execution_projection_bin_samples==baseline.execution_projection_bin_samples,
        "mask invented coarser native projection geometry");
    check(result.assisted.frequency_rate_hypotheses<baseline.frequency_rate_hypotheses,
        "qualified original mask did not reduce actual executed frequency-rate work");
    check(!result.conditional_one_bit_probability_available&&!result.qualified_cost_available&&
        !result.discovery_cost_available&&!result.forecast_available,
        "missing input covariance/component costs/utility coverage was silently promoted");
    const auto bank=modem::oscillator_pattern_search(o.modem);
    const auto native_radius=modem::native_pattern_frequency_cell_radius(o.modem);
    check(native_radius==.125*o.modem.sample_rate/modem::symbol_sample_count(o.modem)&&
        native_radius>bank.frequency.step_hz/2,
        "fixture no longer distinguishes native RF cell authority from generated endpoint spacing");
    for(const auto& cell:forecast.scenario->profiles.front().cells)
        check(cell.frequency_half_width_hz==native_radius,"forecast changed the actual native RF cell");
    for(const auto toward:{0.,std::numeric_limits<double>::infinity()}) {
        auto changed=*forecast.scenario;changed.profiles[0].cells.front().frequency_half_width_hz=
            std::nextafter(native_radius,toward);
        check(!simulation::estimate_helper_frequency(tx,o,true,ch,changed,{},1,false,100,64,
            simulation::ReceiverWorkMode::hardware_fallback).scenario_available,
            "one-ULP change to native RF-cell identity was accepted");
    }
    std::set<double> all,selected;for(const auto& h:bank.hypotheses)all.insert(h.clock_error_ppm);
    for(const auto& cell:forecast.scenario->profiles.front().cells)selected.insert(cell.hypothesis.clock_error_ppm);
    check(all==selected,"forecast lost unresolved LF alternatives");
    auto measured=*forecast.scenario;measured.origin=simulation::HelperScenarioOrigin::measured_reference;
    measured.profiles[0].input.ordinary_noise_model_evidence="test-only covariance prerequisite";
    const auto sparse=simulation::estimate_helper_frequency(tx,o,true,ch,measured,{},1,true,100,64,
        simulation::ReceiverWorkMode::hardware_fallback);
    check(sparse.scenario_available&&!sparse.conditional_one_bit_probability_available&&
        !sparse.conditional_probability_available&&sparse.assisted.probability_model_limit.find("sparse RF")!=std::string::npos,
        "declared covariance evidence bypassed sparse probability-domain limitation");
    auto constrained=o;constrained.dsp_workspace_bytes=1;
    auto priced=*forecast.scenario;priced.costs.validation_evidence="test-only cost gate";
    priced.costs.cpu_reference_id=simulation::reference_cpu;
    priced.costs.additional_frontend_cpu_per_second=0;priced.costs.optional_probe_cpu_per_second=0;
    const auto no_workspace=simulation::estimate_helper_frequency(tx,constrained,true,ch,priced,{},1,false,100,64,
        simulation::ReceiverWorkMode::hardware_fallback);
    check(!no_workspace.assisted.receiver_workspace_supported&&!no_workspace.qualified_cost_available,
        "qualified CPU became available without the retained original owner's workspace");
    auto ineligible=o;ineligible.modem.receiver_strategy=modem::ReceiverStrategy::existing_only;
    check(!simulation::helper_frequency_forecast(tx,ineligible,ch,request).scenario,
        "Existing-only policy manufactured a rolling helper forecast");
    live::Settings cochannel;cochannel.transfer=o;cochannel.helper.power_w=15000;
    cochannel.helper.frequency=helper::Frequency::cochannel;
    check(!simulation::helper_frequency_forecast(tx,o,ch,request,{},simulation::ReceiverWorkMode::hardware_fallback,{},&cochannel).scenario,
        "Retired cochannel policy manufactured an isolated helper forecast");
    auto filter_request=request;filter_request.helper_frequency_hz=2990;
    const auto filter_limit=simulation::helper_frequency_forecast(tx,o,ch,filter_request);
    check(!filter_limit.scenario&&filter_limit.reason.find("filter is ineligible")!=std::string::npos,
        "Native fixed-filter passband/tap eligibility was omitted");
    auto bad=*forecast.scenario;bad.profiles[0].cells.clear();
    const auto missing=simulation::estimate_helper_frequency(tx,o,true,ch,bad,{},1,false,100,64,
        simulation::ReceiverWorkMode::hardware_fallback);
    check(!missing.scenario_available,"incomplete conditional strip was accepted");
    bad=*forecast.scenario;bad.profiles[0].cells.front().clock_lower_ppm-=1;
    check(!simulation::estimate_helper_frequency(tx,o,true,ch,bad,{},1,false,100,64,
        simulation::ReceiverWorkMode::hardware_fallback).scenario_available,"widened original clock cell was accepted");
    auto phase=request;phase.assume_independently_qualified_punctuation=true;
    const auto with_phase=simulation::helper_frequency_forecast(tx,o,ch,phase);
    check(with_phase.scenario.has_value(),"independent optional punctuation assumption broke frequency forecast");
    check(forecast.scenario->profiles[0].physical_domain.frequency_half_width_hz==forecast.requirements.frequency_radius_hz,
        "frequency-only forecast depended on punctuation");
    check(o.modem.bandwidth_hz==36&&o.modem.sample_rate==6000&&ch.snr_db==0,
        "forecast changed payload Rate/SNR or input policy");
}
}
int main(){try{strong_requirements();original_domain();std::cout<<"helper scenario PASS\n";}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
