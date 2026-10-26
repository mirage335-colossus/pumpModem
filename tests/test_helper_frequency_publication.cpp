#include "../src/live_helper_frequency_publication.hpp"
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace d=datapump::live::detail;
namespace h=datapump::helper;
namespace m=datapump::modem;
namespace {
void check(bool value,const char* message) {if(!value){std::cerr<<message<<'\n';std::exit(1);}}
m::Config config() {
    m::Config c;c.sample_rate=6000;c.carrier_hz=1500;c.oscillator_search.emplace();
    auto& p=*c.oscillator_search;p.reference=m::OscillatorReference::shared_radio;
    p.rf_shift_hz=30000000;p.lf.accuracy_ppm=p.rf.accuracy_ppm=100;p.margin=3;return c;
}
h::Reference raw(const m::Config& c,std::uint64_t last,double epsilon) {
    h::Reference r;r.sample_rate=48960;r.sample=r.frequency_sample=last;
    r.frequency_first_sample=last-2000;r.frequency_hz=2062.5+(c.oscillator_search->rf_shift_hz+2062.5)*epsilon;
    r.frequency_uncertainty_hz=.1;r.frequency_fit=h::FrequencyFit::local_polynomial;r.frequency_valid=true;
    r.phase_valid=r.timing_valid=r.phase_observation_known=true;r.phase_observation_sample=last;
    r.punctuation.known=true;return r;
}
h::LinkModel model() {
    h::LinkModel p;p.topology=h::LinkTopology::shared_radio;
    p.rf_accuracy_fraction=p.audio_accuracy_fraction=.0003;p.maximum_current_measurement_age_seconds=1;return p;
}
void preserved_raw_record_and_source_date() {
    const auto c=config();const h::CurrentLinkProvider provider{7,.008726646259971648};
    const auto original=raw(c,489600,.0001);const auto frontier=original.sample+200;
    const auto p=d::helper_frequency_publication(c,2062.5,original,12,provider,0,frontier);
    check(bool(p),"actual frequency publication unavailable");
    // Evict every observer slot; owned evidence never points into this ring.
    std::array<std::optional<h::Reference>,4> ring;
    ring[0]=original;d::HelperOwnedReference queued{original,p};
    for(std::size_t i=0;i<32;++i)ring[i%ring.size()]=raw(c,original.sample+1000+i,.000101);
    const auto retained=queued.frequency->reference();
    check(retained.sample==original.sample&&retained.frequency_first_sample==original.frequency_first_sample&&
        retained.frequency_hz==original.frequency_hz,"ring eviction changed queued raw evidence");
    check(queued.frequency->raw_captured_end==frontier,"decoder today changed actual source ingress");
    check(!retained.phase_valid&&!retained.timing_valid&&!retained.phase_observation_known&&!retained.punctuation.known,
        "retrospective frequency source manufactured phase/punctuation");
    check(queued.value.phase_valid&&queued.value.phase_observation_sample==original.sample,
        "capsule changed the original observed phase endpoint");
    h::CurrentLinkRuntime runtime;
    check(runtime.configure(model(),provider,sizeof(runtime),h::CurrentLinkRuntime::receiver_control_scratch_allowance),
        "funded runtime unavailable");
    check(runtime.evaluate_current(retained,p->mapping,provider,p->raw_captured_end,
        h::CurrentLinkRuntime::receiver_control_scratch_allowance).available,"owned old raw record lost acquisition authority");
    auto today=p->mapping;today.settled_end_sample=frontier+2ULL*original.sample_rate;
    check(!runtime.evaluate_current(retained,today,provider,today.settled_end_sample,
        h::CurrentLinkRuntime::receiver_control_scratch_allowance).available,"source date became an arbitrary future frequency lease");
}
void exact_projection_and_later_staged_fit() {
    const auto c=config();const h::CurrentLinkProvider provider{7,.008726646259971648};
    const auto source=raw(c,489601,.0001);
    const auto p=d::helper_frequency_publication(c,2062.5,source,12,provider,0,source.sample+200);
    check(bool(p),"projection publication unavailable");
    auto logical=source;const std::uint64_t origin=6000;
    logical.sample=*d::HelperFrequencyPublication::map_sample(source.sample,source.sample_rate,c.sample_rate,true)-origin;
    logical.frequency_sample=*d::HelperFrequencyPublication::map_sample(source.frequency_sample,source.sample_rate,c.sample_rate,false)-origin;
    logical.frequency_first_sample=*d::HelperFrequencyPublication::map_sample(source.frequency_first_sample,source.sample_rate,c.sample_rate,false)-origin;
    logical.sample_rate=c.sample_rate;
    check(p->matches(logical,false,origin),"exact logical/rebase source match declined");
    check(p->contains_logical_frontier(logical.sample+1,origin,c.sample_rate),"actual queued ingress is unavailable");
    auto wrong=logical;wrong.frequency_first_sample++;
    check(!p->matches(wrong,false,origin),"different fit support borrowed capsule provenance");
    check(!p->contains_logical_frontier(UINT64_MAX,origin,c.sample_rate),"unobserved logical frontier was accepted");
    // A newly selected later stage fit remains separately owned. Earlier block
    // observations keep their original sample and phase flags/dates.
    const auto earlier=raw(c,450000,.000099);
    d::HelperOwnedReference block{earlier,{}};
    const auto stage_selected=*p;
    check(stage_selected.matches(source,true),"later stage source no longer matches its raw domain");
    check(block.value.sample==earlier.sample&&block.value.phase_observation_sample==earlier.sample,
        "later frequency selection redated an earlier block phase");
    check(stage_selected.reference().sample==source.sample&&!stage_selected.reference().phase_valid,
        "later stage selection gained earlier phase authority");
}
void finite_date_and_helper_off_pointer_shape() {
    static_assert(sizeof(std::unique_ptr<d::HelperOwnedReference>)==sizeof(std::unique_ptr<h::Reference>));
    check(!d::HelperFrequencyPublication::map_sample(UINT64_MAX,1,2,false),"sample map overflow was accepted");
    check(d::HelperFrequencyPublication::map_sample(UINT64_MAX,1,1,true)==UINT64_MAX,
        "exact maximum same-rate endpoint was lost");
    check(d::HelperFrequencyPublication::map_sample(3,2,1,false)==1&&
        d::HelperFrequencyPublication::map_sample(3,2,1,true)==2,"integer source projection changed floor/ceil");
}
void metadata_free_assistance_preserves_observation() {
    const auto c=config();auto source=raw(c,489600,.0001);
    source.metadata.emplace();source.metadata->callsign="NINETEEN-CHAR-ID-12";
    source.phase_radians=.3;source.phase_uncertainty_radians=.01;
    source.dwell_start_seconds=.24;source.dwell_uncertainty_seconds=.001;
    source.punctuation={7,480000,479500,480010,.2,.4,true};
    std::optional<d::HelperOwnedReference> owned{d::HelperOwnedReference{source,{}}};
    static_assert(noexcept(d::helper_reference_value(owned)));
    static_assert(noexcept(d::helper_reference_value(owned?&*owned:nullptr)));
    const auto copy=d::helper_reference_value(owned);
    check(copy&&!copy->metadata,"decoder assistance copied optional callsign allocation");
    check(copy->sample==source.sample&&copy->sample_rate==source.sample_rate&&
        copy->frequency_hz==source.frequency_hz&&copy->frequency_uncertainty_hz==source.frequency_uncertainty_hz&&
        copy->frequency_sample==source.frequency_sample&&copy->frequency_first_sample==source.frequency_first_sample&&
        copy->frequency_fit==source.frequency_fit&&copy->frequency_phase_diffusion_gain==source.frequency_phase_diffusion_gain&&
        copy->frequency_process_uncertainty_hz==source.frequency_process_uncertainty_hz&&
        copy->phase_radians==source.phase_radians&&copy->phase_uncertainty_radians==source.phase_uncertainty_radians&&
        copy->phase_observation_sample==source.phase_observation_sample&&copy->phase_observation_known&&
        copy->phase_valid&&copy->timing_valid&&copy->frequency_valid&&
        copy->dwell_start_seconds==source.dwell_start_seconds&&copy->dwell_uncertainty_seconds==source.dwell_uncertainty_seconds&&
        copy->punctuation.generation==source.punctuation.generation&&copy->punctuation.sample==source.punctuation.sample&&
        copy->punctuation.first_sample==source.punctuation.first_sample&&copy->punctuation.last_sample==source.punctuation.last_sample&&
        copy->punctuation.fraction==source.punctuation.fraction&&
        copy->punctuation.uncertainty_samples==source.punctuation.uncertainty_samples&&copy->punctuation.known,
        "allocation-free assistance changed original frequency/phase/punctuation observations");
    check(owned->value.metadata&&owned->value.metadata->callsign==source.metadata->callsign&&
        owned->value.phase_observation_sample==source.phase_observation_sample,
        "assistance extraction changed immutable queued metadata/phase");
}
}
int main() {
    preserved_raw_record_and_source_date();exact_projection_and_later_staged_fit();finite_date_and_helper_off_pointer_shape();
    metadata_free_assistance_preserves_observation();
    std::cout<<"helper frequency publication ownership checks passed\n";
}
