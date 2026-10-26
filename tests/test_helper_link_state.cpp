#include "../src/helper_link_state.hpp"
#include <cmath>
#include <cfenv>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace h=datapump::helper;
namespace {
void check(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
bool contains(h::LinkInterval range,long double value) {
    return static_cast<long double>(range.lower)<=value&&value<=static_cast<long double>(range.upper);
}
double width(h::LinkInterval range) {return range.upper-range.lower;}
h::LinkModel model(h::LinkTopology topology=h::LinkTopology::independent_audio) {
    h::LinkModel m;m.topology=topology;m.rf_accuracy_fraction=300e-6;m.audio_accuracy_fraction=300e-6;
    m.rf_maximum_fractional_slew_per_second=0;m.audio_maximum_fractional_slew_per_second=0;
    m.maximum_current_measurement_age_seconds=.5;return m;
}
h::LinkMapping mapping() {
    h::LinkMapping m;m.capture_generation=0;m.timebase_identity=0;
    m.reference_identity_known=true;m.reference_identity=0;m.sample_rate=1000;
    m.settled_first_sample=0;m.settled_end_sample=1000000;
    m.actual_rf_shift_hz=30e6;m.helper_audio_hz=1900;m.data_audio_hz=1500;
    m.sideband_sign=1;m.mono_image_sign=1;m.provenance=h::LinkMappingProvenance::modelled_physical_capture;
    m.phase_segment=0;m.phase_continuity_known=true;return m;
}
h::Reference reference(const h::LinkMapping& m,std::uint64_t last,double rf,double audio,
        bool shared=false,double uncertainty=.0001) {
    h::Reference r;r.sample=last;r.sample_rate=m.sample_rate;r.frequency_first_sample=last>=100?last-100:0;
    r.frequency_sample=last;r.frequency_fit=h::FrequencyFit::local_polynomial;
    const auto shift=static_cast<long double>(m.sideband_sign)*m.actual_rf_shift_hz;
    const auto residual=shared?(shift+m.helper_audio_hz)*rf:shift*rf+m.helper_audio_hz*audio;
    r.frequency_hz=static_cast<double>((m.helper_audio_hz+residual)/m.mono_image_sign);
    r.frequency_uncertainty_hz=uncertainty;r.frequency_valid=true;
    // Deliberately no phase, punctuation, metadata, UTC or decoded timecode.
    return r;
}
h::LinkResult observe(h::LinkState& state,const h::Reference& r,const h::LinkMapping& m,
        std::optional<double> enclosed_slew=0) {
    return state.observe({r,m,enclosed_slew},r.sample+1);
}

void constant_shared_and_rank_one() {
    h::LinkState shared;check(shared.reset(model(h::LinkTopology::shared_radio)),"shared model rejected");
    auto m=mapping();auto r=reference(m,1000,100e-6,0,true);
    auto result=observe(shared,r,m);
    check(result.authority==h::LinkAuthority::predictive_state,"frequency-only shared state required phase/punctuation");
    check(contains(result.rf_fraction,100e-6)&&width(result.rf_fraction)<1e-9,"shared fractional estimate lost precision");
    check(result.rf_coordinate_active&&result.audio_coordinate_active&&result.fractional_coordinates_equal&&contains(result.audio_fraction,100e-6)&&
        result.audio_fraction.lower==result.rf_fraction.lower&&result.audio_fraction.upper==result.rf_fraction.upper,
        "shared reduced coordinate was reported as zero physical audio-clock error");
    check(contains(result.data_residual_hz,(30e6L+1500)*100e-6L),"shared data residual excludes the actual signal");
    check(!result.phase_bridge_allowed,"first frequency fit invented phase continuity");
    const auto predicted=shared.predict(m,2000,2);
    check(predicted.authority==h::LinkAuthority::predictive_state&&contains(predicted.rf_fraction,100e-6),"explicit static-mean prediction failed");

    h::LinkState independent;check(independent.reset(model()),"independent model rejected");
    r=reference(m,1000,1e-6,100e-6);
    result=observe(independent,r,m);
    check(result.authority==h::LinkAuthority::predictive_state,"rank-one observation declined");
    check(contains(result.audio_fraction,-300e-6)&&contains(result.audio_fraction,300e-6),
        "one fixed helper coefficient spuriously resolved the independent audio clock");
    for(int i=-3;i<=3;++i) {
        const long double audio=i*100e-6L;
        const long double rf=(30.19L-1900*audio)/30e6L;
        check(contains(result.rf_fraction,rf)&&contains(result.audio_fraction,audio),"rank-one domain lost a valid coupled clock alternative");
        check(contains(result.data_residual_hz,30e6L*rf+1500*audio),"conditional data range lost a rank-one alternative");
    }
}

void actual_hops_and_phase_boundaries() {
    auto m=mapping();h::LinkState state;check(state.reset(model()),"hop model rejected");
    auto r=reference(m,1000,1e-6,100e-6);check(observe(state,r,m).authority==h::LinkAuthority::predictive_state,"first hop rejected");
    m.actual_rf_shift_hz=60e6;m.settled_first_sample=1500;m.phase_segment=1;m.phase_continuity_known=false;
    r=reference(m,2000,1e-6,100e-6);
    auto result=observe(state,r,m);
    check(result.authority==h::LinkAuthority::predictive_state,"actual RF hop incorrectly treated its expected Hz change as drift");
    check(contains(result.rf_fraction,1e-6)&&contains(result.audio_fraction,100e-6),"two actual coefficients lost the true common state");
    check(width(result.audio_fraction)<1e-6,"well-conditioned actual coefficients failed to refine audio uncertainty");
    check(!result.phase_bridge_allowed,"unknown retune phase jump was bridged");
    check(contains(result.data_residual_hz,60e6L*1e-6L+1500*100e-6L),"hop prediction excluded actual data");
    m.phase_continuity_known=true;r=reference(m,3000,1e-6,100e-6);result=observe(state,r,m);
    check(!result.phase_bridge_allowed,"one new phase segment observation bridged an unknown predecessor");
    r=reference(m,4000,1e-6,100e-6);result=observe(state,r,m);
    check(result.phase_bridge_allowed,"unchanged explicitly continuous mapping was not reported");
    // This flag only describes mapping continuity, never phase qualification.
    check(!r.phase_valid&&!r.timing_valid,"fixture accidentally supplied a phase/timing gate");

    auto fake=m;fake.provenance=h::LinkMappingProvenance::fake_schedule_preview;
    check(observe(state,r,fake).reason==h::LinkReason::fake_mapping,"decorative FHSS frequency entered the measurement matrix");
    auto changed=m;changed.reference_identity=1;
    r=reference(changed,5000,1e-6,100e-6);
    check(observe(state,r,changed).reason==h::LinkReason::reference_changed,"unknown cross-reference calibration was carried");
}

void rolling_declared_slew_and_chirp_rejection() {
    auto mod=model(h::LinkTopology::shared_radio);mod.rf_maximum_fractional_slew_per_second=2e-9;
    auto m=mapping();h::LinkState state;check(state.reset(mod),"slew model rejected");
    const auto truth=[](std::uint64_t sample) {return 100e-6+1e-9*static_cast<double>(sample)/1000;};
    h::LinkResult first,last;
    for(std::uint64_t sample=1000;sample<=12000;sample+=1000) {
        const auto r=reference(m,sample,truth(sample),0,true);
        last=observe(state,r,m,1.);
        check(last.authority==h::LinkAuthority::predictive_state,"declared slow ramp was not followed");
        check(contains(last.rf_fraction,truth(sample)),"rolling state excluded the declared slow ramp");
        check(last.retained_halfspaces<=h::LinkState::halfspace_capacity,"state storage grew with measurements");
        if(sample==1000)first=last;
    }
    check(!contains(first.rf_fraction,truth(12000)),"fixture failed to leave the initial narrow fractional interval");
    check(contains(last.rf_fraction,truth(12000)),"first helper strip remained permanently frozen");
    check(last.required_fit_slew_hz_per_second>0&&last.required_fit_drift_phase_radius_radians>0,
        "finite-aperture drift requirement was omitted");
    auto bad=reference(m,13000,truth(13000)+100e-9,0,true);
    auto result=observe(state,bad,m,1.);
    check(result.authority==h::LinkAuthority::declined&&result.reason==h::LinkReason::oscillator_innovation_exceeded,
        "faster frequency innovation was silently reseeded as oscillator drift");
    check(state.prediction_suspended()&&!state.cached_measurement_record(),"rejected innovation left a qualified cached reference");
    check(state.predict(m,14000,2).authority==h::LinkAuthority::declined,"query resurrected rejected steering");
    bad=reference(m,14000,truth(14000)+200e-9,0,true);
    check(observe(state,bad,m,1.).authority==h::LinkAuthority::declined,"successive faster innovations walked the latent state");
    const auto good=reference(m,15000,truth(15000),0,true);
    result=observe(state,good,m,1.);
    check(result.authority==h::LinkAuthority::predictive_state&&!state.prediction_suspended(),
        "independent valid reference could not recover the preserved oscillator state");
    check(!result.phase_bridge_allowed,"same mapping bridged an unqualified rejected interval");
}

void unknown_slew_and_fit_enclosure() {
    auto mod=model();mod.rf_maximum_fractional_slew_per_second.reset();mod.audio_maximum_fractional_slew_per_second.reset();
    auto m=mapping();h::LinkState state;check(state.reset(mod),"unknown-slew model rejected");
    auto r=reference(m,1000,1e-6,100e-6);
    auto result=observe(state,r,m,std::nullopt);
    check(result.authority==h::LinkAuthority::current_measurement&&
        result.reason==h::LinkReason::current_thermal_evolution_unmodelled&&!state.has_predictive_state(),
        "unknown slew silently became a zero-drift predictive prior");
    const auto record=state.cached_measurement_record();
    check(record&&record->authority==h::LinkAuthority::declined&&record->reason==h::LinkReason::cached_record_requires_validation&&
        record->state_sample==1000,"historical cache accessor manufactured current acquisition authority");
    check(state.predict(m,1001,1).reason==h::LinkReason::unknown_evolution_prediction_declined,
        "unknown thermal evolution produced a future forecast");
    r=reference(m,2000,2e-6,-100e-6);result=observe(state,r,m,std::nullopt);
    check(result.authority==h::LinkAuthority::current_measurement&&!state.has_predictive_state(),
        "fresh current-only measurement was pinned to a first-Hz anchor");
    check(contains(result.data_residual_hz,60.L-.15L),"current-only data projection lost actual state");
    m.reference_identity_known=false;result=observe(state,r,m,std::nullopt);
    check(result.reason==h::LinkReason::current_reference_identity_unknown,"unknown reference identity was hidden");

    mod=model();mod.rf_maximum_fractional_slew_per_second=1e-9;
    check(state.reset(mod),"positive-slew reset failed");m=mapping();r=reference(m,1000,1e-6,100e-6);
    result=observe(state,r,m,std::nullopt);
    check(result.reason==h::LinkReason::within_fit_slew_not_enclosed&&!state.has_predictive_state(),
        "transition slew was applied without an actual within-fit enclosure");
    check(observe(state,r,m,.001).reason==h::LinkReason::within_fit_slew_not_enclosed,
        "insufficient fit-slew enclosure was accepted");
    result=observe(state,r,m,1.);
    check(result.authority==h::LinkAuthority::predictive_state,"sufficient fit-slew enclosure was declined");
    check(state.predict(m,5000,1).reason==h::LinkReason::prediction_horizon_exceeded,"finite prediction horizon was ignored");
}

void coordinates_images_and_invalid() {
    auto m=mapping();h::LinkState state;check(state.reset(model()),"coordinate model rejected");
    auto r=reference(m,1000,1e-6,100e-6);
    auto changed=m;changed.settled_first_sample=950;
    check(observe(state,r,changed).reason==h::LinkReason::straddled_mapping,"fit straddling actual retune was accepted");
    check(state.observe({r,m,0},999).reason==h::LinkReason::future_measurement,"future evidence was accepted");
    check(state.observe({r,m,0},1000).reason==h::LinkReason::future_measurement,"observation at exclusive input end was accepted");
    check(state.observe({r,m,0},2001).reason==h::LinkReason::stale_measurement,"old publication bypassed capture-frontier freshness");
    changed=m;changed.mono_image_sign=0;
    check(observe(state,r,changed).reason==h::LinkReason::mono_image_unresolved,"ambiguous mono image selected a branch");
    changed=m;changed.provenance=static_cast<h::LinkMappingProvenance>(99);
    check(observe(state,r,changed).reason==h::LinkReason::invalid_mapping,"unknown mapping enum became actual tuning authority");
    auto invalid=r;invalid.frequency_fit=static_cast<h::FrequencyFit>(99);
    check(observe(state,invalid,m).reason==h::LinkReason::invalid_measurement,"unknown fit kind gained predictive authority");
    changed=m;changed.sideband_sign=-1;changed.mono_image_sign=-1;changed.helper_audio_hz=-1900;changed.data_audio_hz=-1500;
    r=reference(changed,1000,1e-6,100e-6);auto result=observe(state,r,changed);
    check(result.authority==h::LinkAuthority::predictive_state&&contains(result.data_residual_hz,-30.L-.15L),
        "lower sideband/signed mono mapping had the wrong sign");
    changed.timebase_identity=1;r=reference(changed,2000,1e-6,100e-6);
    check(observe(state,r,changed).reason==h::LinkReason::timebase_changed,"new sample clock silently inherited state");
    auto bad=model();bad.audio_accuracy_fraction=-1;
    check(!state.reset(bad),"negative accuracy accepted");
    check(observe(state,r,changed).reason==h::LinkReason::invalid_model,"invalid reset left a usable old state");

    auto zero=model(h::LinkTopology::shared_radio);zero.rf_accuracy_fraction=0;
    check(state.reset(zero),"zero-accuracy model rejected");m=mapping();r=reference(m,0,0,0,true);
    check(state.observe({r,m,0},0).reason==h::LinkReason::future_measurement,"empty input frontier manufactured sample-zero evidence");
    check(observe(state,r,m).authority==h::LinkAuthority::predictive_state,"sample zero or zero-width static domain became unknown");
    r=reference(m,1000,0,0,true);r.frequency_first_sample=1;r.frequency_sample=0;
    check(observe(state,r,m).authority==h::LinkAuthority::predictive_state,"same-date zero sentinel lost its actual full support");
    r=reference(m,2000,0,0,true);r.frequency_sample=1900;r.frequency_first_sample=1901;
    check(observe(state,r,m).reason==h::LinkReason::invalid_measurement,"reversed frequency support was accepted");

    check(state.reset(model()),"audio-only reset failed");m=mapping();m.actual_rf_shift_hz=0;
    r=reference(m,1000,0,100e-6);result=observe(state,r,m);
    check(result.authority==h::LinkAuthority::predictive_state&&!result.rf_coordinate_active&&result.audio_coordinate_active&&
        contains(result.rf_fraction,-300e-6)&&contains(result.rf_fraction,300e-6)&&contains(result.audio_fraction,100e-6),
        "inactive RF coordinate was falsely declared measured zero");
}

void arithmetic_environment_and_tiny_slew() {
    check(h::link_state_detail::arithmetic_environment(),"functional test environment does not support certified arithmetic");
    auto m=mapping();h::LinkState state;check(state.reset(model()),"numeric fixture reset failed");
    auto r=reference(m,1000,1e-6,100e-6);
    check(observe(state,r,m).authority==h::LinkAuthority::predictive_state,"numeric fixture did not establish state");
    struct RestoreRounding {int mode=std::fegetround();~RestoreRounding(){std::fesetround(mode);}};
    {
        RestoreRounding restore;check(std::fesetround(FE_UPWARD)==0,"cannot construct rounding negative fixture");
        check(!h::link_state_detail::arithmetic_environment(),"directed rounding environment was accepted");
        check(state.predict(m,2000,2).reason==h::LinkReason::unsupported_numeric_environment,"predict did not recheck runtime rounding");
        check(!state.has_predictive_state(),"state accessor claimed predictive authority under unsupported rounding");
        check(!state.cached_measurement_record(),"historical accessor ignored changed arithmetic environment");
        r=reference(m,2000,1e-6,100e-6);
        check(observe(state,r,m).reason==h::LinkReason::unsupported_numeric_environment,"observe did not recheck runtime rounding");
        h::LinkState other;check(!other.reset(model()),"reset accepted unsupported arithmetic environment");
    }
    check(h::link_state_detail::arithmetic_environment(),"rounding fixture did not restore the caller environment");
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP>=2)
    struct RestoreControl {unsigned value=_mm_getcsr();~RestoreControl(){_mm_setcsr(value);}};
    {
        RestoreControl restore;_mm_setcsr(restore.value|(1U<<15)|(1U<<6));
        check(!h::link_state_detail::arithmetic_environment(),"FTZ/DAZ environment was accepted");
        check(state.predict(m,2000,2).reason==h::LinkReason::unsupported_numeric_environment,"predict ignored FTZ/DAZ");
    }
    check(h::link_state_detail::arithmetic_environment(),"control-register fixture did not restore the caller environment");
#endif
    auto tiny=model(h::LinkTopology::shared_radio);tiny.rf_maximum_fractional_slew_per_second=std::numeric_limits<double>::denorm_min();
    check(state.reset(tiny),"tiny nonzero slew model rejected before enclosure check");m=mapping();m.actual_rf_shift_hz=1e-100;
    m.helper_audio_hz=1e-100;m.data_audio_hz=1e-100;r=reference(m,1000,0,0,true,1e-110);
    const auto result=observe(state,r,m,0);
    check(result.authority==h::LinkAuthority::declined&&result.reason==h::LinkReason::within_fit_slew_not_enclosed&&
        result.required_fit_slew_hz_per_second>0&&result.required_fit_drift_phase_radius_radians>0,
        "positive tiny slew/phase requirement underflowed into a zero-slew certificate");
}

void large_dates_and_nearly_parallel_rows() {
    auto m=mapping();const auto base=std::numeric_limits<std::uint64_t>::max()-30000;
    m.settled_first_sample=base-1000;m.settled_end_sample=std::numeric_limits<std::uint64_t>::max();
    auto mod=model(h::LinkTopology::shared_radio);mod.rf_maximum_fractional_slew_per_second=2e-9;
    h::LinkState state;check(state.reset(mod),"large-date model rejected");
    auto r=reference(m,base,100e-6,0,true);
    check(observe(state,r,m,1.).authority==h::LinkAuthority::predictive_state,"large first date rejected");
    r=reference(m,base+1000,100e-6+1e-9,0,true);
    auto result=observe(state,r,m,1.);
    check(result.authority==h::LinkAuthority::predictive_state&&contains(result.rf_fraction,100e-6L+1e-9L),
        "integer dates were rounded before their small elapsed-time subtraction");

    check(state.reset(model()),"ill-conditioned reset failed");m=mapping();r=reference(m,1000,1e-6,100e-6);
    check(observe(state,r,m).authority==h::LinkAuthority::predictive_state,"first ill-conditioned row rejected");
    m.actual_rf_shift_hz+=.000001;r=reference(m,2000,1e-6,100e-6);result=observe(state,r,m);
    check(result.authority==h::LinkAuthority::predictive_state,"nearly parallel row failed closed unnecessarily");
    check(contains(result.rf_fraction,1e-6)&&contains(result.audio_fraction,100e-6),"rounded dual solve excluded actual state");
    check(width(result.audio_fraction)>100e-6,"nearly identical RF hops invented precise audio-clock knowledge");
}
}
int main() {
    try {
        static_assert(std::is_trivially_copyable_v<h::LinkState>);
        static_assert(sizeof(h::LinkState)<=2048);
        constant_shared_and_rank_one();actual_hops_and_phase_boundaries();
        rolling_declared_slew_and_chirp_rejection();unknown_slew_and_fit_enclosure();
        coordinates_images_and_invalid();large_dates_and_nearly_parallel_rows();
        arithmetic_environment_and_tiny_slew();
        std::cout<<"helper link state tests passed; sizeof="<<sizeof(h::LinkState)
            <<" long_double_digits="<<std::numeric_limits<long double>::digits<<'\n';
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
