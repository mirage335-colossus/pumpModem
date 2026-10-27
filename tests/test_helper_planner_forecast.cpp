#include "link_planner_model.hpp"
#include "link_planner_page.hpp"
#include "datapump/helper_carrier.hpp"
#include "../src/receiver_oscillator_policy.hpp"
#include "../src/helper_reference_search.hpp"
#include "../src/live_helper.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <set>
using namespace datapump;
namespace {
void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
bool contains(const gui::ui::DocumentNode& node,const char* text) {
    if(node.text.find(text)!=std::string::npos)return true;
    return std::any_of(node.children.begin(),node.children.end(),[&](const auto& child){return contains(child,text);});
}

}
int main(){try{
    // The ideal reference is separate from every practical forecast and lock
    // decision. Preserve the requested 1 s/10 s noise-only scales and T^-3/2.
    for(const auto cn0:{40.,30.,20.,10.,6.,3.,0.,-3.}) {
        const auto one=gui::planner::helper_ideal_tone_half_width(cn0,1);
        const auto ten=gui::planner::helper_ideal_tone_half_width(cn0,10);
        const auto expected=3*std::sqrt(3/(2*std::numbers::pi*std::numbers::pi*std::pow(10.,cn0/10)));
        check(one&&ten&&std::abs(*one-expected)<1e-14*expected&&
            std::abs(*ten-*one/std::pow(10.,1.5))<1e-14*(*ten),
            "ideal helper precision reference lost C/N0 or physical observation scaling");
    }
    for(const auto duration:{0.,-1.,std::numeric_limits<double>::infinity()})
        check(!gui::planner::helper_ideal_tone_half_width(30,duration),
            "ideal helper precision accepted an invalid observation duration");
    check(!gui::planner::helper_ideal_tone_half_width(std::numeric_limits<double>::quiet_NaN(),1),
        "ideal helper precision accepted a nonfinite C/N0");
    // A backend can expose finite partial prices while explicitly declining
    // complete component support (for example V2 compact template rebuilds).
    simulation::Estimate partial;
    partial.receiver_work_supported=true;partial.receiver_workspace_supported=false;
    partial.receiver_cpu_seconds=.13;partial.simulated_seconds=1;
    partial.receiver_frontend_seconds=.02;partial.receiver_search_seconds=.03;
    partial.receiver_kernel_rebuild_seconds=.01;partial.tracking_seconds=.02;partial.payload_processing_seconds=.02;
    partial.hardware_sustained_modeled=true;partial.hardware_sustained_realtime_ratio=.2;
    partial.hardware_sustained_tracking_realtime_ratio=.05;
    const auto supported_ledger=gui::planner::helper_receiver_cost(partial);
    check(supported_ledger.finite_available&&supported_ledger.sustained_available&&
        !supported_ledger.workspace_supported,
        "receiver work pricing conflated its independent workspace eligibility");
    partial.receiver_work_supported=false;
    const auto unsupported_ledger=gui::planner::helper_receiver_cost(partial);
    check(!unsupported_ledger.finite_available&&!unsupported_ledger.sustained_available&&
        unsupported_ledger.finite_cpu_seconds==partial.receiver_cpu_seconds,
        "finite partial component prices were promoted to complete conditional CPU totals");
    // Weak but otherwise valid predictions select bounded airtime without
    // presenting zero as a measured or predicted frequency uncertainty.
    helper::TonePrecisionRequest weak_request;weak_request.sample_rate=48000;
    weak_request.straight_cn0_db_hz=13;
    const auto weak_cap=helper::predict_tone_precision(weak_request);
    check(!weak_cap.supported&&!weak_cap.target_reached&&weak_cap.extension_capped&&
        weak_cap.extra_lead_seconds==36&&weak_cap.total_lead_seconds==38&&
        weak_cap.frequency_half_width_hz==0&&weak_cap.expected_segments==0&&
        weak_cap.reason.find("precision not predicted")!=std::string::npos,
        "valid weak-tone forecast dropped the extra-lead cap or invented precision");
    auto short_request=weak_request;short_request.straight_cn0_db_hz=40;
    short_request.baseline_lead_seconds=0;short_request.max_extra_seconds=.8;
    const auto short_cap=helper::predict_tone_precision(short_request);
    check(!short_cap.supported&&!short_cap.target_reached&&short_cap.extension_capped&&
        short_cap.extra_lead_seconds==.8&&short_cap.total_lead_seconds==.8&&
        short_cap.frequency_half_width_hz==0&&short_cap.expected_segments==0,
        "insufficient clean segments dropped the configured whole-dwell airtime cap");
    auto invalid_request=weak_request;invalid_request.sample_rate=0;
    const auto invalid_cap=helper::predict_tone_precision(invalid_request);
    check(!invalid_cap.supported&&!invalid_cap.extension_capped&&invalid_cap.extra_lead_seconds==0,
        "invalid forecast geometry added optional airtime");
    invalid_request=weak_request;invalid_request.baseline_lead_seconds=301;
    const auto unbounded_cap=helper::predict_tone_precision(invalid_request);
    check(!unbounded_cap.supported&&!unbounded_cap.extension_capped&&unbounded_cap.extra_lead_seconds==0,
        "out-of-domain weak forecast bypassed the bounded source/capture guard");
    gui::planner::ReceivePoint ordinary;ordinary.target_db_hz=40;ordinary.success_probability=.73;
    ordinary.reference_available=true;ordinary.clock_supported=ordinary.workspace_supported=true;
    gui::planner::HelperCpuPoint conditional;conditional.target_db_hz=40;
    conditional.scenario_available=conditional.backend_sustained_available=true;
    conditional.backend_sustained_realtime_ratio=.2;
    const auto capacity=gui::planner::helper_backend_feasibility(ordinary,conditional,.6);
    check(capacity.ordinary_phy_available&&capacity.backend_feasibility_known&&capacity.backend_fits_budget&&
        capacity.ordinary_success_probability==ordinary.success_probability,
        "backend capacity changed ordinary PHY probability or required finite FP64/zero-phase reference coverage");
    const auto busy=gui::planner::helper_backend_feasibility(ordinary,conditional,.1);
    check(busy.backend_feasibility_known&&!busy.backend_fits_budget&&busy.ordinary_phy_available&&
        busy.ordinary_success_probability==ordinary.success_probability,"backend overload was presented as zero physical reception");
    auto unsupported=conditional;unsupported.scenario_available=false;
    check(!gui::planner::helper_backend_feasibility(ordinary,unsupported,.6).backend_feasibility_known,
        "rejected scenario manufactured backend feasibility");
    auto missing_phy=ordinary;missing_phy.reference_available=false;
    const auto missing_phy_capacity=gui::planner::helper_backend_feasibility(missing_phy,conditional,.6);
    check(!missing_phy_capacity.ordinary_phy_available&&missing_phy_capacity.backend_feasibility_known,"backend capacity manufactured a PHY probability");

    gui::planner::Inputs in;in.mode=tuning::PatternMode::auto_pattern;
    in.target_db_hz=40;in.tx_dbm=36.020599913279625;in.path_loss_db=60;in.noise_density_dbm_hz=-164;
    auto& c=in.options.modem;c.bandwidth_hz=36;c.sample_rate=6000;c.carrier_hz=1500;
    c.receiver_strategy=modem::ReceiverStrategy::complex_only;c.oscillator_search.emplace();
    c.oscillator_search->lf={.05,0};c.oscillator_search->rf={1,0};c.oscillator_search->rf_shift_hz=30000000;
    in.channel.clock_error_ppm=.05;in.channel.frequency_offset_hz=30;in.channel.phase_noise_degrees_per_sqrt_second=0;
    in.receiver_work_mode=simulation::ReceiverWorkMode::hardware_fallback;
    in.options.dsp_workspace_bytes=256*1024*1024;in.options.search_seconds=3;
    in.receiver_settings.emplace();in.receiver_settings->transfer=in.options;
    in.receiver_settings->dsp_workspace_bytes=in.options.dsp_workspace_bytes;
    in.receiver_settings->helper.power_w=15000;in.receiver_settings->helper.data_power_w=4;
    in.receiver_settings->helper.ratio_limit_db=90;
    // Explicit component capture format, not an inferred hardware negotiation.
    const std::array selected_target{in.target_db_hz};
    const auto capture=tuning::receive_profiles(in.options.modem,selected_target,in.mode,false).front().sample_rate;
    in.helper_forecast_capture_sample_rate=capture;
    in.helper_forecast_capture_passband_hz=std::nextafter(capture/2.,0.);
    in.helper_forecast_fixed_nominal_rate_conversion=true;
    in.helper_forecast_capture_logical_sample_rate=capture;
    const auto enabled=gui::planner::build(in);
    check(enabled.available&&enabled.helper_reference.enabled&&enabled.helper_reference.budget_available,
        "manual helper mode lost its ordinary plan or physical helper link budget");
    check(enabled.helper_cpu_points.empty()&&enabled.helper_receive_points.empty()&&
        enabled.helper_backend_feasibility_points.empty()&&enabled.helper_work_points.empty()&&
        !enabled.helper_reference.conditional_geometry_available,
        "manual helper reference still executed obsolete mask/cohort/history forecasts");
    check(!enabled.helper_reference_cpu_points.empty()&&enabled.helper_reference.conditional.available&&
        enabled.helper_reference.conditional.frequency_rate_hypotheses>0&&
        enabled.helper_reference.conditional.observation_seconds>0,
        "real-time helper did not publish its separate conditional ordinary receiver cost");
    const auto& forecast=enabled.helper_reference.conditional;
    const auto& ideal=forecast.precision_reference;
    check(ideal.ideal_one_second_half_width_hz&&ideal.ideal_lead_half_width_hz&&
        ideal.ideal_lead_observation_seconds==forecast.planned_lead_seconds/ideal.maximum_source_capture_ratio&&
        ideal.ideal_lead_half_width_hz==gui::planner::helper_ideal_tone_half_width(ideal.cn0_db_hz,
            ideal.ideal_lead_observation_seconds),
        "ideal reference used numeric history or usable segments as coherent observation duration");
    check(forecast.precision.supported&&forecast.precision.extra_lead_seconds>=0&&
        forecast.precision.extra_lead_seconds<=36&&forecast.precision.expected_segments>0&&
        forecast.uncertainty_hz==forecast.precision.frequency_half_width_hz&&
        forecast.planned_lead_seconds>=forecast.precision.baseline_lead_seconds&&
        forecast.planned_tail_seconds>=2&&
        (!forecast.precision.target_reached||forecast.uncertainty_hz<=.1),
        "selected point lost the shared lead predictor/cap/achievable-precision contract");
    check(forecast.precision.sustained_supported&&forecast.sustained&&
        forecast.sustained_at_seconds==forecast.precision.sustained_at_seconds&&
        forecast.sustained->uncertainty_hz==forecast.precision.sustained_frequency_half_width_hz&&
        forecast.sustained->rf_half_width_hz==forecast.sustained->uncertainty_hz&&
        forecast.sustained->clock_half_width_ppm==forecast.clock_half_width_ppm&&
        forecast.sustained->retained_clock_rates==forecast.retained_clock_rates&&
        forecast.sustained->frequency_rate_hypotheses>0&&
        // The envelope accumulates long-double dwell time; the predictor
        // sums double baseline/extra time. Preserve the same selected airtime
        // without requiring their final double conversions to be identical.
        std::abs(forecast.planned_lead_seconds-forecast.precision.total_lead_seconds)<=
            4*std::numeric_limits<double>::epsilon()*std::max({1.,
                std::abs(forecast.planned_lead_seconds),std::abs(forecast.precision.total_lead_seconds)}),
        "sustained receiver query lost its own residual U, unresolved clocks or unchanged initial lead");
    const auto& sustained_cost=forecast.sustained->receiver_cost;
    check(!sustained_cost.helper_startup_cpu_seconds&&!sustained_cost.helper_sustained_realtime_ratio&&
        !sustained_cost.post_lock_lead_receiver_cpu_seconds,
        "sustained conditional cost invented zero-priced optional helper overhead");
    const auto& cost=enabled.helper_reference.conditional.receiver_cost;
    check(cost.finite_available&&cost.finite_audio_seconds>0&&
        cost.workspace_supported&&cost.geometry.workspace_bytes>0,
        "conditional receiver omitted its finite/sustained/storage pricing scope");
    const auto sum=cost.frontend_seconds+cost.search_seconds+cost.kernel_seconds+
        cost.continuation_seconds+cost.payload_seconds+cost.startup_other_seconds;
    check(std::abs(sum-cost.finite_cpu_seconds)<=1e-10*std::max(1.,cost.finite_cpu_seconds)&&
        std::abs(cost.finite_realtime_ratio-cost.finite_cpu_seconds/cost.finite_audio_seconds)<=1e-12&&
        enabled.helper_reference.conditional.realtime_ratio==
            std::max(cost.finite_realtime_ratio,cost.sustained_realtime_ratio),
        "conditional finite ledger double-counted nested epoch setup or lost its denominator");
    check(!cost.helper_startup_cpu_seconds&&!cost.helper_sustained_realtime_ratio&&
        !cost.post_lock_lead_receiver_cpu_seconds,
        "missing helper/lead CPU calibration was represented as an available zero cost");
    in.receiver_settings->helper.power_w=0;const auto disabled=gui::planner::build(in);
    check(disabled.cpu_points.size()==enabled.cpu_points.size()&&disabled.receive_points.size()==enabled.receive_points.size(),
        "helper changed ordinary curve target geometry");
    for(std::size_t i=0;i<disabled.cpu_points.size();++i)check(disabled.cpu_points[i].target_db_hz==enabled.cpu_points[i].target_db_hz&&
        disabled.cpu_points[i].realtime_ratio==enabled.cpu_points[i].realtime_ratio,
        "helper forecast altered ordinary CPU baseline");
    for(std::size_t i=0;i<disabled.receive_points.size();++i)check(disabled.receive_points[i].target_db_hz==enabled.receive_points[i].target_db_hz&&
        disabled.receive_points[i].success_probability==enabled.receive_points[i].success_probability,
        "helper forecast altered ordinary RX baseline");
    // Stale historical scenario inputs cannot reactivate the retired helper
    // forecast path; they also cannot change the ordinary payload baseline.
    auto stale=in;stale.receiver_settings->helper.power_w=15000;
    stale.helper_frequency_scenarios.push_back({stale.target_db_hz,{}});
    const auto ignored=gui::planner::build(stale);
    check(ignored.helper_cpu_points.empty()&&ignored.helper_receive_points.empty()&&ignored.helper_work_points.empty()&&
        ignored.cpu_points.size()==enabled.cpu_points.size(),
        "stale helper scenario revived historical forecast computation");
    for(std::size_t i=0;i<enabled.cpu_points.size();++i)
        check(ignored.cpu_points[i].target_db_hz==enabled.cpu_points[i].target_db_hz&&
            ignored.cpu_points[i].realtime_ratio==enabled.cpu_points[i].realtime_ratio,
            "ignored helper scenario changed ordinary CPU baseline");
    const auto page=gui::planner_page::build(enabled,900,true,false);
    for(const auto* required:{"Helper reference on","The strongest tone in the expected helper range",
        "Robust acquisition waits while the helper is unavailable",
        "Disable Helper Carrier to use ordinary oscillator search",
        "Solid: ordinary receiver baseline. Dashed: conditional helper-corrected ordinary receiver cost",
        "not total CPU, measured speed, or a lock/probability forecast",
        "No clock narrowing is inferred from tone power",
        "helper power does not increase payload SNR or Rate",
        "DSP fixed workspace", "Automatic helper precision lead:","Conditional clean-model precision:",
        "Expected helper C/N0:","dB-Hz", "Ideal noise-only 3σ scale:",
        "not an operational confidence interval", "Practical precision inputs:",
        "Sustained conditional clean-model precision:","if the reference remains available until",
        "This adds no helper-only transmit lead", "Short dashes use initial clean-model precision",
        "long dashes use sustained clean-model precision", "Initial conditional ordinary receiver cost:",
        "Static oscillator accuracy in ppm sets initial discovery uncertainty, not a drift rate",
        "tracking-slew guard is an acceptance limit, not a physical drift prediction",
        "Prediction is not measured lock or a lock-probability forecast",
        "Conditional finite receiver ledger:",
        "unknown additive costs, not zero","Peak rolling-bank storage is unqualified"})
        if(!contains(page,required))throw std::runtime_error(std::string("UI omitted manual reference state/limit: ")+required);
    // A declined optional sustained query cannot relabel initial precision or
    // hide initial receiver pricing. It also cannot keep a stale curve sample.
    auto no_sustained=enabled;no_sustained.helper_reference.conditional.sustained.reset();
    for(auto& point:no_sustained.helper_reference_cpu_points)point.sustained.reset();
    const auto no_sustained_page=gui::planner_page::build(no_sustained,900,true,false);
    check(contains(no_sustained_page,"optional work forecast yielded")&&
        contains(no_sustained_page,"Initial conditional ordinary receiver cost:")&&
        !contains(no_sustained_page,"Sustained conditional clean-model precision:"),
        "missing sustained work erased or replaced the initial receiver price");
    auto percentage=enabled;percentage.inputs.dsp_workspace_percent=50;
    const auto percentage_page=gui::planner_page::build(percentage,900,true,false);
    check(contains(percentage_page,"DSP 50% RAM")&&!contains(percentage_page,"DSP fixed workspace"),
        "fixed workspace sentinel changed the ordinary percentage label");
    if(cost.sustained_available)check(contains(page,"Conditional continuous listening model:"),
        "available continuous receiver pricing was not displayed separately");
    for(const auto* obsolete:{"Qualified helper total","Conditional helper 1-bit RX","Conditional acquisition work",
        "One global cold probe slot","Helper loss or inconsistency immediately restores broad future acquisition"})
        check(!contains(page,obsolete),"manual reference UI retained an obsolete mask/probe/fallback claim");
    // Render both explicit outcomes without making a synthetic runtime lock.
    auto capped=enabled;auto& capped_point=capped.helper_reference.conditional;
    capped_point.precision.supported=true;capped_point.precision.target_reached=false;
    capped_point.precision.extension_capped=true;capped_point.precision.extra_lead_seconds=36;
    capped_point.precision.frequency_half_width_hz=.2;capped_point.uncertainty_hz=.2;
    capped_point.precision.total_lead_seconds=capped_point.precision.baseline_lead_seconds+36;
    capped_point.planned_lead_seconds=capped_point.precision.total_lead_seconds;
    const auto capped_page=gui::planner_page::build(capped,900,true,false);
    check(capped_point.available&&contains(capped_page,"36 s extra-lead cap reached; ±0.1 Hz target not reached")&&
        contains(capped_page,"Initial conditional clean-model half-width at payload start: 0.2 Hz"),
        "cap outcome hid the supported achievable residual precision/work");
    auto declined=enabled;auto& declined_point=declined.helper_reference.conditional;
    declined_point.precision.supported=false;declined_point.precision.reason="bounded forecast input unavailable";
    declined_point.available=false;
    check(contains(gui::planner_page::build(declined,900,true,false),
        "Automatic helper precision lead prediction declined: bounded forecast input unavailable")&&
        contains(gui::planner_page::build(declined,900,true,false),"not proof of a physical precision limit"),
        "unsupported precision forecast was displayed as reached or locked");
    // Shared TX/Planner request uses actual metadata length and key precedence.
    auto metadata=*enabled.inputs.receiver_settings;metadata.helper.callsign="TEST";
    const auto public_request=live::detail::helper_tone_precision_request(metadata,metadata.transfer.modem,48000,30,2);
    check(public_request.metadata_symbols==helper::callsign_packet("TEST").count&&public_request.punctuate,
        "lead request ignored actual callsign airtime or punctuation");
    metadata.transfer.key.emplace(Bytes(32,0x52));
    check(live::detail::helper_tone_precision_request(metadata,metadata.transfer.modem,48000,30,2).metadata_symbols==3,
        "encrypted lead prediction used the ignored public callsign");
    metadata.helper.timecode=false;
    check(live::detail::helper_tone_precision_request(metadata,metadata.transfer.modem,48000,30,2).metadata_symbols==0,
        "tone-only lead prediction retained nonexistent metadata airtime");
    metadata.simulation=true;metadata.simulation_phase_noise_degrees_per_sqrt_second=.75;
    const auto simulated_request=live::detail::helper_tone_precision_request(metadata,metadata.transfer.modem,48000,30,2);
    check(std::abs(simulated_request.phase_noise_radians_per_sqrt_second-.75*std::numbers::pi/180)<1e-15,
        "shared simulation lead request ignored the actual channel phase process");
    // Details must show the SAME unknown-peer clock policy as actual ordinary
    // admission, including an explicitly selected GPS policy without FHSS.
    auto gps=enabled;
    auto& gps_options=gps.inputs.options;
    gps_options.clock_sync=clock_sync::parse("GPS_10ms-1ms_region-0ms_offset");
    auto& gps_policy=*gps_options.modem.oscillator_search;
    gps_policy.lf={100,.5};gps_policy.rf={1,.05};gps_policy.margin=3;
    gps_policy.rf_shift_hz=30000000;
    gps_policy.reference=modem::OscillatorReference::independent_audio;
    // Preserve the original failing geometry: the two individually complete
    // RF1ppm grids cannot retain their exact union within the catalogue cap.
    const auto limited_original=modem::oscillator_pattern_search(gps_options.modem);
    const auto limited_admitted=transfer::receiver_oscillator_search(gps_options.modem,gps_options);
    check(!limited_original.limited&&limited_admitted.timing_correction_unavailable&&
        limited_admitted.transmit_rate_correction==0&&
        limited_admitted.requested_clock_half_width_ppm==limited_original.requested_clock_half_width_ppm&&
        limited_admitted.hypotheses==limited_original.hypotheses,
        "GPS capped display fixture did not retain its original unavailable peer UTC-rate union");
    const auto limited_page=gui::planner_page::build(gps,900,true,false);
    check(contains(limited_page,"Peer UTC-rate allowance:")&&
        contains(limited_page,"full DATA-clock union is unavailable within the current passband/catalogue limits")&&
        contains(limited_page,"Displayed search retains the original oscillator domain")&&
        !contains(limited_page,"full DATA-clock union remains in the displayed ordinary search"),
        "capped GPS search claimed complete peer UTC-rate coverage");
    // Resolve the selected ordinary profile and use a finite RF domain that
    // can represent the full independent peer UTC union for the positive case.
    const std::array gps_targets{gps.inputs.target_db_hz};
    gps_options.modem=tuning::receive_profiles(gps_options.modem,gps_targets,
        gps.inputs.mode,gps_options.key.has_value()).front();
    gps_options.modem.oscillator_search->rf.accuracy_ppm=.05;
    const auto unsteered=modem::oscillator_pattern_search(gps_options.modem);
    const auto admitted=transfer::receiver_oscillator_search(gps_options.modem,gps_options);
    const auto utc=transfer::receiver_utc_rate_correction(gps_options.modem,gps_options);
    check(utc>0&&!unsteered.limited&&!admitted.limited&&!admitted.timing_correction_unavailable&&
        admitted.transmit_rate_correction==utc&&
        admitted.requested_clock_half_width_ppm>unsteered.requested_clock_half_width_ppm&&
        admitted.hypotheses!=unsteered.hypotheses,
        "GPS display fixture did not exercise the independent peer UTC-rate union");
    const auto gps_page=gui::planner_page::build(gps,900,true,false);
    std::ostringstream expected_clock;expected_clock<<std::setprecision(3)<<
        "Sample-clock half-width: requested "<<admitted.requested_clock_half_width_ppm<<" ppm";
    check(contains(gps_page,expected_clock.str().c_str())&&
        contains(gps_page,"Peer UTC-rate allowance:")&&
        contains(gps_page,"full DATA-clock union remains in the displayed ordinary search"),
        "oscillator details omitted ordinary GPS peer UTC-rate search geometry");
    auto simulated=enabled.inputs;
    simulated.receiver_work_mode=simulation::ReceiverWorkMode::sampled_simulation;
    simulated.helper_forecast_plan_sampled_capture=true;
    simulated.channel.phase_noise_degrees_per_sqrt_second=.75;
    simulated.receiver_settings->simulation=false;
    simulated.receiver_settings->simulation_phase_noise_degrees_per_sqrt_second=6;
    // An explicit sampled route cannot use a hardware-only layout snapshot.
    // Preserve that original failing input as an ordinary admission negative.
    const auto contradictory_route=gui::planner::build(simulated);
    check(!contradictory_route.available&&
        contradictory_route.error=="receiver settings contradict the planned physical waveform/route"&&
        !contradictory_route.helper_reference.conditional.precision.supported,
        "contradictory ordinary layout reached helper precision forecasting");
    // Keep the valid sampled route while its retained phase process is stale.
    simulated.receiver_settings->simulation=true;
    const auto simulation_model=gui::planner::build(simulated);
    const auto& simulated_point=simulation_model.helper_reference.conditional;
    auto current=*simulated.receiver_settings;current.transfer=simulated.options;
    const std::array simulation_targets{simulated.target_db_hz};
    current.transfer.modem=tuning::receive_profiles(simulated.options.modem,simulation_targets,
        simulated.mode,simulated.options.key.has_value()).front();
    current.simulation=true;current.simulation_clock_error_ppm=simulated.channel.clock_error_ppm;
    current.simulation_phase_noise_degrees_per_sqrt_second=.75;
    current.helper.data_power_w=std::pow(10.,(simulated.tx_dbm-30)/10);
    const auto baseline_requirements=live::detail::helper_envelope_requirements(current,current.transfer.modem,
        live::detail::helper_receive_envelope_bound(current));
    const auto expected_cn0=simulation_model.helper_reference.simulation_level_modeled?
        simulation_model.helper_reference.simulation_cn0_db_hz:simulation_model.helper_reference.cn0_db_hz;
    const auto expected_request=live::detail::helper_tone_precision_request(current,current.transfer.modem,
        simulated_point.physical_sample_rate,expected_cn0,simulated_point.precision.baseline_lead_seconds,
        baseline_requirements.maximum_source_capture_ratio);
    const auto expected_simulation=helper::predict_tone_precision(expected_request);
    check(simulated_point.precision.supported&&expected_simulation.supported&&
        simulated_point.precision.frequency_half_width_hz==expected_simulation.frequency_half_width_hz&&
        simulated_point.precision.extra_lead_seconds==expected_simulation.extra_lead_seconds,
        "Planner lead forecast used stale simulation phase process instead of channel inputs");
    check(simulated_point.precision_reference.cn0_db_hz==expected_request.straight_cn0_db_hz&&
        simulated_point.precision_reference.phase_noise_radians_per_sqrt_second==expected_request.phase_noise_radians_per_sqrt_second,
        "precision presentation lost actual sampled waveform C/N0 or configured phase process");
    // The transmit envelope and Planner select identical weak-reference
    // airtime, but neither may assign it RF precision or receiver cost.
    auto weak_inputs=enabled.inputs;weak_inputs.path_loss_db=230;
    const auto weak_model=gui::planner::build(weak_inputs);
    const auto& weak_point=weak_model.helper_reference.conditional;
    auto weak_settings=*weak_inputs.receiver_settings;weak_settings.transfer=weak_inputs.options;
    const std::array weak_targets{weak_inputs.target_db_hz};
    weak_settings.transfer.modem=tuning::receive_profiles(weak_inputs.options.modem,weak_targets,
        weak_inputs.mode,weak_inputs.options.key.has_value()).front();
    weak_settings.simulation=false;
    weak_settings.simulation_snr_db=weak_inputs.tx_dbm-weak_inputs.path_loss_db-weak_inputs.noise_density_dbm_hz-
        10*std::log10(weak_settings.transfer.modem.sample_rate/2.);
    weak_settings.helper.data_power_w=std::pow(10.,(weak_inputs.tx_dbm-30)/10);
    helper::TonePrecisionPrediction transmit_weak;
    const auto weak_requirements=live::detail::helper_envelope_requirements(weak_settings,weak_settings.transfer.modem,
        live::detail::helper_receive_envelope_bound(weak_settings),{},&transmit_weak);
    const auto weak_envelope=helper::plan_envelope(weak_settings.transfer.modem,weak_requirements);
    check(weak_model.available&&!weak_point.available&&!weak_point.precision.supported&&
        weak_point.precision.extension_capped&&!weak_point.precision.target_reached&&
        weak_point.precision.extra_lead_seconds==36&&transmit_weak.extra_lead_seconds==36&&
        weak_requirements.frequency_precision_extension_seconds==36&&
        weak_point.planned_lead_seconds==static_cast<double>(weak_envelope.lead_seconds)&&
        weak_point.planned_tail_seconds==static_cast<double>(weak_envelope.tail_seconds)&&
        weak_point.uncertainty_hz==0&&weak_point.frequency_rate_hypotheses==0&&!weak_point.sustained&&
        weak_point.reason.find("precision not predicted")!=std::string::npos,
        "weak lead cap disagreed between transmit and Planner or created an assisted RF estimate");
    check(contains(gui::planner_page::build(weak_model,900,true,false),
        "36.000000 s extra-lead cap selected; precision not predicted; target remains unmet"),
        "weak-cap UI omitted its planned airtime or unsupported-precision limit");
    const auto gps_estimate=gui::planner::build(gps.inputs);
    const auto& gps_conditional=gps_estimate.helper_reference.conditional;
    check(gps_conditional.precision.supported&&gps_conditional.frequency_rate_hypotheses>0&&
        gps_conditional.rf_half_width_hz==gps_conditional.uncertainty_hz&&
        gps_conditional.clock_half_width_ppm==admitted.requested_clock_half_width_ppm,
        "same-source GPS forecast added UTC RF error twice or removed original DATA-clock uncertainty");
    // The encrypted Rate360/interleave/FHSS acceptance geometry is genuinely
    // folded before mono capture: nominal [-1425,4425] becomes [-1395,4455]
    // at CFO +30 Hz (before clock error). A generic inverse cannot restore
    // arbitrary complex source spectra: nominal +1000 and -1060 Hz both
    // produce cos(2*pi*1030*t) after CFO +30 and real projection, although
    // their original ordinary real outputs are cos(2*pi*1000*t) and
    // cos(2*pi*1060*t). The receiver must fit the retained RAW input basis.
    auto raw_inputs=enabled.inputs;raw_inputs.mode=tuning::PatternMode::auto_keystream;
    raw_inputs.options.key.emplace(Bytes(32,0x52));
    auto& raw_config=raw_inputs.options.modem;
    raw_config.bandwidth_hz=360;raw_config.sample_rate=14400;
    raw_config.receiver_strategy=modem::ReceiverStrategy::automatic;
    raw_config.dsss=true;raw_config.dsss_factor=10;
    raw_config.outer_dsss_version=modem::OuterDsssVersion::interleaved_v2;
    raw_config.oscillator_search->lf={100,.5};raw_config.oscillator_search->rf={1,.05};
    raw_config.oscillator_search->margin=3;
    raw_config.oscillator_search->reference=modem::OscillatorReference::independent_audio;
    // Match Controller construction order: select the actual keyed private
    // waveform before transmit_hopping validates/freezes its power calendar.
    const std::array raw_targets{raw_inputs.target_db_hz};
    raw_config=tuning::receive_profiles(raw_config,raw_targets,raw_inputs.mode,true).front();
    check(raw_config.scramble&&raw_config.spreading_mode==modem::SpreadingMode::pattern,
        "raw helper fixture did not resolve its encrypted private inner pattern");
    modem::FhssPlan raw_hops;raw_hops.channels=2;
    raw_hops.shifts[0]=30e6;raw_hops.shifts[1]=30.1e6;raw_hops.dwell_seconds=.4;
    raw_inputs.options.audio_timing_error_seconds=0;
    raw_config.fake_hopping=transfer::transmit_hopping(raw_hops,raw_inputs.options);
    raw_inputs.channel.clock_error_ppm=100;raw_inputs.channel.frequency_offset_hz=30;
    raw_inputs.receiver_settings->transfer=raw_inputs.options;
    raw_inputs.helper_forecast_capture_sample_rate=48000;
    raw_inputs.helper_forecast_capture_passband_hz=std::nextafter(24000.,0.);
    raw_inputs.helper_forecast_capture_logical_sample_rate=14400;
    const auto raw_model=gui::planner::build(raw_inputs);
    const auto& raw_point=raw_model.helper_reference.conditional;
    check(raw_model.available&&raw_point.precision.supported&&
        raw_point.reason.find("conditional raw mono Robust source")!=std::string::npos,
        "folded reference forecast pretended its raw PCM was NCO corrected");
    auto raw_options=raw_inputs.options;
    raw_options.modem=tuning::receive_profiles(raw_config,raw_targets,raw_inputs.mode,true).front();
    check(std::abs(helper::occupied_bandwidth(raw_options.modem)-5850)<1e-9,
        "raw helper forecast fixture lost the acceptance folded band");
    auto raw_settings=*raw_inputs.receiver_settings;raw_settings.transfer=raw_options;
    raw_settings.helper.callsign.clear();raw_settings.simulation=false;
    raw_settings.simulation_snr_db=raw_inputs.tx_dbm-raw_inputs.path_loss_db-raw_inputs.noise_density_dbm_hz-
        10*std::log10(raw_options.modem.sample_rate/2.);
    raw_settings.helper.data_power_w=std::pow(10.,(raw_inputs.tx_dbm-30)/10);
    const auto raw_width=live::detail::helper_occupied_bandwidth(raw_settings);
    const auto raw_plan=helper::plan(raw_settings.helper,helper::placement_band(raw_options.modem,raw_width),
        120000000,0,modem::outer_dsss_transmit_gain(raw_options.modem));
    const auto measured_offset=raw_inputs.channel.frequency_offset_hz+
        raw_plan.frequency_hz*raw_inputs.channel.clock_error_ppm*1e-6;
    const auto raw_utc=transfer::receiver_utc_rate_correction(raw_options.modem,raw_options);
    const auto raw_search=modem::helper_reference_search(raw_options.modem,raw_point.uncertainty_hz,
        raw_plan.frequency_hz,measured_offset,raw_utc,
        modem::HelperReferenceClockRelation::common_corrected_source,{},
        modem::PatternUtcClockGrid::complete_expanded_domain);
    check(!raw_search.original_prior_retained&&raw_search.residual_center_hz==measured_offset&&
        raw_search.catalogue.hypotheses.size()==raw_point.frequency_rate_hypotheses&&
        raw_search.rf_half_width_hz==raw_point.rf_half_width_hz&&
        raw_search.catalogue.requested_clock_half_width_ppm==raw_point.clock_half_width_ppm,
        "raw conditional RF/clock geometry disagreed with the ordinary receiver catalogue");
    const auto true_frequency=raw_inputs.channel.frequency_offset_hz+
        raw_options.modem.carrier_hz*raw_inputs.channel.clock_error_ppm*1e-6;
    check(std::abs(true_frequency-(measured_offset+
        (raw_options.modem.carrier_hz-raw_plan.frequency_hz)*raw_inputs.channel.clock_error_ppm*1e-6))<1e-12,
        "raw helper frequency origin omitted the unresolved DATA/helper clock coupling");
    auto raw_channel=raw_inputs.channel;raw_channel.snr_db=raw_settings.simulation_snr_db;
    constexpr std::array<std::uint8_t,1> raw_bit{0};
    const auto raw_transmission=transfer::estimate_binary(raw_bit,raw_options);
    const auto raw_estimate=simulation::estimate_with_receiver_search(raw_transmission,raw_options,true,
        raw_channel,raw_search.catalogue,1,100,raw_inputs.receiver_work_mode,
        raw_inputs.receiver_timing_model,&raw_settings);
    const auto raw_cost=gui::planner::helper_receiver_cost(raw_estimate);
    check(raw_estimate.carrier_in_search&&raw_estimate.clock_in_search&&
        !raw_estimate.confidence_available&&!raw_estimate.reference_probability_available,
        "raw catalogue lost the actual carrier/clock or manufactured PHY confidence");
    const auto same_price=[](double x,double y) {
        return (std::isnan(x)&&std::isnan(y)) || x==y;
    };
    check(raw_point.receiver_cost.finite_available==raw_cost.finite_available&&
        raw_point.receiver_cost.sustained_available==raw_cost.sustained_available&&
        raw_point.receiver_cost.workspace_supported==raw_cost.workspace_supported&&
        same_price(raw_point.receiver_cost.finite_cpu_seconds,raw_cost.finite_cpu_seconds)&&
        same_price(raw_point.receiver_cost.finite_audio_seconds,raw_cost.finite_audio_seconds)&&
        same_price(raw_point.receiver_cost.sustained_realtime_ratio,raw_cost.sustained_realtime_ratio),
        "folded helper CPU did not come from the ordinary RAW waveform/search estimator");
    if(!raw_estimate.receiver_work_supported || !raw_estimate.receiver_workspace_supported ||
       raw_estimate.oscillator_search_limited)
        check(!raw_point.available&&raw_point.reason.find("unsupported")!=std::string::npos,
            "unsupported raw backend was forced into a helper CPU curve");
    // Frequency can also resolve DATA clock in the declared shared-radio,
    // no-UTC model. Price the exact original-cell acquisition selection while
    // retaining the complete ordinary constructor and continuation ledger.
    auto shared_inputs=enabled.inputs;shared_inputs.mode=tuning::PatternMode::auto_keystream;
    shared_inputs.target_db_hz=8;shared_inputs.path_loss_db=180;
    shared_inputs.options.key.emplace(Bytes(32,0x53));
    auto& shared_config=shared_inputs.options.modem;
    shared_config=tuning::resolve(36,8,shared_inputs.mode,true,1500,10,
        modem::OuterDsssVersion::interleaved_v2).config;
    shared_config.oscillator_search.emplace();auto& shared_policy=*shared_config.oscillator_search;
    shared_policy.lf={100,.5};shared_policy.rf={100,.5};shared_policy.margin=3;
    shared_policy.rf_shift_hz=30e6;shared_policy.reference=modem::OscillatorReference::shared_radio;
    shared_inputs.channel.frequency_offset_hz=3000;shared_inputs.channel.clock_error_ppm=100;
    shared_inputs.channel.phase_noise_degrees_per_sqrt_second=.5;
    shared_inputs.receiver_settings->transfer=shared_inputs.options;
    shared_inputs.helper_forecast_capture_sample_rate=48960;
    shared_inputs.helper_forecast_capture_passband_hz=std::nextafter(24480.,0.);
    shared_inputs.helper_forecast_capture_logical_sample_rate=shared_config.sample_rate;
    const auto shared_model=gui::planner::build(shared_inputs);
    const auto& shared_point=shared_model.helper_reference.conditional;
    check(shared_model.available && shared_point.precision.supported && shared_point.shared_frequency_clock &&
        shared_point.shared_frequency_clock->lower_ppm<100 && shared_point.shared_frequency_clock->upper_ppm>100 &&
        shared_point.shared_clock_selected_pairs>0 &&
        shared_point.shared_clock_selected_pairs<shared_point.frequency_rate_hypotheses &&
        shared_point.shared_clock_selected_rates<shared_point.retained_clock_rates &&
        shared_point.available && shared_point.receiver_cost.finite_available &&
        shared_point.receiver_cost.workspace_supported &&
        shared_point.reason.find("full-domain acquisition cells")!=std::string::npos,
        "shared frequency clock lost its matched ordinary original-cell work ledger");
    check(contains(gui::planner_page::build(shared_model,900,true,false),
        "Conditional shared-radio source clock from frequency:") &&
        contains(gui::planner_page::build(shared_model,900,true,false),"not measured lock"),
        "Planner hid frequency-derived clock provenance or its qualification limit");
    raw_inputs.receiver_settings->helper.power_w=0;
    const auto raw_baseline=gui::planner::build(raw_inputs);
    check(raw_baseline.cpu_points.size()==raw_model.cpu_points.size()&&
        raw_baseline.receive_points.size()==raw_model.receive_points.size(),
        "raw helper changed the ordinary curve geometry");
    for(std::size_t i=0;i<raw_baseline.cpu_points.size();++i)
        check(raw_baseline.cpu_points[i].target_db_hz==raw_model.cpu_points[i].target_db_hz&&
            raw_baseline.cpu_points[i].realtime_ratio==raw_model.cpu_points[i].realtime_ratio,
            "raw helper forecast altered the ordinary CPU baseline");
    for(std::size_t i=0;i<raw_baseline.receive_points.size();++i)
        check(raw_baseline.receive_points[i].target_db_hz==raw_model.receive_points[i].target_db_hz&&
            raw_baseline.receive_points[i].success_probability==raw_model.receive_points[i].success_probability,
            "raw helper forecast altered ordinary PHY reception");
    std::cout<<"helper planner forecast PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
