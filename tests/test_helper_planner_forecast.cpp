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
    check(enabled.available&&enabled.helper_reference.enabled&&enabled.helper_reference.budget_available,
        "manual helper mode lost its ordinary plan or physical helper link budget");
    check(enabled.helper_cpu_points.empty()&&enabled.helper_receive_points.empty()&&
        enabled.helper_backend_feasibility_points.empty()&&enabled.helper_work_points.empty()&&
        !enabled.helper_reference.conditional_geometry_available,
        "manual helper reference still executed obsolete mask/cohort/history forecasts");
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
        "The plots show the ordinary receiver baseline",
        "A stabilized RF search/CPU/reception estimate for this reference path is not available yet",
        "helper power does not increase payload SNR or Rate"})
        if(!contains(page,required))throw std::runtime_error(std::string("UI omitted manual reference state/limit: ")+required);
    for(const auto* obsolete:{"Qualified helper total","Conditional helper 1-bit RX","Conditional acquisition work",
        "One global cold probe slot","Helper loss or inconsistency immediately restores broad future acquisition"})
        check(!contains(page,obsolete),"manual reference UI retained an obsolete mask/probe/fallback claim");
    std::cout<<"helper planner forecast PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
