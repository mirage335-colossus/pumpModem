#include "datapump/pattern_correlator.hpp"
#include "datapump/receiver_strategy.hpp"
#include "datapump/live.hpp"
#include "datapump/scheduled_complex_search.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/symbol_schedule.hpp"
#include "shared_scheduled_capture.hpp"
#include "scheduled_diagnostic_journal.hpp"
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <limits>
using namespace datapump;using namespace datapump::modem;
namespace shared=datapump::modem::detail::scheduled_capture;
#define CHECK(c) do{if(!(c))throw std::runtime_error("check failed: " #c);}while(false)
int main(){try {
    const auto* gate=std::getenv("DATAPUMP_SCHED_LAZY_ACQUISITION");CHECK(gate && std::string_view(gate)=="1");
    Config c;c.sample_rate=6120;c.carrier_hz=1500;c.bandwidth_hz=36;c.spreading_factor=256;
    c.scramble=true;c.dsss_factor=10;c.outer_dsss_version=OuterDsssVersion::interleaved_v2;
    c.data_key.emplace(Bytes(32,0x37));c.scheduled_hopping=std::make_shared<SchedHopPlan>(SchedHopPlan{10,1});
    c.oscillator_search.emplace();c.oscillator_search->lf={.0001,.5};c.oscillator_search->rf={.0001,.5};
    c.oscillator_search->rf_shift_hz=30000000;c.oscillator_search->margin=3;c=scheduled_estimate_config(c);
    PatternSearch s;s.start_offset_seconds=0;s.start_uncertainty_seconds=7;s.search_stream_phases=true;
    s.receiver_bank_count=receiver_strategy_bank_count(c,6,1,1);s.receiver_strategy_workspace_bytes=7899971;
    const auto old=oscillator_pattern_search(c);s.hypotheses=old.hypotheses;
    s.admitted_frequency_half_width_hz=old.frequency.requested_half_width_hz;s.admitted_clock_half_width_ppm=old.requested_clock_half_width_ppm;
    s.complex_frequency_coverage=!old.limited;s.complex_clock_coverage=!old.limited;
    const auto refined=scheduled_complex_search(c,s);CHECK(refined.complete_coverage);s=refined.search;
    const auto g=pattern_scheduled_complex_geometry(c,s);CHECK(g.supported && g.retained_hypotheses>0);
    constexpr std::size_t workspace=7899971,cohort=809520800,consumers=314;
    const auto required=pattern_scheduled_acquisition_requirements(c,s,g,workspace);
    CHECK(required.geometry_supported && required.required_history_samples>g.physical_aperture_samples);
    CHECK(required.local_capture_bytes>required.capture_fence_bytes && required.per_bank_required_bytes>0);
    CHECK(required.slot_unit_bytes && required.minimum_slot_admission_bytes==10*required.slot_unit_bytes);
    CHECK(required.serial_execution_bytes>0);
    CHECK(required.diagnostic_journal_bytes==detail::scheduled_diagnostics::DeferredEvents::required_bytes(1024));
    CHECK(!s.scheduled_capture);
    const auto projected=pattern_scheduled_acquisition_memory_projection(c,s,g,workspace,cohort,consumers);
    CHECK(projected.projected_fits && !s.scheduled_capture);
    CHECK(projected.shared_capture_bytes==shared::SharedScheduledCapture::cohort_layout(cohort,consumers).reserved_bytes && projected.logical_lanes==g.retained_hypotheses);
    const auto execution_without_owner=pattern_scheduled_acquisition_policy(c,s,g,workspace);
    CHECK(!execution_without_owner.shared_capture && !execution_without_owner.supported);
    const auto expected=plan_receiver_strategy(c,s,64*1024*1024,cohort,consumers,live::receiver_control_margin(c,true));
    CHECK(expected.supported && expected.backend==ReceiverBackend::existing_complex && expected.scheduled_implicit_acquisition);
    CHECK(!std::isfinite(expected.selected_processing_ratio));CHECK(!s.scheduled_capture);
    auto constrained=s;constrained.receiver_strategy_workspace_bytes=1;
    const auto constrained_result=plan_receiver_strategy(c,constrained,64*1024*1024,cohort,consumers,live::receiver_control_margin(c,true));
    CHECK(!constrained_result.supported && constrained.receiver_strategy_workspace_bytes==1);
    CHECK(!constrained.scheduled_capture && constrained.hypotheses==s.hypotheses);
    // A real allocated owner permits actual policy; projection alone never did.
    const auto layout=shared::SharedScheduledCapture::cohort_layout(cohort,consumers);
    s.scheduled_capture=std::make_shared<shared::SharedScheduledCapture>(layout.limits,shared::Identity{1,1},0);
    const auto actual=pattern_scheduled_acquisition_policy(c,s,g,workspace);
    CHECK(actual.supported && actual.shared_capture && actual.shared_capture_bytes==projected.shared_capture_bytes);
    const auto too_small=pattern_scheduled_acquisition_memory_projection(c,s,g,required.per_bank_required_bytes,cohort,consumers);
    CHECK(!too_small.projected_fits);
    const auto wrong_capacity=pattern_scheduled_acquisition_memory_projection(c,s,g,workspace,2*1024*1024,consumers);
    CHECK(!wrong_capacity.projected_fits);
    auto invalid=s;invalid.candidate_limit=std::numeric_limits<std::size_t>::max();
    CHECK(!pattern_scheduled_acquisition_requirements(c,invalid,g,workspace).geometry_supported);
    invalid=s;invalid.track_limit=std::numeric_limits<std::size_t>::max();
    CHECK(!pattern_scheduled_acquisition_requirements(c,invalid,g,workspace).geometry_supported);
    invalid=s;invalid.bit_limit=0;CHECK(!pattern_scheduled_acquisition_requirements(c,invalid,g,workspace).geometry_supported);
    invalid=s;invalid.chunk_bits=0;CHECK(!pattern_scheduled_acquisition_requirements(c,invalid,g,workspace).geometry_supported);
    invalid=s;invalid.false_alarm_probability=0;CHECK(!pattern_scheduled_acquisition_requirements(c,invalid,g,workspace).geometry_supported);
    invalid=s;invalid.start_offset_seconds.reset();CHECK(!pattern_scheduled_acquisition_requirements(c,invalid,g,workspace).geometry_supported);
    std::cout<<"bank="<<required.per_bank_required_bytes<<" fences="<<required.capture_fence_bytes
             <<" local="<<required.local_capture_bytes<<" shared="<<projected.shared_capture_bytes
             <<" logical="<<projected.logical_lanes<<" CPU estimate unavailable\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
