#pragma once

#include "datapump/triad_receiver.hpp"
#include "datapump/triad_shaping.hpp"

#include <optional>
#include <string>
#include <vector>

namespace datapump::triad::v3 {

// A provisional true-atom model, not a receiver sensitivity/capacity result.
// These inputs are agreed public profile settings. In particular planning_bits
// is fixed for a profile and must never be the length of a received/TX draft.
struct DurationOptions {
    double target_cn0_db_hz = 32;
    double arrival_lower_seconds = 0, arrival_upper_seconds = 0;
    double helper_lead_seconds = 0, pulse_halo_seconds = 0;
    bool stochastic_channel = false;
    std::size_t planning_bits = 1;
    double correct_probability = .99;
    double outage_fraction = 0, retained_fraction = 1;
    double maximum_active_seconds = 315576000.;
    unsigned maximum_evaluations = 128;
    // A smaller predeclared full-grid spacing can be charged explicitly.
    // Native full-path resolution may further reduce it with duration.
    double frequency_step_hz = .01;
    // Original fixed affine residual-component prior, never a selected count.
    std::size_t residual_state_count = 1;
    std::optional<double> manual_active_seconds;
    ShapingOptions shaping_options{};
};

enum class DurationStatus {
    provisional_plan,
    manual_override,
    duration_limit,
    evaluation_limit,
    geometry_limit
};

struct DurationPrediction {
    std::uint64_t tiles = 0;
    double active_seconds = 0, elapsed_seconds = 0;
    double energy_n0 = 0, effective_modes = 0;
    double true_atom_log_lr_threshold = 0, mean_true_atom_log_lr = 0;
    double log_miss_bound = 0, detection_lower_bound = 0;
    double log_grid_count = 0, frequency_step_hz = 0;
    double origin_shift_allowance_seconds = 0;
    bool meets_detection_allocation = false;
};

struct DurationPlan {
    DurationStatus status = DurationStatus::duration_limit;
    std::optional<DurationPrediction> selected, previous_legal;
    // Present only after selected duration passes native geometry/shaping.
    std::optional<ShapingPlan> shaping;
    std::vector<DurationPrediction> evaluated;
    double per_bit_detection_miss_budget = 0;
    std::string reason;
    // Intentionally never promoted by a successful mathematical selection.
    bool correct_probability_qualified = false;
    bool physical_null_qualified = false;
    bool model_qualified = false;
};

double duration_effective_modes(double seconds, double width_hz,
                                double phase_diffusion_rad_sqrt_s, unsigned rays = 1);
DurationPrediction duration_prediction(double energy_n0, double modes,
                                      double threshold, bool stochastic_channel);
DurationPlan plan_duration(const ShapingPlan&, const ReceiverConfig&, const DurationOptions&);
const char* duration_status_name(DurationStatus);

} // namespace datapump::triad::v3
