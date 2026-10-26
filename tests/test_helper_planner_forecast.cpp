#include "link_planner_model.hpp"
#include "link_planner_page.hpp"
#include "datapump/helper_carrier.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <set>
using namespace datapump;
namespace {
void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
bool contains(const gui::ui::DocumentNode& node,const char* text) {
    if(node.text.find(text)!=std::string::npos)return true;
    return std::any_of(node.children.begin(),node.children.end(),[&](const auto& child){return contains(child,text);});
}
bool descending_work_axis(const gui::ui::DocumentNode& node) {
    if(node.children.size()==2&&node.children[1].plot_name=="planner/helper-native-work") {
        const auto& labels=node.children[0].children;
        if(labels.size()<2)return false;
        for(std::size_t i=1;i<labels.size();++i)
            if(!(std::stod(labels[i-1].text)>std::stod(labels[i].text)))return false;
        return true;
    }
    return std::any_of(node.children.begin(),node.children.end(),[](const auto& child){return descending_work_axis(child);});
}
}
int main(){try{
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
    const auto enabled=gui::planner::build(in);
    check(enabled.available&&!enabled.helper_cpu_points.empty(),"normal GUI sweep never called helper forecast builder");
    check(enabled.helper_cpu_points.size()==enabled.cpu_points.size(),"conditional CPU gaps lost ordinary target positions");
    check(enabled.helper_backend_feasibility_points.size()==enabled.cpu_points.size(),
        "backend-capacity overlay lost known unsupported target gaps");
    for(const auto& capacity:enabled.helper_backend_feasibility_points)if(capacity.ordinary_phy_available) {
        const auto original=std::find_if(enabled.receive_points.begin(),enabled.receive_points.end(),
            [&](const auto& p){return p.target_db_hz==capacity.target_db_hz;});
        check(original!=enabled.receive_points.end()&&capacity.ordinary_success_probability==original->success_probability,
            "normal GUI backend-capacity overlay altered ordinary PHY probability");
    }

    check(std::any_of(enabled.helper_cpu_points.begin(),enabled.helper_cpu_points.end(),[](const auto& p){
        return p.requirements.frequency_requirements_available;}),"strong expected C/N0 never produced numerical requirements");
    check(std::none_of(enabled.helper_cpu_points.begin(),enabled.helper_cpu_points.end(),[](const auto& p){
        return p.sustained_available||p.forecast_available;}),"missing costs/global utility were silently supplied");
    check(std::none_of(enabled.helper_receive_points.begin(),enabled.helper_receive_points.end(),[](const auto& p){
        return p.confidence_available||p.reference_available;}),"unvalidated helper filter received a fabricated RX probability");
    in.receiver_settings->helper.power_w=0;const auto disabled=gui::planner::build(in);
    check(disabled.cpu_points.size()==enabled.cpu_points.size()&&disabled.receive_points.size()==enabled.receive_points.size(),
        "helper changed ordinary curve target geometry");
    for(std::size_t i=0;i<disabled.cpu_points.size();++i)check(disabled.cpu_points[i].target_db_hz==enabled.cpu_points[i].target_db_hz&&
        disabled.cpu_points[i].realtime_ratio==enabled.cpu_points[i].realtime_ratio,
        "helper forecast altered ordinary CPU baseline");
    for(std::size_t i=0;i<disabled.receive_points.size();++i)check(disabled.receive_points[i].target_db_hz==enabled.receive_points[i].target_db_hz&&
        disabled.receive_points[i].success_probability==enabled.receive_points[i].success_probability,
        "helper forecast altered ordinary RX baseline");
    check(!enabled.helper_work_points.empty()&&std::count_if(enabled.helper_work_points.begin(),enabled.helper_work_points.end(),
        [](const auto& p){return p.native_probe_attempted;})<=12,
        "ordinary GUI omitted bounded representative work queries or exceeded the constructor cap");
    for(const auto& cpu:enabled.helper_cpu_points)if(!cpu.scenario_available) {
        const auto gap=std::find_if(enabled.helper_work_points.begin(),enabled.helper_work_points.end(),
            [&](const auto& work){return work.target_db_hz==cpu.target_db_hz;});
        check(gap!=enabled.helper_work_points.end()&&!gap->native.available&&!gap->native_probe_attempted,
            "work chart bridged a known unsupported scenario or queried its constructor");
    }
    // Reject a supplied scenario at an ordinary target outside the sparse work
    // probe set. Retaining the break must not require a thirteenth constructor.
    auto rejection=in;rejection.receiver_settings->helper.power_w=15000;
    std::set<double> probe_targets{enabled.inputs.target_db_hz};
    const auto stride=std::max<std::size_t>(1,(enabled.cpu_points.size()+9)/10);
    for(std::size_t i=0;i<enabled.cpu_points.size();++i)
        if(i%stride==0)probe_targets.insert(enabled.cpu_points[i].target_db_hz);
    if(!enabled.cpu_points.empty())probe_targets.insert(enabled.cpu_points.back().target_db_hz);
    const auto unqueried=std::find_if(enabled.cpu_points.begin(),enabled.cpu_points.end(),[&](const auto& cpu){
        return !probe_targets.contains(cpu.target_db_hz);});
    check(unqueried!=enabled.cpu_points.end(),"fixture has no target outside the bounded native probes");
    rejection.helper_frequency_scenarios.push_back({unqueried->target_db_hz,{}});
    const auto rejected=gui::planner::build(rejection);
    const auto gap=std::find_if(rejected.helper_work_points.begin(),rejected.helper_work_points.end(),[&](const auto& work){
        return work.target_db_hz==unqueried->target_db_hz;});
    check(gap!=rejected.helper_work_points.end()&&!gap->native.available&&!gap->native_probe_attempted,
        "supplied scenario rejection remained an after-qualified work point");
    auto unknown_capture=in;unknown_capture.receiver_settings->helper.power_w=15000;
    unknown_capture.helper_forecast_capture_sample_rate.reset();unknown_capture.helper_forecast_capture_passband_hz.reset();
    const auto unknown=gui::planner::build(unknown_capture);
    check(std::none_of(unknown.helper_cpu_points.begin(),unknown.helper_cpu_points.end(),[](const auto& point){
        return point.scenario_available||point.backend_sustained_available;}),
        "unknown physical capture rate was replaced with the logical modem rate");
    auto constrained=in;constrained.receiver_settings->helper.power_w=15000;
    constrained.options.dsp_workspace_bytes=1;constrained.receiver_settings->dsp_workspace_bytes=1;
    const auto no_workspace=gui::planner::build(constrained);
    check(std::none_of(no_workspace.helper_cpu_points.begin(),no_workspace.helper_cpu_points.end(),[](const auto& point){
        return point.backend_sustained_available||point.sustained_available;}),
        "conditional CPU curve ignored unavailable ordinary-owner workspace");
    auto extreme=in;extreme.receiver_settings->helper.power_w=15000;extreme.noise_density_dbm_hz=-1000;
    const auto out_of_range=gui::planner::build(extreme);
    check(std::none_of(out_of_range.helper_receive_points.begin(),out_of_range.helper_receive_points.end(),[](const auto& point){
        return point.reference_available||point.confidence_available;}),
        "finite helper reference bypassed the unclamped payload numerical range");
    const auto page=gui::planner_page::build(enabled,900,true,false);
    check(descending_work_axis(page),"work axis skipped or misordered its native tick labels");
    // The actual executed current-target seam superseded the old blanket
    // "joint clock narrowing is not priced" statement. Pin its stronger scope
    // limits while retaining every separate curve/gap/CPU/probability contract.
    for(const auto* required:{"Qualified helper total","Conditional helper 1-bit RX",
        "Missing covariance, backend or component-cost coverage leaves graph gaps",
        "Conditional acquisition work","Conditional backend model",
        "Ordinary PHY at conditional backend capacity","gaps do not predict zero reception",
        "not total CPU or a measured speedup",
        "Frequency-only forecasts retain all original DATA-clock and UTC-steering alternatives",
        "An actually executed joint RF/DATA-clock certificate may illustrate the selected current target using its exact retained subset",
        "its actual source relation decides the retained DATA-clock domain",
        "Generic helpers retain their physical-a/UTC union",
        "an explicit common corrected Robust source measures b directly",
        "a and c individually unresolved and not added again",
        "does not forecast future qualification","Neither case is a GPSDO-equivalent estimate",
        "No CPU percentage, backlog or RX-probability gain follows from these counts"})
        if(!contains(page,required))throw std::runtime_error(std::string("UI omitted conditional scope/limit: ")+required);
    std::cout<<"helper planner forecast PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
