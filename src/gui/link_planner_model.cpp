#include "link_planner_model.hpp"
#include <cmath>
#include <iomanip>
#include <sstream>

namespace datapump::gui::planner {
Model build(const Inputs& inputs,std::stop_token) {
    Model result;
    result.inputs=inputs;
    result.receiver_status="Robust planning is unavailable until the triad backend is connected.";
    if(std::isfinite(inputs.tx_dbm)&&std::isfinite(inputs.path_loss_db)&&
       std::isfinite(inputs.noise_density_dbm_hz)) {
        result.received_dbm=inputs.tx_dbm-inputs.path_loss_db;
        result.actual_cn0_db_hz=result.received_dbm-inputs.noise_density_dbm_hz;
    } else result.error="Link budget inputs must be finite.";
    return result;
}
Model build(const Inputs& inputs,Cache&,std::stop_token stop,
            const std::function<void(const Model&)>& selected,std::optional<TargetSteps>) {
    auto result=build(inputs,stop);
    if(selected&&!stop.stop_requested())selected(result);
    return result;
}
TargetSteps preview_steps(const Inputs&) {return {};}
std::string duration(double seconds) {
    if(!std::isfinite(seconds)||seconds<0)return "Unavailable";
    const double scale=seconds>=86400?86400:seconds>=3600?3600:seconds>=60?60:1;
    std::ostringstream output;
    output<<std::setprecision(3)<<seconds/scale<<(scale==86400?" days":scale==3600?" h":scale==60?" min":" s");
    return output.str();
}
}
