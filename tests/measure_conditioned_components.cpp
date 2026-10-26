#include "conditioned_reference_factory.hpp"
#include "live_receiver_admission.hpp"
#include "live_helper_prior.hpp"
#include "fhss_profile.hpp"
#include "datapump/tuning.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <ctime>
#include <iomanip>
#include <set>
#include <stdexcept>
using namespace datapump;
namespace {
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
live::Settings b_settings() {
    live::Settings value;auto& o=value.transfer;
    o.key.emplace(Bytes(32,0x74));o.search_seconds=3;o.dsp_workspace_bytes=128*1024*1024;
    value.dsp_workspace_bytes=o.dsp_workspace_bytes;
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
    const auto cpu=[](){return static_cast<double>(std::clock())/CLOCKS_PER_SEC;};
    const auto begin_constructor=cpu();
    modem::PatternReceiver receiver(c,std::min<std::size_t>(prepared.search.receiver_strategy_workspace_bytes,32*1024*1024),prepared.search);
    const auto constructor_cpu=cpu()-begin_constructor;
    const auto before=receiver.acquisition_state();const auto before_work=receiver.fft_work();
    modem::detail::ConditionedReferenceDescriptor descriptor;
    check(receiver.export_conditioned_reference(descriptor,32768,32768),descriptor.reason.c_str());
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
    const auto begin_factory=cpu();
    const auto built=conditioned_factory::make_first_bit_request(receiver,
        std::span(mask.cells).first(original.size()),source);
    const auto factory_cpu=cpu()-begin_factory;
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
    double conditioning_cpu=0,scoring_cpu=0;std::size_t iterations=0,score_cells=0;
    const auto begin=cpu();
    for(;iterations<16&&cpu()-begin<2.;++iterations) {
        auto before=cpu();
        const auto observations=conditioned_private::condition_observations(*built.request,built.request->source_pcm[0]);
        conditioning_cpu+=cpu()-before;before=cpu();
        const auto scores=conditioned_private::score_observations(*built.request,observations);
        scoring_cpu+=cpu()-before;score_cells+=scores.size();
    }
    check(iterations>0,"component measurement performed no iterations");
    std::cout<<std::setprecision(17)
        <<"scope=isolated_known_onset_native_B_FP64_primitives,host=current_unscaled,physical_sample_rate="<<c.sample_rate
        <<",starts="<<built.request->starts<<",bin_samples="<<descriptor.geometry.bin_samples
        <<",bins="<<descriptor.geometry.bins_per_symbol<<",original_jobs="<<built.work.original_jobs
        <<",selected_jobs="<<built.work.selected_jobs<<",original_trial_charge="<<descriptor.original_trial_charge
        <<",raw_samples="<<built.request->source_pcm[0].size()<<",iterations="<<iterations
        <<",score_cells="<<score_cells<<",constructor_cpu="<<constructor_cpu<<",factory_cpu="<<factory_cpu
        <<",conditioning_cpu="<<conditioning_cpu<<",scoring_cpu="<<scoring_cpu
        <<",helper_detector_cpu_included=0,coarse_cpu_included=0,translator_resampler_cpu_included=0"
        <<",probe_cpu_included=0,whole_receiver_cpu_included=0,no_cpu_gain_claim=1\n";
    std::cout<<"conditioned primitive measurement PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
