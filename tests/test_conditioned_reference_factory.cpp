#include "conditioned_reference_factory.hpp"
#include "live_receiver_admission.hpp"
#include "live_helper_prior.hpp"
#include "fhss_profile.hpp"
#include "datapump/tuning.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace datapump;
namespace {
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
live::Settings b_settings() {
    live::Settings value;auto& o=value.transfer;
    o.key.emplace(Bytes(32,0x74));o.search_seconds=3;o.dsp_workspace_bytes=128*1024*1024;
    value.dsp_workspace_bytes=o.dsp_workspace_bytes;
    o.audio_timing_error_seconds=0; // Match --audio-error0ms before resolving the production hop calendar.
    o.modem=tuning::resolve(360,40,tuning::PatternMode::auto_keystream,true,1500,10,
        modem::OuterDsssVersion::interleaved_v2).config;
    auto& c=o.modem;c.oscillator_search.emplace();auto& p=*c.oscillator_search;
    p.lf={100,.5};p.rf={1,.05};p.rf_shift_hz=3e7;p.margin=3;
    p.reference=modem::OscillatorReference::independent_audio;
    // Component coverage explicitly selects production FP64, not a claim
    // that Default's automatically resolved arithmetic has been qualified.
    c.search_arithmetic=modem::SearchArithmetic::fp64;
    const auto hops=gui::fhss::resolve(gui::fhss::Preference::fcc,c);
    check(hops.available,"actual B FHSS plan unavailable");
    modem::FhssPlan plan;plan.channels=hops.channels;plan.dwell_seconds=hops.dwell_seconds;
    for(unsigned i=0;i<plan.channels;++i)plan.shifts[i]=hops.center(i)-c.carrier_hz;
    c.fake_hopping=transfer::transmit_hopping(plan,o);
    c=transfer::seeded_config(o,1800000000);
    value.helper.power_w=15000;value.helper.data_power_w=4;value.helper.ratio_limit_db=90;
    return value;
}
}
int main(){try {
    using namespace simulation::detail;
    auto value=b_settings();const auto& c=value.transfer.modem;
    check(c.sample_rate==14400&&modem::symbol_sample_count(c)==5120&&modem::deferred_symbols(c),
        "factory fixture lost user's production B sampled/calendar geometry");
    live::detail::ReceiverAdmissionContext context;context.now=c.stream_epoch;
    context.capacity=live::receiver_workspace_capacity(value);context.keyed=true;
    context.control_margin=live::receiver_control_margin(c,true);
    const auto prepared=live::detail::prepare_receiver_search(c,value,c.stream_epoch,context);
    modem::PatternReceiver receiver(c,prepared.search.receiver_strategy_workspace_bytes,prepared.search);
    const auto before=receiver.acquisition_state();const auto before_work=receiver.fft_work();
    modem::detail::ConditionedReferenceDescriptor descriptor;
    check(receiver.export_conditioned_reference(descriptor,32768,32768),descriptor.reason.c_str());
    check(descriptor.frequency_cell_radius_hz==modem::native_pattern_frequency_cell_radius(c),
        "pristine actual owner did not export native RF-cell authority");
    const auto owned=sizeof(descriptor)+descriptor.jobs.capacity()*sizeof(modem::detail::FftSearchJob)+
        (descriptor.source_offsets.capacity()+descriptor.phase_uppers.capacity())*sizeof(std::uint64_t)+
        descriptor.reason.capacity()+1;
    check(descriptor.descriptor_bytes==owned,"descriptor retained-byte count omitted dynamic reason storage");
    check(descriptor.original_trial_charge==descriptor.original_starts*descriptor.jobs.size()&&
        descriptor.original_threshold_trials==descriptor.original_trial_charge,
        "actual complete original trial charge was replaced by selected jobs");
    const auto original=receiver.acquisition_hypotheses();check(original.size()==descriptor.original_pairs,
        "export did not retain actual native pair identity");
    for(const auto& job:descriptor.jobs)check(job.frequency_index<original.size()&&
        job.frequency_hz==original[job.frequency_index].frequency_offset_hz&&
        job.clock_ratio==1+original[job.frequency_index].clock_error_ppm*1e-6,
        "export changed actual frequency/LF jobs");
    const auto helper=helper::plan(value.helper,helper::placement_band(c),120000000,0,
        modem::outer_dsss_transmit_gain(c));
    const auto mask=live::detail::helper_prior_mask(original,30+helper.frequency_hz*100e-6,.01,
        c.carrier_hz-helper.frequency_hz,.125*c.sample_rate/static_cast<double>(modem::symbol_sample_count(c)),
        before.admitted_clock_cell_half_width_ppm.value(),before.admitted_clock_half_width_ppm.value(),false);
    check(mask.admitted,"declared finite reference did not select conservative original cells");
    conditioned_factory::SourceConditions source;source.channel.clock_error_ppm=100;
    source.channel.frequency_offset_hz=30;source.channel.phase_noise_degrees_per_sqrt_second=0;
    source.payload_filter_hz=helper.frequency_hz;source.independent_prior=true;
    source.prior_evidence_end_sample=1; // Explicit exclusive fixed-prior boundary in this component fixture.
    const auto built=conditioned_factory::make_first_bit_request(receiver,
        std::span(mask.cells).first(original.size()),source);
    check(built.request.has_value(),built.reason.c_str());
    check(built.request->original_jobs.size()==descriptor.jobs.size()&&
        built.request->original_threshold_trials==descriptor.original_threshold_trials&&
        built.request->original_acquisition_threshold==descriptor.original_acquisition_threshold,
        "factory lost full ordinary geometry or threshold charge");
    check(built.request->source_pcm[0].size()==built.request->source_pcm[1].size()&&
        built.work.retained_raw_samples==2*built.request->source_pcm[0].size(),
        "production source alternatives lost common finite FIR support");
    check(built.request->conditioning.prior_evidence_end_sample<=built.request->conditioning.first_raw_sample,
        "factory conditioned on future source/FIR data");
    auto bad=source;bad.channel.phase_noise_degrees_per_sqrt_second=.5;
    check(!conditioned_factory::make_first_bit_request(receiver,std::span(mask.cells).first(original.size()),bad).request,
        "fixed-phase reference silently removed configured phase diffusion");
    auto shifted=source;shifted.frequency_correction_hz=30;
    check(!conditioned_factory::make_first_bit_request(receiver,std::span(mask.cells).first(original.size()),shifted).request,
        "untranslated original descriptor silently accepted an NCO coordinate change");
    modem::detail::ConditionedReferenceDescriptor capped;
    check(!receiver.export_conditioned_reference(capped,1,32768),"descriptor job cap was ignored");
    modem::PatternReceiver finished(c,prepared.search.receiver_strategy_workspace_bytes,prepared.search);
    finished.finish();modem::detail::ConditionedReferenceDescriptor ended;
    check(!finished.export_conditioned_reference(ended,32768,32768),
        "finished-without-PCM owner was called pristine");
    const auto after=receiver.acquisition_state();const auto after_work=receiver.fft_work();
    check(after.observed_samples==before.observed_samples&&after.processing_frontier==before.processing_frontier&&
        after.original_pairs==before.original_pairs&&after.selected_pairs==before.selected_pairs&&
        after_work.threshold_trials==before_work.threshold_trials&&after_work.template_jobs==before_work.template_jobs,
        "metadata export/factory mutated ordinary receiver acquisition state");
    std::cout<<"conditioned reference factory PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
