#include "datapump/simulation_estimate.hpp"
#include "datapump/pattern_receiver.hpp"
#include "datapump/live.hpp"
#include "datapump/helper_carrier.hpp"
#include "datapump/tuning.hpp"
#include "fhss_profile.hpp"
#include "live_helper_prior.hpp"
#include "live_receiver_admission.hpp"
#include "receiver_oscillator_policy.hpp"
#include <iomanip>
#include <map>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace datapump;
namespace {
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
live::Settings b_settings() {
    live::Settings value;auto& o=value.transfer;o.key.emplace(Bytes(32,0x74));o.search_seconds=3;
    o.dsp_workspace_bytes=value.dsp_workspace_bytes=128*1024*1024;
    o.audio_timing_error_seconds=0; // Match --audio-error0ms before resolving the production hop calendar.
    o.modem=tuning::resolve(360,40,tuning::PatternMode::auto_keystream,true,1500,10,
        modem::OuterDsssVersion::interleaved_v2).config;
    auto& c=o.modem;c.oscillator_search.emplace();auto& p=*c.oscillator_search;
    p.lf={100,.5};p.rf={1,.05};p.rf_shift_hz=3e7;p.margin=3;
    p.reference=modem::OscillatorReference::independent_audio;
    const auto hops=gui::fhss::resolve(gui::fhss::Preference::fcc,c);check(hops.available,"B FHSS plan unavailable");
    modem::FhssPlan plan;plan.channels=hops.channels;plan.dwell_seconds=hops.dwell_seconds;
    for(unsigned i=0;i<plan.channels;++i)plan.shifts[i]=hops.center(i)-c.carrier_hz;
    c.fake_hopping=transfer::transmit_hopping(plan,o);
    value.helper.power_w=15000;value.helper.data_power_w=4;value.helper.ratio_limit_db=90;
    return value;
}
}
int main(){try {
    auto settings=b_settings();const auto& o=settings.transfer;const auto& c=o.modem;
    check(c.receiver_strategy==modem::ReceiverStrategy::automatic&&c.search_arithmetic==modem::SearchArithmetic::default_mode,
        "native work fixture bypassed normal Auto/Default policies");
    // Independently compare the common policy to the actual ordinary
    // ordinary Live admission in BOTH routes. A simulated source has no UTC slewing, but
    // ordinary reception still retains the declared peer steering ambiguity.
    const auto declared=transfer::receiver_oscillator_search(c,o);
    const auto unsteered=modem::oscillator_pattern_search(c);
    check(transfer::effective_clock_sync(o).has_value()&&
        declared.requested_clock_half_width_ppm>unsteered.requested_clock_half_width_ppm&&
        declared.hypotheses!=unsteered.hypotheses,
        "Default FHSS fixture lost its actual ordinary UTC steering domain");
    for(bool simulation:{false,true}) {
        auto route=settings;route.simulation=simulation;
        live::detail::ReceiverAdmissionContext context;context.now=1800000000;context.keyed=true;
        context.capacity=live::receiver_workspace_capacity(route);
        context.control_margin=live::receiver_control_margin(c,true);
        const auto actual=live::detail::prepare_receiver_search(c,route,1800000000,context);
        check(actual.search.hypotheses==declared.hypotheses&&
            actual.search.admitted_clock_half_width_ppm==std::optional<double>{declared.requested_clock_half_width_ppm}&&
            actual.search.admitted_clock_cell_half_width_ppm==std::optional<double>{declared.clock_step_ppm/2},
            "Planner original policy differs from actual ordinary admission RF/LF/UTC domain");
    }
    std::cout<<std::setprecision(17)<<"ordinary_policy unsteered_pairs="<<unsteered.hypotheses.size()
        <<" unsteered_clock_half_ppm="<<unsteered.requested_clock_half_width_ppm
        <<" actual_pairs="<<declared.hypotheses.size()
        <<" actual_clock_half_ppm="<<declared.requested_clock_half_width_ppm<<'\n';
    modem::ChannelConfig ch;ch.clock_error_ppm=100;ch.frequency_offset_hz=30;
    ch.phase_noise_degrees_per_sqrt_second=std::hypot(.5,.05);ch.snr_db=101.44727494896694;
    const auto tx=transfer::estimate_binary(std::array<std::uint8_t,1>{1},o);
    const auto plan=helper::plan(settings.helper,helper::placement_band(c),120000000,0,modem::outer_dsss_transmit_gain(c));
    simulation::HelperFrequencyForecastRequest request;request.physical_sample_rate=c.sample_rate;
    request.physical_passband_upper_hz=std::nextafter(c.sample_rate/2.,0.);request.helper_frequency_hz=plan.frequency_hz;
    request.expected_usable_helper_cn0_db_hz=30+10*std::log10(15000.)-60+164;
    const auto forecast=simulation::helper_frequency_forecast(tx,o,ch,request,{},simulation::ReceiverWorkMode::hardware_fallback,{},&settings);
    if(!forecast.scenario) {
        const auto original=modem::oscillator_pattern_search(c,modem::utc_transmit_rate_limit(c));
        const auto radius=modem::native_pattern_frequency_cell_radius(c);
        std::map<double,std::vector<double>> lanes;
        for(const auto& pair:original.hypotheses)lanes[pair.clock_error_ppm].push_back(pair.frequency_offset_hz);
        double max_rf_gap=0,max_lf_gap=0,previous_clock=-original.requested_clock_half_width_ppm;
        for(auto& [clock,frequencies]:lanes) {
            const auto cell=live::detail::helper_prior_detail::inner_clock(clock,original.clock_step_ppm/2,
                original.requested_clock_half_width_ppm);
            max_lf_gap=std::max(max_lf_gap,cell.lower-previous_clock);previous_clock=std::max(previous_clock,cell.upper);
            std::sort(frequencies.begin(),frequencies.end());
            for(std::size_t i=1;i<frequencies.size();++i) {
                const auto a=live::detail::helper_prior_detail::inner_cell(frequencies[i-1],0,radius);
                const auto b=live::detail::helper_prior_detail::inner_cell(frequencies[i],0,radius);
                max_rf_gap=std::max(max_rf_gap,b.lower-a.upper);
            }
        }
        // Test the complete ORIGINAL catalogue, independently of RF mask
        // selection. If even this union has a gap, no subset can certify it.
        std::vector<double> endpoints{-original.requested_clock_half_width_ppm,original.requested_clock_half_width_ppm};
        for(const auto& [clock,unused]:lanes) {
            (void)unused;
            const auto cell=live::detail::helper_prior_detail::inner_clock(clock,original.clock_step_ppm/2,
                original.requested_clock_half_width_ppm);
            for(const auto edge:{cell.lower,cell.upper})if(edge>endpoints.front()&&
                edge<original.requested_clock_half_width_ppm)endpoints.push_back(edge);
        }
        std::sort(endpoints.begin(),endpoints.end());endpoints.erase(std::unique(endpoints.begin(),endpoints.end()),endpoints.end());
        bool reported=false;
        for(std::size_t i=1;i<endpoints.size()&&!reported;++i) {
            const auto lo=endpoints[i-1],hi=endpoints[i];
            const auto required=live::detail::helper_prior_detail::strip(
                request.helper_frequency_hz*ch.clock_error_ppm*1e-6+ch.frequency_offset_hz,
                forecast.requirements.frequency_radius_hz,c.carrier_hz-request.helper_frequency_hz,lo,hi);
            std::vector<live::detail::helper_prior_detail::Interval> cells;
            for(const auto& pair:original.hypotheses) {
                const auto clock=live::detail::helper_prior_detail::inner_clock(pair.clock_error_ppm,
                    original.clock_step_ppm/2,original.requested_clock_half_width_ppm);
                if(clock.lower<=lo&&clock.upper>=hi)cells.push_back(
                    live::detail::helper_prior_detail::inner_cell(pair.frequency_offset_hz,0,radius));
            }
            std::sort(cells.begin(),cells.end(),[](auto a,auto b){return a.lower<b.lower;});
            auto frontier=required.lower;
            for(const auto cell:cells) {
                if(cell.upper<frontier)continue;
                if(cell.lower>frontier) {
                    std::cerr<<std::setprecision(17)<<"original_strip_gap clocks="<<lo<<":"<<hi
                        <<" required="<<required.lower<<":"<<required.upper<<" frontier="<<frontier
                        <<" next_lower="<<cell.lower<<" gap="<<cell.lower-frontier<<'\n';
                    reported=true;break;
                }
                frontier=std::max(frontier,cell.upper);if(frontier>=required.upper)break;
            }
            if(!reported&&frontier<required.upper) {
                std::cerr<<std::setprecision(17)<<"original_strip_uncovered clocks="<<lo<<":"<<hi
                    <<" required="<<required.lower<<":"<<required.upper<<" frontier="<<frontier<<'\n';
                reported=true;
            }
        }
        std::cerr<<std::setprecision(17)<<"forecast_coverage_diagnostic helper="<<request.helper_frequency_hz
            <<" observed_offset="<<request.helper_frequency_hz*ch.clock_error_ppm*1e-6+ch.frequency_offset_hz
            <<" ci="<<forecast.requirements.frequency_radius_hz<<" nominal_halfcell="
            <<.125*c.sample_rate/static_cast<double>(modem::symbol_sample_count(c))
            <<" actual_halfcell="<<radius<<" RFstep="<<original.frequency.step_hz
            <<" A="<<original.requested_clock_half_width_ppm<<" beta="<<original.clock_step_ppm/2
            <<" pairs="<<original.hypotheses.size()<<" LF="<<lanes.size()
            <<" max_inward_RF_gap="<<max_rf_gap<<" max_inward_LF_gap="<<max_lf_gap<<'\n';
    }
    check(forecast.scenario.has_value(),forecast.reason.c_str());
    for(const auto& cell:forecast.scenario->profiles[0].cells)
        check(cell.frequency_half_width_hz==modem::native_pattern_frequency_cell_radius(c),
            "B forecast used a generated-grid cell instead of actual native authority");
    const auto work=simulation::helper_native_work(o,*forecast.scenario,simulation::ReceiverWorkMode::hardware_fallback,&settings);
    if(!work.available)std::cerr<<"native_work_decline stage=hardware work="<<work.available
        <<" reason="<<work.reason<<" first_bit="<<work.first_bit.available
        <<" first_bit_reason="<<work.first_bit.reason<<'\n';
    check(work.available,work.reason.c_str());
    check(work.original_jobs>work.conditional_jobs&&work.retained_clock_rates>1&&work.original_starts>1,
        "conditional work omitted real reduction or unresolved LF/arrival alternatives");
    check(work.original_trial_charge==work.original_jobs*work.original_starts&&work.original_batch_end_threshold>0,
        "native work did not retain full original trial charge");
    check(work.probe_workspace_bytes<=32*1024*1024,"optional native metadata constructor exceeded bounded probe grant");
    for(unsigned changed=0;changed<4;++changed) {
        auto mismatch=*forecast.scenario;auto& g=mismatch.profiles[0].actual_geometry;
        if(changed==0)g.minimum_rate=std::nextafter(g.minimum_rate,0.);
        if(changed==1)g.maximum_rate=std::nextafter(g.maximum_rate,2.);
        if(changed==2)g.drift_supported=true;
        if(changed==3)g.differential_supported=true;
        check(!simulation::helper_native_work(o,mismatch,simulation::ReceiverWorkMode::hardware_fallback,&settings).available,
            "work curve accepted a different original clock/detector geometry");
    }
    auto shifted=*forecast.scenario;shifted.profiles[0].input.frequency_correction_hz=30;
    check(!simulation::helper_native_work(o,shifted,simulation::ReceiverWorkMode::hardware_fallback,&settings).available,
        "work curve invented translated ordinary geometry");
    simulation::HelperFirstBitConditions missing;missing.channel=ch;
    const auto held=simulation::helper_native_work(o,*forecast.scenario,simulation::ReceiverWorkMode::hardware_fallback,&settings,{},&missing);
    check(!held.first_bit.available&&held.first_bit.reason.find("independent")!=std::string::npos,
        "finite outcome silently assumed an independent reference");
    auto fixed=settings;fixed.simulation=true;fixed.transfer.modem.search_arithmetic=modem::SearchArithmetic::fp64;
    fixed.transfer.modem.oscillator_search->lf.phase_noise_degrees_per_sqrt_second=0;
    fixed.transfer.modem.oscillator_search->rf.phase_noise_degrees_per_sqrt_second=0;
    auto fixed_channel=ch;fixed_channel.phase_noise_degrees_per_sqrt_second=0;
    const auto fixed_tx=transfer::estimate_binary(std::array<std::uint8_t,1>{1},fixed.transfer);
    const auto fixed_forecast=simulation::helper_frequency_forecast(fixed_tx,fixed.transfer,fixed_channel,request,{},
        simulation::ReceiverWorkMode::sampled_simulation,{},&fixed);
    if(!fixed_forecast.scenario)std::cerr<<"native_work_decline stage=sampled_forecast reason="
        <<fixed_forecast.reason<<'\n';
    check(fixed_forecast.scenario.has_value(),fixed_forecast.reason.c_str());
    simulation::HelperFirstBitConditions finite;finite.channel=fixed_channel;
    finite.independent_reference_before_observation=true;finite.trials=256;
    const auto outcome=simulation::helper_native_work(fixed.transfer,*fixed_forecast.scenario,
        simulation::ReceiverWorkMode::sampled_simulation,&fixed,{},&finite);
    if(!outcome.first_bit.available)std::cerr<<"native_work_decline stage=sampled_first_bit work="
        <<outcome.available<<" reason="<<outcome.reason<<" first_bit="<<outcome.first_bit.available
        <<" first_bit_reason="<<outcome.first_bit.reason<<'\n';
    check(outcome.first_bit.available,outcome.first_bit.reason.c_str());
    check(outcome.first_bit.payload_filter_hz==fixed_forecast.scenario->profiles[0].input.payload_filter_hz&&
        outcome.first_bit.payload_filter_hz==request.helper_frequency_hz,
        "finite outcome replanned a different notch from the scenario being graphed");
    check(outcome.first_bit.trials==256&&outcome.first_bit.original_jobs==outcome.original_jobs&&
        outcome.first_bit.selected_jobs==outcome.conditional_jobs&&outcome.first_bit.retained_clock_rates==outcome.retained_clock_rates,
        "first-bit integration lost actual jobs/LF or original geometry");
    auto converted=*fixed_forecast.scenario;
    converted.profiles[0].input.physical_sample_rate=2*fixed.transfer.modem.sample_rate;
    const auto conversion=simulation::helper_native_work(fixed.transfer,converted,
        simulation::ReceiverWorkMode::sampled_simulation,&fixed,{},&finite);
    check(!conversion.first_bit.available&&conversion.first_bit.reason.find("physical-to-logical")!=std::string::npos,
        "finite outcome invented physical converter response/noise coverage");
    for(unsigned field=0;field<3;++field) {
        auto missing_filter=*fixed_forecast.scenario;
        if(field==0)missing_filter.profiles[0].input.payload_filter_hz.reset();
        if(field==1)missing_filter.profiles[0].input.payload_filter_hz=std::numeric_limits<double>::quiet_NaN();
        if(field==2)missing_filter.profiles[0].input.payload_filter_hz=0;
        const auto rejected_filter=simulation::helper_native_work(fixed.transfer,missing_filter,
            simulation::ReceiverWorkMode::sampled_simulation,&fixed,{},&finite);
        check(rejected_filter.available&&!rejected_filter.first_bit.available&&!rejected_filter.first_bit.reason.empty(),
            "invalid finite filter metadata invented a probability or discarded independent work geometry");
    }
    finite.channel.snr_db=301;
    const auto extreme=simulation::helper_native_work(fixed.transfer,*fixed_forecast.scenario,
        simulation::ReceiverWorkMode::sampled_simulation,&fixed,{},&finite);
    check(!extreme.first_bit.available&&extreme.first_bit.reason.find("sample-SNR")!=std::string::npos,
        "finite outcome admitted an out-of-range payload SNR");
    finite.channel.snr_db=fixed_channel.snr_db;
    finite.channel.phase_noise_degrees_per_sqrt_second=.5;
    auto stale_settings=fixed;stale_settings.helper.data_power_w*=100;
    const auto moving=simulation::helper_native_work(fixed.transfer,*fixed_forecast.scenario,
        simulation::ReceiverWorkMode::sampled_simulation,&stale_settings,{},&finite);
    check(moving.first_bit.payload_filter_hz==fixed_forecast.scenario->profiles[0].input.payload_filter_hz,
        "changed receiver-settings power silently selected another finite-reference notch");
    check(!moving.first_bit.available&&moving.first_bit.reason.find("phase")!=std::string::npos,
        "finite outcome silently removed configured phase diffusion");
    check(o.modem.receiver_strategy==modem::ReceiverStrategy::automatic&&o.modem.search_arithmetic==modem::SearchArithmetic::default_mode,
        "work query changed user backend policies");
    std::cout<<"helper native work curve PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
