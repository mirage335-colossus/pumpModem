#include "../src/helper_current_link_runtime.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace h=datapump::helper;
namespace {
void check(bool value,const char* message) {
    if(!value) {std::cerr<<message<<'\n';std::exit(1);}
}
constexpr auto control=h::CurrentLinkRuntime::receiver_control_scratch_allowance;
constexpr auto storage=h::CurrentLinkRuntime::storage_required();
h::LinkModel policy() {
    h::LinkModel m;m.topology=h::LinkTopology::shared_radio;
    m.rf_accuracy_fraction=m.audio_accuracy_fraction=.0003;
    m.maximum_current_measurement_age_seconds=1;return m;
}
h::LinkMapping mapping(std::uint64_t end) {
    h::LinkMapping m;m.capture_generation=12;m.timebase_identity=22;m.sample_rate=48960;
    m.reference_identity_known=true;m.reference_identity=42;
    m.actual_rf_shift_hz=30000000;m.helper_audio_hz=2062.5;m.data_audio_hz=1500;
    m.settled_end_sample=end;m.phase_segment=1;
    m.provenance=h::LinkMappingProvenance::fixed_configuration;return m;
}
h::Reference frequency(const h::LinkMapping& m,std::uint64_t first,std::uint64_t last,double epsilon) {
    h::Reference r;r.sample=r.frequency_sample=last;r.frequency_first_sample=first;r.sample_rate=m.sample_rate;
    r.frequency_hz=m.helper_audio_hz+(m.actual_rf_shift_hz+m.helper_audio_hz)*epsilon;
    r.frequency_uncertainty_hz=.1;r.frequency_fit=h::FrequencyFit::local_polynomial;r.frequency_valid=true;
    // No phase, punctuation, metadata or known transmission timestamp.
    return r;
}
void fresh_movement_and_no_unknown_prediction() {
    h::CurrentLinkRuntime runtime;const h::CurrentLinkProvider provider{7,.008726646259971648};
    check(runtime.configure(policy(),provider,storage,control),"current runtime configure declined");
    auto m=mapping(1001);const auto a=frequency(m,0,1000,.0001);
    const auto first=runtime.observe(a,m,provider,1001);
    check(first.available&&first.new_independent_fit,"first direct frequency unavailable");
    check(first.link.authority==h::LinkAuthority::current_measurement,"UNKNOWN slew acquired predictive authority");
    m.settled_end_sample=2001;const auto b=frequency(m,1001,2000,.000101);
    const auto second=runtime.observe(b,m,provider,2001);
    check(second.available&&second.new_independent_fit,"fresh interval frozen to first CI");
    check(second.link.rf_fraction.lower>first.link.rf_fraction.upper,"independent current strips were intersected across time");
    check(second.correction_hz<first.correction_hz-29.9,"fractional movement did not change current data correction");
    check(!runtime.has_predictive_state(),"UNKNOWN thermal evolution retained a predictive domain");
    check(runtime.predict(m,2001,1).authority==h::LinkAuthority::declined,"UNKNOWN thermal evolution authorized a future sample");
    m.settled_end_sample=2002;
    const auto duplicate=runtime.observe(b,m,provider,2002);
    check(duplicate.available&&!duplicate.new_independent_fit&&duplicate.last_sample==2000,
        "repeated frequency publication renewed its actual evidence date");
    m.settled_end_sample=60001;
    const auto stale=runtime.current(m,provider,60001);
    check(!stale.available&&stale.link.reason==h::LinkReason::stale_measurement,"cached frequency bypassed current age");
}
void genuine_mapping_and_provider_seam() {
    h::CurrentLinkRuntime runtime;const h::CurrentLinkProvider provider{7,.008726646259971648};
    check(runtime.configure(policy(),provider,storage,control),"mapping configure declined");
    auto m=mapping(1001);
    check(runtime.observe(frequency(m,0,1000,.0001),m,provider,1001).available,"initial map unavailable");
    auto changed=m;changed.actual_rf_shift_hz=60000000;changed.phase_segment=2;
    changed.settled_first_sample=5000;changed.settled_end_sample=6001;
    check(!runtime.observe(frequency(changed,5000,6000,.0001),changed,provider,6001).available,
        "fixed/fake configuration manufactured an actual RF retune");
    changed.provenance=h::LinkMappingProvenance::modelled_physical_capture;
    const h::CurrentLinkProvider reset_provider{8,provider.phase_noise_radians_per_sqrt_second};
    const auto actual=runtime.observe(frequency(changed,5000,6000,.0001),changed,reset_provider,6001);
    check(actual.available,"genuine settled-map/new-provider seam declined");
    check(actual.link.rf_fraction.lower<.0001&&actual.link.rf_fraction.upper>.0001,
        "changed RF failed to re-express the same fractional oscillator");
    check(std::abs(actual.correction_hz+6000.15)<.11,"changed RF reused the old scalar hertz correction");
    check(!actual.link.phase_bridge_allowed,"frequency-only retune invented phase continuity");
    auto next=changed;next.actual_rf_shift_hz=120000000;next.phase_segment=3;
    next.settled_first_sample=8000;next.settled_end_sample=9001;
    const auto straddled=runtime.observe(frequency(next,7999,9000,.0001),next,reset_provider,9001);
    check(!straddled.available&&straddled.link.reason==h::LinkReason::straddled_mapping,
        "frequency fit straddled a genuine RF mapping");
    next.settled_end_sample=9011;
    check(runtime.observe(frequency(next,8000,9010,.0001),next,reset_provider,9011).available,
        "fresh contained fit did not recover after straddle decline");
}
void optional_budget_and_capture_reset() {
    h::CurrentLinkRuntime short_grant;
    const h::CurrentLinkProvider provider{7,.008726646259971648};
    check(!short_grant.configure(policy(),provider,storage-1,control),"underfunded current owner admitted");
    check(!short_grant.configure(policy(),provider,storage,control-1),"underfunded serialized query admitted");
    h::CurrentLinkRuntime runtime;
    check(runtime.configure(policy(),provider,storage,control),"budget configure declined");
    auto m=mapping(1001);
    check(runtime.observe(frequency(m,0,1000,.0001),m,provider,1001).available,"budget initial fit declined");
    auto other=m;other.capture_generation++;
    check(!runtime.current(other,provider,1001).available,"capture reset borrowed prior frequency authority");
    runtime.release_optional_work();
    check(!runtime.observe(frequency(m,1001,2000,.000101),m,provider,2001).available,"unfunded optional worker entered");
}
void actual_ring_record_projection_and_provider_lifecycle() {
    h::CurrentLinkRuntime runtime;const h::CurrentLinkProvider provider{7,.008726646259971648};
    check(runtime.configure(policy(),provider,storage,control),"ring owner configure declined");
    auto m=mapping(1001);m.reference_identity_known=false;
    const auto old=frequency(m,0,1000,.0001);
    const auto first=runtime.evaluate_current(old,m,provider,1001,control);
    check(first.available&&first.new_independent_fit,"actual raw record was not queried");
    m.settled_end_sample=2001;const auto recent=frequency(m,1001,2000,.000101);
    const auto latest=runtime.evaluate_current(recent,m,provider,2001,control);
    check(latest.available&&latest.new_independent_fit,"current mean movement froze to the first CI");
    auto profile=m;profile.data_audio_hz=2500;
    const auto projected=runtime.evaluate_current(recent,profile,provider,2001,control);
    check(projected.available&&!projected.new_independent_fit,"new DATA profile falsely required an RF retune");
    check(std::abs(projected.correction_hz-latest.correction_hz+.101)<.001,
        "new DATA profile did not use fractional current correction");
    const auto queued=runtime.evaluate_current(old,m,provider,2001,control);
    check(queued.available&&!queued.new_independent_fit&&queued.last_sample==1000,
        "queued raw record acquired a newer evidence date");
    const auto still_latest=runtime.current(m,provider,2001);
    check(still_latest.available&&still_latest.last_sample==2000,
        "older queued record replaced the latest independent measurement");
    const h::CurrentLinkProvider next{8,provider.phase_noise_radians_per_sqrt_second};
    check(!runtime.evaluate_current(recent,m,next,2001,control).available,"provider mismatch reused prior authority");
    check(runtime.reset_provider(next,control),"joined provider reset declined");
    check(!runtime.current(m,next,2001).available,"provider reset retained prior authority");
    m.timebase_identity++;m.phase_segment++;
    check(runtime.evaluate_current(recent,m,next,2001,control).available,"fresh actual record did not bind the reset provider");
    check(!runtime.has_predictive_state(),"current ring path manufactured a thermal prediction");
}
}
int main() {
    fresh_movement_and_no_unknown_prediction();
    genuine_mapping_and_provider_seam();
    optional_budget_and_capture_reset();
    actual_ring_record_projection_and_provider_lifecycle();
    std::cout<<"private current fractional caller checks passed\n";
}
