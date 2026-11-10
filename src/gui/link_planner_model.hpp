#pragma once
#include "datapump/transfer.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>

namespace datapump::gui::planner {
// Saved planning inputs belong to the UI. Receiver probability and CPU
// forecasts require qualification against the native receiver.
struct Inputs {
    transfer::Options options;
    double target_db_hz=32;
    double tx_dbm=3;
    double path_loss_db=120;
    double noise_density_dbm_hz=-164;
    std::size_t wire_bits=1;
    bool empty_draft=false;
    unsigned dsp_workspace_percent=0;
};
struct Model {
    Inputs inputs;
    bool available=false;
    bool calculating=false;
    bool automatic_mode=true;
    double received_dbm=0;
    double actual_cn0_db_hz=0;
    std::string receiver_status;
    std::string error;
    std::optional<double> stronger_fit_target,weaker_fit_target;
};
struct TargetSteps { std::optional<double> stronger,weaker; };
// Retained request boundary for the application; it owns no receiver cache.
class Cache {};
Model build(const Inputs& inputs,std::stop_token stop = {});
Model build(const Inputs& inputs,Cache&,std::stop_token stop = {},
            const std::function<void(const Model&)>& selected = {},
            std::optional<TargetSteps> supplied_steps = std::nullopt);
TargetSteps preview_steps(const Inputs& inputs);
std::string duration(double seconds);
}
