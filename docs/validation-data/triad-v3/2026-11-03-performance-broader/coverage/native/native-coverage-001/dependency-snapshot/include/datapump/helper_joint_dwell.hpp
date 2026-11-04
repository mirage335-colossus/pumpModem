#pragma once

#include "datapump/helper_aided_phase.hpp"

namespace datapump::helper {

// Optional local, current-dwell-only likelihood. The preceding aided result
// supplies acquisition gates and local noise estimates, but is NOT ingested
// alongside this result. Prediction supplies coordinates and fallback bounds,
// never a prior likelihood row. Selection and plug-in covariance remain
// conditional on the aided gate and the declared locally stationary, white
// noise/two-tone model; neither is a universal interference guarantee.
struct HelperJointDwellConfig {
    double sample_rate = 0;
    double phase_noise_radians_per_sqrt_second = 0;
    double maximum_residual_hz = .75;
    double maximum_fit_score = 30;
    double frequency_drift_bound_hz_per_second = 0;
    std::size_t maximum_samples = 24000;
    // Reuse the one immutable 8,640-sample Hann window pair. Set false to
    // reproduce per-call window construction; signal-dependent correlations
    // and all covariance/process calculations are unchanged. Other aperture
    // sizes and non-default rounding modes always use per-call construction.
    bool use_static_window_cache = true;
};
struct HelperJointDwellResult {
    bool valid = false;
    bool fallback_required = true;
    double fit_score = 0;
    // Conditional bounded-model allowance for the fresh phase and phase
    // difference, separate from the stochastic covariance. It depends on the
    // declared within-aperture drift bound and plug-in two-tone model. The
    // observed GLS weights are frozen: their data-dependent changes under a
    // disturbance are not bounded by this local linearized allowance.
    double common_phase_model_half_width_radians = 0;
    double phase_difference_model_half_width_radians = 0;
    HelperAidedPhaseResult observation;
};

HelperJointDwellResult estimate_helper_joint_dwell(
    std::span<const std::complex<float>> samples,
    const HelperJointDwellConfig& config,
    const HelperAidedPhasePrediction& previous_prediction,
    const HelperAidedPhaseResult& gated_aided_observation);

} // namespace datapump::helper
