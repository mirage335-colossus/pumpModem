#pragma once
#include "datapump/live.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace datapump::gui::planner {
struct Inputs {
    std::optional<live::Settings> settings;
    std::weak_ptr<triad::v3::receiver_detail::ReceiverBulkBackend> backend;
    std::string backend_id;
    std::optional<std::array<std::uint8_t,16>> gpu_device_uuid;
    double target_db_hz=32;
    double tx_dbm=3;
    double path_loss_db=120;
    double noise_density_dbm_hz=-164;
    std::optional<double> waveform_bandwidth_hz;
    std::optional<helper::MonoPassband> data_passband;
};
struct Point {
    double target_db_hz=0;
    std::optional<double> bit_seconds, cpu_seconds_per_audio_second;
    std::optional<double> rx_data_detection_probability, workspace_fraction;
    std::optional<double> rx_success_probability;
    bool workspace_fits=false;
};
struct TargetSteps {
    std::optional<double> fitted, stronger, weaker;
};
// Checked points in ascending target order, including the requested point.
TargetSteps select_targets(double requested, std::span<const Point>);
struct Model {
    Inputs inputs;
    bool available=false;
    bool calculating=false;
    bool sampling=false;
    unsigned workers=0;
    double received_dbm=0;
    double actual_cn0_db_hz=0;
    std::optional<double> budget_target_db_hz;
    std::string receiver_status;
    std::string error;
    std::optional<Point> selected;
    std::vector<Point> points;
    std::string cpu_status, rx_status, workspace_status;
    std::string sample_status;
    std::optional<double> fitted_target;
    std::optional<double> stronger_fit_target,weaker_fit_target;
};
struct SampleResult {
    static constexpr unsigned trial_budget = 4;
    unsigned trials=0, correct=0;
    unsigned first_pair_correct=0;
    unsigned workers=0;
    std::optional<double> processing_ratio;
    std::string status;
};
Model budget_preview(const Inputs& inputs);
Model build(const Inputs& inputs,std::stop_token stop = {});
SampleResult sample(const Inputs&, std::stop_token);
void add_sample(Model&, const SampleResult&);
void add_sample(Model&, double geometry_bit_seconds, const SampleResult&);
std::string duration(double seconds);
}
