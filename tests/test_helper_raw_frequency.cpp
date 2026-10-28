#include "live_helper_raw_frequency.hpp"
#include "live_helper_constructor_radius.hpp"
#include "gui/fhss_profile.hpp"
#include "datapump/tuning.hpp"
#include <iostream>
#include <set>
using namespace datapump;
using namespace datapump::live::detail;
void require(bool value,const char* reason){if(!value)throw Error(reason);}
int main()try {
    auto c=tuning::resolve(360,40,tuning::PatternMode::auto_keystream,true,1500,10,
        modem::OuterDsssVersion::interleaved_v2).config;
    c.oscillator_search=modem::OscillatorSearchConfig{{100,.5},{1,.05},3e7,3};
    const auto hops=gui::fhss::resolve(gui::fhss::Preference::fcc,c);
    require(hops.available,"supplied Fake-FHSS profile unavailable");
    modem::FhssPlan hopping;hopping.channels=hops.channels;hopping.dwell_seconds=hops.dwell_seconds;
    for(unsigned i=0;i<hopping.channels;++i)hopping.shifts[i]=hops.center(i)-c.carrier_hz;
    transfer::Options options;options.modem=c;options.clock_sync=clock_sync::parse("default");
    options.audio_timing_error_seconds=.07;c.fake_hopping=transfer::transmit_hopping(hopping,options);options.modem=c;
    helper::Options helper_options;helper_options.power_w=1500;helper_options.data_power_w=4;
    const auto H=helper::plan(helper_options,helper::placement_band(c),120000000,0,
        modem::outer_dsss_transmit_gain(c)).frequency_hz;
    const auto rate=4*c.sample_rate;
    const auto utc=transfer::receiver_utc_rate_correction(c,options);
    // Numeric provenance/geometry control, NOT an acquisition experiment.
    // These retained measured values never seed a tracker or ordinary receiver.
    constexpr double measured_M=30.53,U=.0157;
    helper::Reference ref;ref.sample_rate=rate;ref.frequency_valid=true;
    ref.frequency_hz=H+measured_M;ref.frequency_uncertainty_hz=U;
    const auto represented_M=ref.frequency_hz-H;
    HelperInputFrontend frontend(HelperInputFrontend::reservation_bound(rate,c.sample_rate,H),91);
    frontend.admit_realtime(c,H,ref,rate,rate/2.-10,0,utc);
    require(frontend.active() && frontend.reference_passthrough() && !frontend.original_prior_retained() &&
        frontend.current_correction_hz()==0,"supplied folded q0 basis changed");
    std::vector<float> input(rate/50);
    for(std::uint64_t first=0;first<2*rate;first+=input.size())frontend.push(input,first);
    auto blocks=frontend.take(UINT64_MAX,ref,2*rate);
    require(!blocks.empty(),"raw folded frontend produced no ordinary PCM");
    auto& block=blocks.back();const auto support=helper_raw_frequency_support(block);
    require(support && (*support)[0]+helper::AnalyticFrequencyTranslator::group_delay==block.corrected_dependency_first &&
        (*support)[1]+helper::AnalyticFrequencyTranslator::group_delay==block.corrected_dependency_end,
        "known q0 delay lost complete raw dependency dates");
    helper::ToneFrequencyInterval measured;measured.valid=true;measured.frequency_hz=ref.frequency_hz;measured.uncertainty_hz=U;
    measured.first_sample=(*support)[0];measured.end_sample=(*support)[1];
    measured.evidence_first_sample=measured.first_sample;measured.evidence_end_sample=measured.end_sample;
    measured.captured_end_sample=block.raw_captured_end;
    block.frequency_interval=helper_raw_frequency_interval(block,measured);
    const auto raw=helper_raw_frequency_m_bound(block,H,0);
    require(raw && (*raw)[0]<=represented_M-U && (*raw)[1]>=represented_M+U && (*raw)[0]>30 &&
        block.applied_correction_hz.lower_hz==0 && block.applied_correction_hz.upper_hz==0 &&
        !block.corrected_rf_uncertainty_hz && !block.common_phase_corrected,
        "raw M was zeroed, falsely corrected or given phase authority");
    HelperScoreMandatoryHistory history;require(history.configure(c.sample_rate),"raw history configure failed");
    const HelperFrequencyDomainStamp stamp{91,2};history.observe_authority(stamp);
    require(history.append({block.first_sample,block.first_sample+block.samples.size(),(*raw)[0],(*raw)[1],stamp,true}),
        "raw mandatory certificate append failed");
    const auto hull=history.query(block.first_sample,block.first_sample+block.samples.size());
    require(hull.valid && hull.lower_hz==(*raw)[0] && hull.upper_hz==(*raw)[1],"raw M did not survive mandatory history");
    const auto interval=helper_score_center_radius(hull.lower_hz,hull.upper_hz);require(bool(interval),"raw M interval invalid");
    auto actual=modem::helper_reference_search(c,helper_score_constructor_radius(c,(*interval)[1]),H,(*interval)[0],utc,
        modem::HelperReferenceClockRelation::common_corrected_source,false,modem::PatternUtcClockGrid::complete_expanded_domain);
    const auto full_clock=actual.catalogue.requested_clock_half_width_ppm;
    require(!actual.original_prior_retained && actual.residual_center_hz>30 && full_clock>300,
        "raw constructor lost its nonzero origin or original LF/UTC domain");
    modem::PatternSearch search;modem::apply_helper_reference_search(std::move(actual),search);
    const HelperScoreFrequencyBasis basis{stamp,c.sample_rate,H,c.carrier_hz,utc,c.oscillator_search,false};
    const auto lease=HelperScoreFrequencyLease::freeze(basis,(*interval)[0],helper_score_constructor_radius(c,(*interval)[1]),search,false);
    require(lease.valid && lease.contains(hull,basis),"raw constructor did not contain its actual M hull");
    modem::detail::PatternHelperFrequencySelector selector;
    require(search.admitted_clock_cell_half_width_ppm && selector.initialize(search,
        {(*interval)[0],(*interval)[1],H-c.carrier_hz,*search.admitted_clock_cell_half_width_ppm,{}}) && selector.selected &&
        selector.original_clock_rates>1 && selector.selected_clock_rates==selector.original_clock_rates,
        "raw frequency selector removed unresolved LF alternatives");
    auto outside=measured;--outside.first_sample;
    require(!helper_raw_frequency_interval(block,outside),"partial raw frequency support certified a whole block");
    outside=measured;outside.evidence_end_sample=block.raw_captured_end+1;
    require(!helper_raw_frequency_interval(block,outside),"future frequency evidence acquired authority");
    require(!helper_raw_frequency_m_bound(block,H,(*support)[0]+1),"pre-veto dependency acquired new authority");
    block.data_parameter_domain=true;require(!helper_raw_frequency_support(block),"raw q0 path invented actual-RF mapping");
    block.data_parameter_domain=false;block.applied_correction_hz.upper_hz=.1;
    require(!helper_raw_frequency_support(block),"nonzero applied q was relabelled q0");
    std::cout<<"raw_q0_component M_lower="<<(*raw)[0]<<" M_upper="<<(*raw)[1]<<" q=0 phase=0 original_LF="
        <<selector.original_clock_rates<<" selected_LF="<<selector.selected_clock_rates
        <<" raw_first="<<(*support)[0]<<" raw_end="<<(*support)[1]<<" no_acquisition_or_payload_claim=1\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
