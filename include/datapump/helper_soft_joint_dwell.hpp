#pragma once

#include "datapump/helper_joint_dwell.hpp"
#include <array>

namespace datapump::helper {

// Previous-dwell-only relative clock prediction, in coordinates
// (high-minus-low phase, high-minus-low frequency). The phase includes the
// retained, unknown differential-phase intercept. Both covariance blocks must
// conservatively cover their unknown off-diagonal terms; when only marginal
// sigmas are known, 2*diag(sigma^2) is a valid covariance upper bound.
struct HelperSoftJointPrediction {
    bool valid=false, previous_observations_only=false, reference_preserved=false;
    unsigned alternatives=0;
    double time_seconds=0, latest_observation_seconds=0, reference_input_seconds=0;
    double phase_difference_radians=0, frequency_difference_hz=0;
    std::array<std::array<double,2>,2> thermal_covariance{}, process_covariance{};
    double phase_model_half_width_radians=0, frequency_model_half_width_hz=0;
};

struct HelperSoftJointConfig {
    double consistency_sigma_multiplier=5;
    double maximum_prediction_age_seconds=2;
    // Refuse an unresolved phase cycle, including bounded model allowances.
    double maximum_phase_innovation_support_radians=1.5707963267948966;
};

struct HelperSoftJointResult {
    bool valid=false, fallback_required=true;
    double time_seconds=0, reference_input_seconds=0;
    double first_sample_time_seconds=0, end_sample_time_seconds=0;
    // Current fresh high-pilot phase and frequency; RF phase is not predicted
    // across hops. Other entries permit a complete covariance-aware transport.
    // Coordinates: (fH Hz, fL Hz, phiH rad, phiL rad).
    std::array<double,4> value{};
    // Thermal and process marginal standard deviations propagated separately.
    // Each is an L1 upper bound across current and previous data, accounting
    // for unknown cross-time correlation; they are not a posterior covariance.
    std::array<double,4> thermal_sigma{}, process_sigma{}, model_half_width{};
    double phase_innovation_radians=0, frequency_innovation_hz=0;
    double phase_innovation_support_radians=0, frequency_innovation_support_hz=0;
};

// Correction only. Call only with the prior obtained BEFORE current samples;
// ingest the fresh raw likelihood once, never ingest this conditioned result.
// Conditional on valid reported covariance/model radii, disjoint thermal
// samples, local two-tone linearization and a unique phase branch. The caller
// must separately include transport, clock evolution and interpolation/filter
// support before admitting output samples. A failure requires the existing
// high-only or acquisition fallback; it does not reset the original reference.
HelperSoftJointResult condition_helper_joint_dwell(
    const HelperJointDwellResult& fresh,
    const HelperSoftJointPrediction& previous,
    const HelperSoftJointConfig& config = {});

} // namespace datapump::helper
