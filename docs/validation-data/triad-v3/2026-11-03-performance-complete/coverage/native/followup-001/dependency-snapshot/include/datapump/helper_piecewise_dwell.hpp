#pragma once

#include "datapump/helper_aided_phase.hpp"

namespace datapump::helper {

// Experimental pulse witness and single-side phase correction. Model selection
// is conditional on the aided acquisition, previous-only frequency coordinates,
// plug-in noise estimates, and the stated bounded within-dwell dynamics.
// A missing detection is NOT evidence that the dwell is physically clean.
struct HelperPiecewiseDwellConfig {
    // With the bounded template family, 4.5 single-quadrature sigma gives a
    // conservative nominal Gaussian union allowance; radial models use 5.3.
    double detection_sigma = 4.5;
    double model_half_width_radians = .10;
    double minimum_support_seconds = .10;
    std::size_t boundary_guard_segments = 2;
    std::size_t maximum_samples = 24000;
};
struct HelperPiecewiseDwellResult {
    bool evaluated = false, disturbance_detected = false, valid = false;
    std::size_t support_first_sample = 0, support_end_sample = 0;
    std::size_t change_first_sample = 0, change_end_sample = 0;
    double anchor_time_seconds = 0;
    double high_phase_radians = 0, low_phase_radians = 0;
    double high_sigma_radians = 0, low_sigma_radians = 0;
    double model_half_width_radians = 0;
    double maximum_score = 0;
    unsigned winning_model = 0; // 1 common phase, 2/3 single phase, 4 common gain,
                                // 5/6 single gain, 7/8 complex single-pilot change
};
// Every dwell has a new independent RF phase. The prediction may only be a
// previous-observation frequency coordinate, never another likelihood row.
// When detected, callers must not ingest the contaminated full-aperture
// measurement; valid=false withdraws all phase authority for this dwell.
// Support offsets are relative to samples; callers must intersect retune and
// interpolation/filter support and account for clock/phase transport.
HelperPiecewiseDwellResult estimate_helper_piecewise_dwell(
    std::span<const std::complex<float>> samples,
    const HelperAidedPhaseConfig& aided_config,
    const HelperPiecewiseDwellConfig& piecewise_config,
    const HelperAidedPhasePrediction& previous_prediction,
    const HelperAidedPhaseResult& gated_baseline,
    double first_sample_time_seconds);
}
