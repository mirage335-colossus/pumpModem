#include "../src/live_helper.hpp"
#include <iostream>
using namespace datapump;
void check(bool v,const char* text){if(!v)throw Error(text);}
int main(){try {
    std::array<std::optional<live::detail::HelperMeasurement>,4> history;
    for(std::size_t i=0;i<history.size();++i) {
        helper::Reference r;r.sample=100+i;r.sample_rate=48000;r.frequency_sample=90+i;
        r.phase_observation_sample=80+i;r.frequency_first_sample=70+i;
        r.frequency_hz=1234.5+i;r.phase_radians=.1+i;r.frequency_uncertainty_hz=.02+i;
        r.phase_uncertainty_radians=.03+i;r.dwell_start_seconds=.24+i;r.dwell_uncertainty_seconds=.00001+i;
        r.frequency_phase_diffusion_gain=.4+i;r.frequency_process_uncertainty_hz=.5+i;
        r.frequency_fit=helper::FrequencyFit::local_polynomial;
        r.frequency_valid=r.phase_valid=r.timing_valid=r.phase_observation_known=true;
        r.punctuation={4+i,20+i,10+i,30+i,.25,.75,true};
        r.metadata=helper::Metadata{helper::MetadataKind::callsign,"KEPT SEPARATELY",42,false};
        history[i]=live::detail::HelperMeasurement(r);
        const auto restored=history[i]->reference();
        check(restored.sample==r.sample&&restored.sample_rate==r.sample_rate&&
            restored.frequency_sample==r.frequency_sample&&restored.phase_observation_sample==r.phase_observation_sample&&
            restored.frequency_first_sample==r.frequency_first_sample&&restored.frequency_fit==r.frequency_fit,
            "history changed measurement dates/fit/clock");
        check(restored.frequency_hz==r.frequency_hz&&restored.phase_radians==r.phase_radians&&
            restored.frequency_uncertainty_hz==r.frequency_uncertainty_hz&&restored.phase_uncertainty_radians==r.phase_uncertainty_radians&&
            restored.dwell_start_seconds==r.dwell_start_seconds&&restored.dwell_uncertainty_seconds==r.dwell_uncertainty_seconds&&
            restored.frequency_phase_diffusion_gain==r.frequency_phase_diffusion_gain&&
            restored.frequency_process_uncertainty_hz==r.frequency_process_uncertainty_hz,
            "history changed numeric measurement/uncertainty");
        check(restored.frequency_valid&&restored.phase_valid&&restored.timing_valid&&restored.phase_observation_known,
            "history dropped a qualification flag");
        const auto& p=restored.punctuation;const auto& q=r.punctuation;
        check(p.generation==q.generation&&p.sample==q.sample&&p.first_sample==q.first_sample&&p.last_sample==q.last_sample&&
            p.fraction==q.fraction&&p.uncertainty_samples==q.uncertainty_samples&&p.known==q.known,
            "history changed actual marker provenance");
        check(!restored.metadata,"numeric measurement history retained metadata storage");
        const auto rebased=live::detail::rebase_helper_reference(restored,1,200,48000);
        check(rebased&&!rebased->punctuation.known,"unimplemented logical-origin transform retained raw marker authority");
    }
    check(history.front()->sample==100&&history.back()->sample==103,"four-entry history was reduced");
    helper::Reference zero;zero.phase_observation_known=true;zero.punctuation.known=true;
    const auto exactzero=live::detail::HelperMeasurement(zero).reference();
    check(exactzero.phase_observation_known&&exactzero.phase_observation_sample==0&&exactzero.punctuation.known&&
        exactzero.punctuation.sample==0,"zero-valued known provenance became unknown");
    std::cout<<"measurement_history reference="<<sizeof(helper::Reference)<<" numeric="<<sizeof(live::detail::HelperMeasurement)
        <<" history_saved="<<sizeof(std::array<std::optional<helper::Reference>,4>)-sizeof(history)
        <<" all4_numeric_and_marker_records_exact metadata_separate\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
